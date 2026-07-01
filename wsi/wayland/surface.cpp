/*
 * Copyright (c) 2021, 2024-2025 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/** @file
 * @brief Implementation of a Wayland WSI Surface
 */

#include "surface.hpp"
#include "swapchain.hpp"
#include "surface_properties.hpp"
#include "wl_object_owner.hpp"
#include "wl_helpers.hpp"
#include "util/log.hpp"

#include <drm_fourcc.h>

namespace wsi
{
namespace wayland
{

struct formats_vector
{
   formats_vector(util::vector<util::drm::drm_format_pair> *format_list)
      : formats(format_list)
   {
   }

   util::vector<util::drm::drm_format_pair> *formats{ nullptr };
   bool is_out_of_memory{ false };
};

namespace
{
/* Handler for format event of the zwp_linux_dmabuf_v1 interface. */
VWL_CAPI_CALL(void)
zwp_linux_dmabuf_v1_format_impl(void *data, struct zwp_linux_dmabuf_v1 *dma_buf, uint32_t drm_format) VWL_API_POST
{
   UNUSED(data);
   UNUSED(dma_buf);
   UNUSED(drm_format);
}

/* Handler for modifier event of the zwp_linux_dmabuf_v1 interface. */
VWL_CAPI_CALL(void)
zwp_linux_dmabuf_v1_modifier_impl(void *data, struct zwp_linux_dmabuf_v1 *dma_buf, uint32_t drm_format,
                                  uint32_t modifier_hi, uint32_t modifier_low) VWL_API_POST
{
   UNUSED(dma_buf);
   auto *drm_supported_formats = reinterpret_cast<formats_vector *>(data);

   util::drm::drm_format_pair format = {};
   format.fourcc = drm_format;
   format.modifier = (static_cast<uint64_t>(modifier_hi) << 32) | modifier_low;

   if (!drm_supported_formats->is_out_of_memory)
   {
      /* wlroots-based compositors advertise DRM_FORMAT_MOD_INVALID when they effectively only support
       * INVALID + LINEAR, but the Mali stack cannot allocate against a target modifier of INVALID on
       * this path, so substitute LINEAR (universally allocatable and importable). */
      format.modifier =
         (format.modifier == DRM_FORMAT_MOD_INVALID) ? DRM_FORMAT_MOD_LINEAR : format.modifier;
      drm_supported_formats->is_out_of_memory = !drm_supported_formats->formats->try_push_back(format);
   }
}

/* Handler for clock_id event of the wp_presentation interface. */
VWL_CAPI_CALL(void)
wp_presentation_clock_id_impl(void *data, struct wp_presentation *wp_presentation,
                              uint32_t compositor_clockid) VWL_API_POST
{
   UNUSED(wp_presentation);

   clockid_t *clockid = static_cast<clockid_t *>(data);
   *clockid = compositor_clockid;
}

} // namespace

/**
 * @brief Listener for the zwp_linux_dmabuf_v1 interface
 */
const zwp_linux_dmabuf_v1_listener dma_buf_listener = {
   .format = zwp_linux_dmabuf_v1_format_impl,
   .modifier = zwp_linux_dmabuf_v1_modifier_impl,
};

/**
 * @brief Register listener for the zwp_linux_dmabuf_v1 interface to query
 *        supported formats and modifiers.
 *
 * @param[in]  dmabuf_interface            Object of the zwp_linux_dmabuf_v1 interface.
 * @param[out] drm_supported_format_query  Vector which will be filled with the supported drm
 *                                         formats and their modifiers.
 *
 * @retval VK_SUCCESS                    Indicates success.
 * @retval VK_ERROR_UNKNOWN              Indicates one of the Wayland functions failed.
 */
static VkResult register_supported_format_and_modifier_listener(zwp_linux_dmabuf_v1 *dmabuf_interface,
                                                                formats_vector *drm_supported_format_query)
{
   int res = zwp_linux_dmabuf_v1_add_listener(dmabuf_interface, &dma_buf_listener, drm_supported_format_query);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add zwp_linux_dmabuf_v1 listener.");
      return VK_ERROR_UNKNOWN;
   }

   return VK_SUCCESS;
}

/**
 * @brief Listener for the wp_presentation interface
 */
const wp_presentation_listener presentation_listener = {
   .clock_id = wp_presentation_clock_id_impl,
};

/**
 * @brief Register listener for clock_id event for wp_presentation interface
 *
 * @param presentation_interface wp_presentation interface
 * @param clockid Clock ID pointer that will get the assigned clock_id from the event
 *
 * @retval VK_SUCCESS                    Indicates success.
 * @retval VK_ERROR_UNKNOWN              Indicates one of the Wayland functions failed.
 */
static VkResult register_clock_id_listener(wp_presentation *presentation_interface, clockid_t *clockid)
{
   int res = wp_presentation_add_listener(presentation_interface, &presentation_listener, clockid);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add wp_presentation listener.");
      return VK_ERROR_UNKNOWN;
   }

   return VK_SUCCESS;
}

struct surface::init_parameters
{
   const util::allocator &allocator;
   wl_display *display;
   wl_surface *surf;
};

surface::surface(const init_parameters &params)
   : wsi::surface()
   , wayland_display(params.display)
   , surface_queue(nullptr)
   , wayland_surface(params.surf)
   , m_supported_formats(params.allocator)
   , properties(this, params.allocator)
   , last_frame_callback(nullptr)
   , present_pending(false)
{
}

VWL_CAPI_CALL(void)
surface_registry_handler(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface,
                         uint32_t version) VWL_API_POST
{
   auto wsi_surface = reinterpret_cast<wsi::wayland::surface *>(data);

   if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name) && version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION)
   {
      zwp_linux_dmabuf_v1 *dmabuf_interface_obj = reinterpret_cast<zwp_linux_dmabuf_v1 *>(wl_registry_bind(
         wl_registry, name, &zwp_linux_dmabuf_v1_interface, ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION));

      if (dmabuf_interface_obj == nullptr)
      {
         WSI_LOG_ERROR("Failed to get zwp_linux_dmabuf_v1 interface.");
         return;
      }

      wsi_surface->dmabuf_interface.reset(dmabuf_interface_obj);
   }
   else if (!strcmp(interface, zwp_linux_explicit_synchronization_v1_interface.name))
   {
      zwp_linux_explicit_synchronization_v1 *explicit_sync_interface_obj =
         reinterpret_cast<zwp_linux_explicit_synchronization_v1 *>(
            wl_registry_bind(wl_registry, name, &zwp_linux_explicit_synchronization_v1_interface, 1));

      if (explicit_sync_interface_obj == nullptr)
      {
         WSI_LOG_ERROR("Failed to get zwp_linux_explicit_synchronization_v1 interface.");
         return;
      }

      wsi_surface->explicit_sync_interface.reset(explicit_sync_interface_obj);
   }
   else if (!strcmp(interface, wp_presentation_interface.name))
   {
      wp_presentation *wp_presentation_obj =
         reinterpret_cast<wp_presentation *>(wl_registry_bind(wl_registry, name, &wp_presentation_interface, 1));

      if (wp_presentation_obj == nullptr)
      {
         WSI_LOG_ERROR("Failed to get wp_presentation interface.");
         return;
      }

      wsi_surface->presentation_time_interface.reset(wp_presentation_obj);
   }
}

bool surface::init()
{
   surface_queue.reset(wl_display_create_queue(wayland_display));
   if (surface_queue.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to create wl surface queue.");
      return false;
   }

   auto display_proxy = make_proxy_with_queue(wayland_display, surface_queue.get());
   if (display_proxy == nullptr)
   {
      WSI_LOG_ERROR("Failed to create wl display proxy.");
      return false;
   };

   auto registry = wayland_owner<wl_registry>{ wl_display_get_registry(display_proxy.get()) };
   if (registry == nullptr)
   {
      WSI_LOG_ERROR("Failed to get wl display registry.");
      return false;
   }

   const wl_registry_listener registry_listener = { surface_registry_handler, nullptr };
   int res = wl_registry_add_listener(registry.get(), &registry_listener, this);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add registry listener.");
      return false;
   }

   res = wl_display_roundtrip_queue(wayland_display, surface_queue.get());
   if (res < 0)
   {
      WSI_LOG_ERROR("Roundtrip failed.");
      return false;
   }

   if (dmabuf_interface.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to obtain zwp_linux_dma_buf_v1 interface.");
      return false;
   }

   /* RK3588: explicit sync is optional. If the compositor does not provide
    * zwp_linux_explicit_synchronization_v1, fall back to implicit (CPU fence) sync. */
   if (explicit_sync_interface.get() != nullptr)
   {
      auto surface_sync_obj =
         zwp_linux_explicit_synchronization_v1_get_synchronization(explicit_sync_interface.get(), wayland_surface);
      if (surface_sync_obj == nullptr)
      {
         WSI_LOG_ERROR("Failed to retrieve surface synchronization interface");
         return false;
      }

      surface_sync_interface.reset(surface_sync_obj);
   }

   VkResult vk_res = VK_SUCCESS;
   if (presentation_time_interface.get() != nullptr)
   {
      vk_res = register_clock_id_listener(presentation_time_interface.get(), &m_clockid);
      if (vk_res != VK_SUCCESS)
      {
         return false;
      }
   }

   formats_vector drm_supported_formats(&m_supported_formats);
   vk_res = register_supported_format_and_modifier_listener(dmabuf_interface.get(), &drm_supported_formats);
   if (vk_res != VK_SUCCESS)
   {
      return false;
   }

   res = wl_display_roundtrip_queue(wayland_display, surface_queue.get());
   if (res < 0)
   {
      WSI_LOG_ERROR("Roundtrip failed.");
      return false;
   }

   if (drm_supported_formats.is_out_of_memory)
   {
      WSI_LOG_ERROR("Host got out of memory for DRM format query.");
      return false;
   }

   return true;
}

util::unique_ptr<surface> surface::make_surface(const util::allocator &allocator, wl_display *display, wl_surface *surf)
{
   init_parameters params{ allocator, display, surf };
   auto wsi_surface = allocator.make_unique<surface>(params);
   if (wsi_surface != nullptr)
   {
      if (wsi_surface->init())
      {
         return wsi_surface;
      }
   }
   return nullptr;
}

surface::~surface()
{
}

wsi::surface_properties &surface::get_properties()
{
   return properties;
}

util::unique_ptr<swapchain_base> surface::allocate_swapchain(layer::device_private_data &dev_data,
                                                             const VkAllocationCallbacks *allocator)
{
   util::allocator alloc{ dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, allocator };
   return util::unique_ptr<swapchain_base>(alloc.make_unique<swapchain>(dev_data, allocator, *this));
}

static void frame_done(void *data, wl_callback *cb, uint32_t time)
{
   UNUSED(time);
   UNUSED(cb);

   bool *present_pending = reinterpret_cast<bool *>(data);
   assert(present_pending);

   *present_pending = false;
}

bool surface::set_frame_callback()
{
   /* request a hint when we can present the _next_ frame */
   auto surface_proxy = make_proxy_with_queue(wayland_surface, surface_queue.get());
   if (surface_proxy == nullptr)
   {
      WSI_LOG_ERROR("failed to create wl_surface proxy");
      return false;
   }

   /* Reset will also destroy the previous callback object. */
   last_frame_callback.reset(wl_surface_frame(surface_proxy.get()));
   if (last_frame_callback.get() == nullptr)
   {
      WSI_LOG_ERROR("Failed to create frame callback.");
      return false;
   }

   static const wl_callback_listener frame_listener = { frame_done };
   present_pending = true;
   int res = wl_callback_add_listener(last_frame_callback.get(), &frame_listener, &present_pending);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add frame done callback listener.");
      return false;
   }

   return true;
}

bool surface::wait_next_frame_event()
{
   /*
    * In a previous present call we sent a wl_surface::frame request, which will
    * trigger an event when the compositor starts a redraw using the previous frame
    * we sent. If the compositor isn't sending us frame events at least every second
    * we don't wait indefinitely so we don't block the next image presentation if
    * we are, e.g. minimised.
    */
   const int timeout = 1000;
   while (present_pending)
   {
      int res = dispatch_queue(wayland_display, surface_queue.get(), timeout);
      if (res < 0)
      {
         WSI_LOG_ERROR("Error while waiting for the compositor to send the next frame event.");
         return false;
      }
      else if (res == 0)
      {
         WSI_LOG_INFO("Wait for frame event timed out, present anyway.");
         present_pending = false;
      }
   }

   return true;
}

} // namespace wayland
} // namespace wsi
