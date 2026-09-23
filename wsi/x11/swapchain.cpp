/*
 * Copyright (c) 2017-2022, 2026 Arm Limited.
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

/**
 * @file swapchain.cpp
 *
 * @brief Contains the implementation for a x11 swapchain.
 */

#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <system_error>
#include <thread>

#include <sys/shm.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

#include <xcb/shm.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "swapchain.hpp"
#include "util/log.hpp"
#include "util/macros.hpp"
#include "wsi/external_memory.hpp"
#include "wsi/image_backing_memory_external.hpp"
#include "wsi/wsi_alloc_utils.hpp"
#include "wsi/swapchain_base.hpp"
#include "wsi/extensions/present_id.hpp"
#include "wsi/extensions/swapchain_maintenance.hpp"
#include "wsi/extensions/external_memory_extension.hpp"
#include "wsi/extensions/image_compression_control.hpp"
#include "shm_presenter.hpp"
#include "dri3_presenter.hpp"
#include "present_wait_x11.hpp"
#include "util/drm/drm_utils.hpp"

#include <drm_fourcc.h>

namespace wsi
{
namespace x11
{

#define X11_SWAPCHAIN_MAX_PENDING_COMPLETIONS 128

/**
 * @brief Build the importable format/modifier set for DRI3 by asking the X server.
 *
 * DRI3 presents through the X server (not KMS), so the correct, topology-independent source of
 * supported formats is the server itself via DRI3 1.2 @c xcb_dri3_get_supported_modifiers - the
 * analogue of how the Wayland backend sources formats from the compositor. Falls back to LINEAR +
 * MOD_INVALID when the server is DRI3 < 1.2 or advertises nothing (MOD_INVALID is the path the
 * patched Mali Xwayland handles).
 */
static void query_dri3_supported_formats(xcb_connection_t *connection, xcb_window_t window, uint32_t fourcc,
                                         uint8_t depth, uint8_t bpp, util::vector<util::drm::drm_format_pair> &out)
{
   xcb_dri3_get_supported_modifiers_cookie_t cookie = xcb_dri3_get_supported_modifiers(connection, window, depth, bpp);
   xcb_dri3_get_supported_modifiers_reply_t *reply =
      xcb_dri3_get_supported_modifiers_reply(connection, cookie, nullptr);
   if (reply != nullptr)
   {
      const uint64_t *mods = xcb_dri3_get_supported_modifiers_window_modifiers(reply);
      const int count = xcb_dri3_get_supported_modifiers_window_modifiers_length(reply);
      for (int i = 0; i < count; i++)
      {
         (void)out.try_push_back(util::drm::drm_format_pair{ fourcc, mods[i] });
      }
      free(reply);
   }

   if (out.size() == 0)
   {
      (void)out.try_push_back(util::drm::drm_format_pair{ fourcc, DRM_FORMAT_MOD_LINEAR });
      (void)out.try_push_back(util::drm::drm_format_pair{ fourcc, DRM_FORMAT_MOD_INVALID });
   }
}

void x11_image_data::release_x_resources()
{
   /* Checked requests whose replies are discarded: no round trip, and a failure is dropped instead
    * of reaching the application's event queue, where Xlib's default error handler would exit. */
   bool released = false;
   if (pixmap != XCB_PIXMAP_NONE)
   {
      xcb_discard_reply(connection, xcb_free_pixmap_checked(connection, pixmap).sequence);
      pixmap = XCB_PIXMAP_NONE;
      released = true;
   }
   if (shm_seg != XCB_NONE)
   {
      xcb_discard_reply(connection, xcb_shm_detach_checked(connection, shm_seg).sequence);
      shm_seg = XCB_NONE;
      released = true;
   }
   if (shm_seg_alt != XCB_NONE)
   {
      xcb_discard_reply(connection, xcb_shm_detach_checked(connection, shm_seg_alt).sequence);
      shm_seg_alt = XCB_NONE;
      released = true;
   }
   if (released)
   {
      xcb_flush(connection);
   }

   /* The server holds its own attachment until it processes the detach above, so the client mappings
    * can go now; the segments were marked IPC_RMID at creation, so the kernel frees them after both. */
   if (shm_addr != nullptr && shm_addr != (void *)-1)
   {
      if (shmdt(shm_addr) != 0)
      {
         WSI_LOG_ERROR("Failed to detach shared memory: errno=%d", errno);
      }
      shm_addr = nullptr;
   }
   if (shm_addr_alt != nullptr && shm_addr_alt != (void *)-1)
   {
      if (shmdt(shm_addr_alt) != 0)
      {
         WSI_LOG_ERROR("Failed to detach alternate shared memory: errno=%d", errno);
      }
      shm_addr_alt = nullptr;
   }
}

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator,
                     surface &wsi_surface)
   : swapchain_base(dev_data, pAllocator)
   , m_connection(wsi_surface.get_connection())
   , m_window(wsi_surface.get_window())
   , m_wsi_surface(&wsi_surface)
   , m_image_factory(m_allocator, m_device_data)
   , m_send_sbc(0)
   , m_target_msc(0)
   , m_last_present_msc(0)
   , m_present_event_thread_run(false)
   , m_thread_status_lock()
   , m_thread_status_cond()
{
}

swapchain::~swapchain()
{
   bool thread_may_wait_for_x = false;
   {
      auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);
      m_present_event_thread_run = false;
      thread_may_wait_for_x = m_use_dri3 && dri3_present_events_due();
      m_thread_status_cond.notify_all();
   }

   /* A DRI3 event thread that is blocked waiting for a Present event only sees the stop request once an
    * event arrives, so provoke one. Should that fail, rather than hang in join(), leave the thread blocked -
    * and the special-event queue it waits on registered - once it has had a moment to stop by itself. */
   bool selected_on_root = false;
   if (thread_may_wait_for_x && m_present_event_thread.joinable() && !wake_present_event_thread(selected_on_root))
   {
      auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);
      const bool exited = m_thread_status_cond.wait_for(thread_status_lock, std::chrono::milliseconds(100),
                                                        [this] { return m_present_event_thread_exited; });
      thread_status_lock.unlock();
      if (!exited)
      {
         WSI_LOG_ERROR("Failed to wake the Present event thread; abandoning it");
         {
            std::lock_guard<std::mutex> control_lock(m_present_event_thread_control->lock);
            m_present_event_thread_control->abandoned = true;
         }
         m_present_event_thread.detach();
         m_presenter->abandon_present_special_event();
      }
   }

   /* Join whenever the thread exists, even if it has already stopped by itself (an X connection error
    * or a swapchain error ends it early): destroying a joinable std::thread calls std::terminate. */
   if (m_present_event_thread.joinable())
   {
      m_present_event_thread.join();
   }

   if (selected_on_root)
   {
      /* An empty event mask frees the event context again. */
      const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
      const uint32_t event_id = static_cast<dri3_presenter *>(m_presenter.get())->get_present_event_id();
      xcb_discard_reply(m_connection, xcb_present_select_input_checked(m_connection, event_id, root, 0).sequence);
      xcb_flush(m_connection);
   }
   /* No completion will send an image still waiting behind the present in flight any more. */
   {
      auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);
      if (m_held_present.has_value())
      {
         unpresent_image(m_held_present->image_index);
         m_held_present.reset();
      }
   }

   /* Call the base's teardown. The per-image X resources are released afterwards, when the images'
    * x11_image_data is destroyed - once teardown has waited for pending presents and stopped the
    * presentation thread, so no present_image can still be using them. */
   teardown();
}

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread)
{
   UNUSED(device);

   if (m_wsi_surface == nullptr)
   {
      WSI_LOG_ERROR("X11 swapchain init_platform: m_wsi_surface is null");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Prefer DRI3 + Present (zero-copy); fall back to MIT-SHM when the X server lacks DRI3/Present. */
   try
   {
      /* WSI_X11_FORCE_SHM forces the MIT-SHM path (CPU copy) - useful for A/B comparison against DRI3
       * and as an escape hatch for workloads that misbehave on DRI3. */
      const bool force_shm = (getenv("WSI_X11_FORCE_SHM") != nullptr);
      /* DRI3 strategy: default is true zero-copy (OPTION_NONE); set WSI_X11_DRI3_COPY to select
       * GPU-copy (OPTION_COPY) - the server blits the pixmap, trading a blit for deterministic
       * recycling. Pacing is separate (present mode), so the default is paced zero-copy (FIFO). */
      const bool dri3_copy = (getenv("WSI_X11_DRI3_COPY") != nullptr);
      auto dri3 = force_shm ? std::unique_ptr<dri3_presenter>() : std::make_unique<dri3_presenter>();
      if (dri3 != nullptr && dri3->is_available(m_connection, m_wsi_surface))
      {
         dri3->set_copy_mode(dri3_copy);
         m_presenter = std::move(dri3);
         m_use_dri3 = true;
         m_dri3_copy_mode = dri3_copy;
      }
      else
      {
         auto shm = std::make_unique<shm_presenter>();
         if (!shm->is_available(m_connection, m_wsi_surface))
         {
            WSI_LOG_ERROR("Neither DRI3 nor SHM presentation is available");
            return VK_ERROR_INITIALIZATION_FAILED;
         }
         m_presenter = std::move(shm);
         m_use_dri3 = false;
      }
   }
   catch (const std::exception &e)
   {
      WSI_LOG_ERROR("Exception creating presentation strategy: %s", e.what());
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* DRI3 presents GPU-local dma-buf images, so it needs the wsialloc allocator and a dma-buf image
    * factory. If that setup fails (e.g. the X server can't supply usable DRI3 formats), fall back to
    * the always-available SHM path rather than failing swapchain creation outright. */
   if (m_use_dri3)
   {
      VkResult dri3_result = VK_ERROR_INITIALIZATION_FAILED;
      auto wsi_allocator = swapchain_wsialloc_allocator::create();
      if (wsi_allocator.has_value())
      {
         m_wsi_allocator = m_allocator.make_unique<swapchain_wsialloc_allocator>(std::move(wsi_allocator.value()));
         if (m_wsi_allocator != nullptr)
         {
            dri3_result = init_image_factory(*swapchain_create_info);
         }
      }

      if (dri3_result != VK_SUCCESS)
      {
         WSI_LOG_WARNING("DRI3 setup failed (%d); falling back to SHM presentation", dri3_result);
         m_presenter.reset();
         m_wsi_allocator.reset();
         m_use_dri3 = false;
         m_dri3_copy_mode = false;

         try
         {
            auto shm = std::make_unique<shm_presenter>();
            if (shm == nullptr || !shm->is_available(m_connection, m_wsi_surface))
            {
               WSI_LOG_ERROR("SHM presentation unavailable after DRI3 fallback");
               return VK_ERROR_INITIALIZATION_FAILED;
            }
            m_presenter = std::move(shm);
         }
         catch (const std::exception &e)
         {
            WSI_LOG_ERROR("Exception creating SHM fallback presenter: %s", e.what());
            return VK_ERROR_INITIALIZATION_FAILED;
         }
      }
   }

   if (!m_use_dri3)
   {
      TRY_LOG_CALL(init_image_factory(*swapchain_create_info));
   }

   /* Pacing follows the present mode (FIFO/FIFO_RELAXED -> paced). GPU-copy + unpaced is the only cell
    * that needs the fixed deferred-release pipeline (buffers must free immediately so the app can run
    * ahead); every other cell recycles via PresentIdleNotify. Computed after the final strategy is
    * known (it may have fallen back to SHM above). */
   const bool paced =
      (m_present_mode == VK_PRESENT_MODE_FIFO_KHR || m_present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
   m_dri3_deferred_release = m_dri3_copy_mode && !paced;

   /* Log the chosen cell once per process: per-swapchain state but constant in practice, and logging
    * it on every swapchain creation floods the log for apps that recreate them often. */
   static bool logged_presentation = false;
   if (!logged_presentation)
   {
      logged_presentation = true;
      if (m_use_dri3)
      {
         WSI_LOG_INFO("X11 swapchain using DRI3 presentation (%s, %s)", m_dri3_copy_mode ? "GPU-copy" : "zero-copy",
                      paced ? "paced" : "unpaced");
      }
      else
      {
         WSI_LOG_INFO("X11 swapchain using SHM presentation");
      }
   }

   VkResult init_result = m_presenter->init(m_connection, m_window, m_wsi_surface);
   if (init_result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Failed to initialize presentation strategy");
      return init_result;
   }

   /* DRI3 drives image recycling from Present events on this queue; null for SHM. */
   m_present_special_event = m_presenter->get_present_special_event();

   /* Set the run flag before the thread starts rather than from the thread itself: a swapchain destroyed
    * before the thread got scheduled would otherwise see it unset, and the thread would then run on a
    * destroyed swapchain. */
   {
      auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);
      m_present_event_thread_run = true;
   }
   try
   {
      m_present_event_thread_control = std::make_shared<present_event_thread_control>();
      m_present_event_thread = std::thread(&swapchain::present_event_thread, this, m_present_event_thread_control);
   }
   catch (const std::system_error &)
   {
      m_present_event_thread_run = false;
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   catch (const std::bad_alloc &)
   {
      m_present_event_thread_run = false;
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (m_use_dri3)
   {
      static const char xwayland[] = "XWAYLAND";
      xcb_query_extension_reply_t *reply = xcb_query_extension_reply(
         m_connection, xcb_query_extension(m_connection, sizeof(xwayland) - 1, xwayland), nullptr);
      m_is_xwayland = reply != nullptr && reply->present;
      free(reply);
   }

   /* Every present mode needs the presentation thread: it waits for rendering to finish before an image
    * is handed to the X server, which has no other way to know (the image factory's
    * wait_on_present_fence), and without the thread the base would present without waiting at all. */
   use_presentation_thread = true;

   return VK_SUCCESS;
}

VkResult swapchain::init_image_factory(const VkSwapchainCreateInfoKHR &swapchain_create_info)
{
   auto image_handle_creator = m_allocator.make_unique<vulkan_image_handle_creator>(m_allocator, swapchain_create_info);
   if (image_handle_creator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (m_use_dri3)
   {
      /* DRI3 presents through the X server, so the importable format/modifier set comes from the
       * server (DRI3 1.2), not from KMS - this works regardless of the DRM topology (e.g. CIX, where
       * the display connector is not on card0). Mirrors how the Wayland backend sources formats from
       * the compositor. */
      uint32_t width = 0;
      uint32_t height = 0;
      int depth = 24;
      m_wsi_surface->get_size_and_depth(&width, &height, &depth);
      const uint8_t bpp = static_cast<uint8_t>((depth == 24) ? 32 : depth);
      const uint32_t fourcc = util::drm::vk_to_drm_format(swapchain_create_info.imageFormat);

      util::vector<util::drm::drm_format_pair> supported_formats{ m_allocator };
      query_dri3_supported_formats(m_connection, m_window, fourcc, static_cast<uint8_t>(depth), bpp, supported_formats);
      if (supported_formats.size() == 0)
      {
         WSI_LOG_ERROR("No DRI3 formats available for image allocation");
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      auto compression_control = image_create_compression_control::create(m_device, &swapchain_create_info);
      auto sc_img_create_ext_mem_result = swapchain_image_create_external_memory::create(
         image_handle_creator->get_image_create_info(), compression_control, *m_wsi_allocator, supported_formats,
         m_device_data.physical_device, m_allocator);
      if (auto error = std::get_if<VkResult>(&sc_img_create_ext_mem_result))
      {
         return *error;
      }
      auto sc_img_create_ext_mem =
         std::get<util::unique_ptr<swapchain_image_create_external_memory>>(std::move(sc_img_create_ext_mem_result));

      auto external_image_create_info = sc_img_create_ext_mem->get_external_image_create_info();
      TRY_LOG_CALL(image_handle_creator->add_extension(std::move(sc_img_create_ext_mem)));

      wsialloc_create_info_args wsialloc_args = { external_image_create_info.selected_format,
                                                  external_image_create_info.flags, external_image_create_info.extent,
                                                  external_image_create_info.explicit_compression };
      auto backing_memory_creator =
         m_allocator.make_unique<external_image_backing_memory_creator>(m_device_data, *m_wsi_allocator, wsialloc_args);
      if (backing_memory_creator == nullptr)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      /* X11 DRI3 is implicit-sync (no surface sync interface): non-exportable fence, CPU-wait the
       * present fence so the server never samples a half-rendered buffer. */
      m_image_factory.init(std::move(image_handle_creator), std::move(backing_memory_creator), false, true);
      return VK_SUCCESS;
   }

   /* SHM: host-visible, linearly-tiled images the CPU reads. */
   auto linear_ext = m_allocator.make_unique<linear_tiling_extension>();
   if (linear_ext == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   TRY_LOG_CALL(image_handle_creator->add_extension(std::move(linear_ext)));

   auto backing_memory_creator = m_allocator.make_unique<device_backing_memory_creator>(m_device_data);
   if (backing_memory_creator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* exportable_fence=false, wait_on_present_fence=true: the X11 SHM path is implicit-sync. The base
    * CPU-waits on the present fence before present_image, so the rendered image is ready to copy. The
    * real (host-visible) binding is done in allocate_and_bind_swapchain_image; the device backing
    * memory created here is unused. */
   m_image_factory.init(std::move(image_handle_creator), std::move(backing_memory_creator), false, true);
   return VK_SUCCESS;
}

swapchain_image_factory &swapchain::get_image_factory()
{
   return m_image_factory;
}

VkResult swapchain::allocate_and_bind_swapchain_image(swapchain_image &image)
{
   auto image_data_ptr = m_allocator.make_unique<x11_image_data>(m_device, m_allocator, m_connection);
   if (image_data_ptr == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   x11_image_data *image_data = image_data_ptr.get();
   image_data->device_data = &m_device_data;

   const auto image_create_info = get_image_factory().get_image_handle_creator().get_image_create_info();

   uint32_t width = image_create_info.extent.width;
   uint32_t height = image_create_info.extent.height;
   int depth = 24;
   uint32_t surface_width = 0;
   uint32_t surface_height = 0;
   if (!m_wsi_surface->get_size_and_depth(&surface_width, &surface_height, &depth))
   {
      WSI_LOG_WARNING("Could not get surface depth, using default: %d", depth);
   }

   if (m_use_dri3)
   {
      /* DRI3: allocate the GPU-local dma-buf, wrap it as an X pixmap (before the Vulkan import, so
       * the fds are still ours to duplicate), then import and bind. */
      auto &backing = swapchain_image_factory::get_backing_memory_from_image<image_backing_memory_external>(image);
      TRY_LOG_CALL(backing.allocate());
      TRY_LOG(m_presenter->create_image_resources(image, image_data, width, height, depth),
              "Failed to create DRI3 pixmap resources");
      TRY_LOG_CALL(backing.import_and_bind(image.get_image()));
   }
   else
   {
      /* SHM: allocate host-visible memory, bind it to the (linear) image, set up the SHM segments. */
      const VkMemoryPropertyFlags optimal = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      TRY_LOG_CALL(image_data->external_mem.configure_for_host_visible(image_create_info, required, optimal));
      TRY_LOG_CALL(image_data->external_mem.allocate_and_bind_image(image.get_image(), image_create_info));
      TRY_LOG(m_presenter->create_image_resources(image, image_data, width, height, depth),
              "Failed to create SHM image resources");
   }

   image.set_data(std::move(image_data_ptr));
   image.set_status(swapchain_image::FREE);
   return VK_SUCCESS;
}

bool swapchain::dri3_present_events_due()
{
   /* Before the first present there are no events to wait for, and the image vector may still be growing. */
   if (!m_images_ready)
   {
      return false;
   }

   for (auto &image : m_swapchain_images)
   {
      auto *data = image.get_data<x11_image_data>();
      if (data != nullptr && (data->awaiting_idle || data->awaiting_complete))
      {
         return true;
      }
   }
   return false;
}

void swapchain::handle_dri3_present_event(xcb_generic_event_t *event)
{
   /* Every swapchain that selected Present input on the window receives the events for all of its presents
    * (e.g. a retired swapchain's), so only clear the due flags of events that match one of our presents:
    * by pixmap, whose XID is unique, or by serial. */
   auto *generic = reinterpret_cast<xcb_present_generic_event_t *>(event);
   if (generic->evtype == XCB_PRESENT_IDLE_NOTIFY)
   {
      auto *idle = reinterpret_cast<xcb_present_idle_notify_event_t *>(event);
      if (m_images_ready)
      {
         for (auto &image : m_swapchain_images)
         {
            auto *data = image.get_data<x11_image_data>();
            if (data != nullptr && data->pixmap == idle->pixmap)
            {
               data->awaiting_idle = false;
            }
         }
      }

      /* The server is done reading the pixmap, so the image can be recycled: hand it to free_image_found
       * via the free-buffer pool. The unpaced GPU-copy cell recycles through the fixed deferred-release
       * pipeline in present_image instead. */
      if (!m_dri3_deferred_release)
      {
         if (!m_free_buffer_pool.push_back(idle->pixmap))
         {
            WSI_LOG_ERROR("DRI3: free buffer pool full, dropping idle pixmap");
         }
         m_thread_status_cond.notify_all();
      }
   }
   else if (generic->evtype == XCB_PRESENT_COMPLETE_NOTIFY)
   {
      auto *complete = reinterpret_cast<xcb_present_complete_notify_event_t *>(event);
      if (complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP && m_images_ready)
      {
         for (auto &image : m_swapchain_images)
         {
            auto *data = image.get_data<x11_image_data>();
            if (data != nullptr && data->awaiting_complete && data->present_serial == complete->serial)
            {
               data->awaiting_complete = false;
            }
         }
      }

      /* Track the vsync count so present_image can pace the next frame (FIFO target_msc). */
      m_last_present_msc = complete->msc;

      if (complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP && m_unpaced_serial_in_flight.has_value() &&
          *m_unpaced_serial_in_flight == complete->serial)
      {
         m_unpaced_serial_in_flight.reset();
         if (m_held_present.has_value())
         {
            const pending_present_request held = *m_held_present;
            m_held_present.reset();
            send_present(held, 0);
         }
      }
      m_thread_status_cond.notify_all();
   }
}

bool swapchain::wake_present_event_thread(bool &selected_on_root)
{
   selected_on_root = false;

   /* A PresentNotifyMSC for MSC 0 completes at once, with a PresentCompleteNotify on our event queue. The
    * requests here are checked so that their errors are reported here, not in the application's event
    * queue. */
   xcb_generic_error_t *error =
      xcb_request_check(m_connection, xcb_present_notify_msc_checked(m_connection, m_window, 0, 0, 0, 0));
   if (error == nullptr)
   {
      return true;
   }
   free(error);

   /* The application destroyed the window first (zink does, for one), and the server freed the Present
    * event context our thread waits on together with it. Its ID is ours to reuse: select it on the root
    * window and notify there, and the event reaches the same queue. */
   const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
   const uint32_t event_id = static_cast<dri3_presenter *>(m_presenter.get())->get_present_event_id();
   error = xcb_request_check(m_connection, xcb_present_select_input_checked(m_connection, event_id, root,
                                                                            XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY));
   if (error == nullptr)
   {
      selected_on_root = true;
      error = xcb_request_check(m_connection, xcb_present_notify_msc_checked(m_connection, root, 0, 0, 0, 0));
   }
   if (error != nullptr)
   {
      free(error);
      return false;
   }
   return true;
}

void swapchain::present_event_thread(std::shared_ptr<present_event_thread_control> control)
{
   auto control_lock = std::unique_lock<std::mutex>(control->lock);
   if (control->abandoned)
   {
      return;
   }
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   if (m_use_dri3)
   {
      xcb_connection_t *connection = m_connection;
      xcb_special_event_t *special_event = m_present_special_event;

      while (m_present_event_thread_run)
      {
         /* Block for a Present event only while one is due - one always arrives then, as long as the window
          * exists - and otherwise sleep until the next present, so an idle swapchain costs no wakeups.
          * xcb_wait_for_special_event returns as soon as the event is queued, whichever thread read it from
          * the connection. */
         if (!dri3_present_events_due())
         {
            m_thread_status_cond.wait(thread_status_lock);
            continue;
         }

         thread_status_lock.unlock();
         control_lock.unlock();
         xcb_generic_event_t *event = xcb_wait_for_special_event(connection, special_event);
         control_lock.lock();
         if (control->abandoned)
         {
            free(event);
            return;
         }
         thread_status_lock.lock();

         if (event == nullptr)
         {
            /* The connection has failed. */
            break;
         }

         handle_dri3_present_event(event);
         free(event);
      }

      m_present_event_thread_run = false;
      m_present_event_thread_exited = true;
      m_thread_status_cond.notify_all();
      return;
   }

   while (m_present_event_thread_run)
   {
      auto assume_forward_progress = false;

      /* This thread is started from init_platform(), while swapchain_base::init() still has to create
       * the images - it grows m_swapchain_images without holding m_thread_status_lock, so walking the
       * vector now would race a reallocation. m_images_ready is set by the first present_image(),
       * which cannot run before init() has returned, and pending completions only exist after one. */
      if (m_images_ready)
      {
         for (auto &image : m_swapchain_images)
         {
            if (image.get_status() == swapchain_image::UNALLOCATED)
            {
               continue;
            }

            auto data = image.get_data<x11_image_data>();
            if (data != nullptr && data->pending_completions.size() != 0)
            {
               assume_forward_progress = true;
               break;
            }
         }
      }

      if (!assume_forward_progress)
      {
         m_thread_status_cond.wait(thread_status_lock);
         continue;
      }

      if (error_has_occured())
      {
         break;
      }

      thread_status_lock.unlock();

      thread_status_lock.lock();
      std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Short polling interval
   }

   m_present_event_thread_run = false;
   m_present_event_thread_exited = true;
   m_thread_status_cond.notify_all();
}

void swapchain::present_image(const pending_present_request &pending_present)
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   /* swapchain_base::init() has returned by the time an image can be presented, so m_swapchain_images
    * is now stable and the present event thread may walk it. */
   m_images_ready = true;

   const bool paced =
      (m_present_mode == VK_PRESENT_MODE_FIFO_KHR || m_present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);

   if (m_use_dri3 && !m_dri3_deferred_release && !paced)
   {
      /* MAILBOX/IMMEDIATE: keep at most one present in flight and replace the image waiting behind it with
       * each newer one, releasing the replaced image straight back to the application; the event thread
       * sends the waiting image when the present in flight completes. The server then gets one present
       * per refresh, always the newest image, instead of every frame the application renders - Xwayland
       * falls behind the compositor when flooded with presents it can only discard. */
      if (m_unpaced_serial_in_flight.has_value() && m_present_event_thread_run)
      {
         if (m_held_present.has_value())
         {
            /* get_free_buffer waits on m_thread_status_cond, not on the base's semaphore. */
            unpresent_image(m_held_present->image_index);
            m_thread_status_cond.notify_all();
         }
         m_held_present = pending_present;
         return;
      }
      send_present(pending_present, 0);
      return;
   }

   /* A switch to a paced present mode supersedes an image still waiting from an unpaced one. */
   if (m_held_present.has_value())
   {
      unpresent_image(m_held_present->image_index);
      m_held_present.reset();
      m_thread_status_cond.notify_all();
   }

   /* FIFO: schedule each frame one vsync past the last completed present (strictly increasing) so the
    * server paces presents to the display refresh instead of releasing them in bursts. Other present
    * modes present as soon as possible (target_msc 0). If no Complete events arrive, m_last_present_msc
    * stays 0 and the targets fall in the past, degrading gracefully to as-soon-as-possible. */
   uint64_t target_msc = 0;
   if (paced)
   {
      m_target_msc = m_target_msc + 1;
      if (m_last_present_msc + 1 > m_target_msc)
      {
         m_target_msc = m_last_present_msc + 1;
      }
      target_msc = m_target_msc;
   }

   send_present(pending_present, target_msc);

   if (!m_use_dri3)
   {
      /* SHM completes synchronously inside present_image, so the image is free to reuse now. DRI3
       * leaves it presented until its PresentIdleNotify recycles it (present_event_thread ->
       * m_free_buffer_pool -> free_image_found). */
      thread_status_lock.unlock();
      unpresent_image(pending_present.image_index);
   }
}

void swapchain::send_present(const pending_present_request &pending_present, uint64_t target_msc)
{
   auto image_data = m_swapchain_images[pending_present.image_index].get_data<x11_image_data>();

   m_send_sbc++;
   uint32_t serial = static_cast<uint32_t>(m_send_sbc);

   const bool paced =
      (m_present_mode == VK_PRESENT_MODE_FIFO_KHR || m_present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);

   if (m_use_dri3)
   {
      /* PresentOptionAsync presents an image whose target MSC has already passed straight away instead of at
       * the next vblank. That is what IMMEDIATE and FIFO_RELAXED ask for, at the risk of tearing; on Xwayland
       * nothing can tear, which lets a MAILBOX image reach the compositor a refresh sooner too. */
      const bool async = m_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ||
                         m_present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR ||
                         (m_present_mode == VK_PRESENT_MODE_MAILBOX_KHR && m_is_xwayland);
      static_cast<dri3_presenter *>(m_presenter.get())->set_immediate_mode(async);

      image_data->awaiting_idle = true;
      image_data->awaiting_complete = true;
      image_data->present_serial = serial;
   }

   VkResult present_result = m_presenter->present_image(image_data, serial, target_msc);
   if (present_result == VK_SUCCESS && m_use_dri3 && !m_dri3_deferred_release && !paced)
   {
      m_unpaced_serial_in_flight = serial;
   }
   if (present_result != VK_SUCCESS)
   {
      image_data->awaiting_idle = false;
      image_data->awaiting_complete = false;
      WSI_LOG_ERROR("Failed to present image using presentation strategy: %d", present_result);
      /* A failed present never completes, and this runs on the presentation thread where the result
       * cannot be returned to the application. Fault the swapchain so that the next acquire/present
       * reports the error instead of the application waiting for a frame that will never arrive.
       * The presenters report a broken presentation channel as VK_ERROR_UNKNOWN, which acquire and
       * present may not return, so report it the way the other backends do. */
      set_error_state(present_result == VK_ERROR_DEVICE_LOST ? present_result : VK_ERROR_SURFACE_LOST_KHR);
   }

   /* Present ID (and so present wait) counts an image as delivered once it has been handed to the X
    * server; on failure set_error_state() above wakes anything waiting on it with the error instead. */
   auto *present_id_ext = get_swapchain_extension<wsi_ext_present_id>();
   if (present_result == VK_SUCCESS && present_id_ext != nullptr)
   {
      present_id_ext->mark_delivered(pending_present.present_id);
   }

   m_thread_status_cond.notify_all();

   if (m_dri3_deferred_release)
   {
      /* Unpaced GPU-copy: the server has copied the pixmap (OPTION_COPY), so recycle on a fixed
       * pipeline - free image N-DRI3_DEFER_FRAMES now - instead of waiting for PresentIdleNotify,
       * whose latency is bimodal (fast when the compositor composites, ~a frame when it flips). This
       * frees buffers immediately so the app runs ahead (MAILBOX). unpresent_image under the lock
       * matches free_image_found; notify so a waiting acquire re-checks. */
      int oldest = m_dri3_deferred[m_dri3_defer_head];
      m_dri3_deferred[m_dri3_defer_head] = static_cast<int>(pending_present.image_index);
      m_dri3_defer_head = (m_dri3_defer_head + 1) % DRI3_DEFER_FRAMES;
      if (oldest >= 0)
      {
         unpresent_image(static_cast<uint32_t>(oldest));
         m_thread_status_cond.notify_all();
      }
   }
}

bool swapchain::free_image_found()
{
   while (m_free_buffer_pool.size() > 0)
   {
      auto pixmap = m_free_buffer_pool.pop_front();
      assert(pixmap.has_value());
      for (size_t i = 0; i < m_swapchain_images.size(); i++)
      {
         auto data = m_swapchain_images[i].get_data<x11_image_data>();
         if (data != nullptr && data->pixmap == pixmap.value())
         {
            unpresent_image(static_cast<uint32_t>(i));
         }
      }
   }

   for (auto &img : m_swapchain_images)
   {
      if (img.get_status() == swapchain_image::FREE)
      {
         return true;
      }
   }
   return false;
}

VkResult swapchain::get_free_buffer(uint64_t *timeout)
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   if (*timeout == 0)
   {
      return free_image_found() ? VK_SUCCESS : VK_NOT_READY;
   }
   else if (*timeout == UINT64_MAX)
   {
      while (!free_image_found())
      {
         if (!m_present_event_thread_run)
         {
            return VK_ERROR_OUT_OF_DATE_KHR;
         }

         m_thread_status_cond.wait(thread_status_lock);
      }
   }
   else
   {
      auto time_point = std::chrono::high_resolution_clock::now() + std::chrono::nanoseconds(*timeout);

      while (!free_image_found())
      {
         if (!m_present_event_thread_run)
         {
            return VK_ERROR_OUT_OF_DATE_KHR;
         }

         if (m_thread_status_cond.wait_until(thread_status_lock, time_point) == std::cv_status::timeout)
         {
            return VK_TIMEOUT;
         }
      }
   }

   *timeout = 0;
   return VK_SUCCESS;
}

VkResult swapchain::add_required_extensions(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info)
{
   UNUSED(device);

   constexpr VkSwapchainCreateFlagsKHR present_wait2_mask =
      (VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR);
   const bool present_wait2 = (swapchain_create_info->flags & present_wait2_mask) == present_wait2_mask;
   const bool present_wait = m_device_data.is_present_wait_enabled() || present_wait2;

   /* Present wait waits on present IDs, so it needs the present ID extension as well - including when
    * the application enabled VK_KHR_present_wait without VK_KHR_present_id, which would otherwise
    * construct wsi_ext_present_wait_x11 from a null reference below, as the missing-extension assert
    * is compiled out of Release builds. (Testing present_wait2 here as well would be redundant: it
    * implies the present-ID-2 flag.) */
   if (m_device_data.is_present_id_enabled() || m_device_data.is_present_wait_enabled() ||
       (swapchain_create_info->flags & VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR))
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_present_id>()))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   if (present_wait)
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_present_wait_x11>(
             *get_swapchain_extension<wsi_ext_present_id>(true), present_wait2)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   /* The layer advertises VK_EXT/KHR_swapchain_maintenance1 and reports the feature as supported, so
    * the backend has to provide the extension: swapchain_base requires it when the application passes
    * VkSwapchainPresentModeInfoEXT, and it is what validates the present modes and scaling mode asked
    * for at creation. */
   if (m_device_data.is_swapchain_maintenance1_enabled())
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_swapchain_maintenance1>(m_allocator)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   return VK_SUCCESS;
}

} /* namespace x11 */
} /* namespace wsi */
