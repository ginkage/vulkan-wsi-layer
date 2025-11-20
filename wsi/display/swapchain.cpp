/*
 * Copyright (c) 2024-2025 Arm Limited.
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
 * @brief Contains the class implementation for a display swapchain.
 */

#include <errno.h>

#include <util/macros.hpp>
#include <wsi/extensions/external_memory_extension.hpp>
#include <wsi/extensions/image_compression_control.hpp>
#include <wsi/extensions/present_id.hpp>
#include <wsi/swapchain_base.hpp>

#include "swapchain.hpp"
#include "present_wait_display.hpp"

namespace wsi
{

namespace display
{

class display_image_data : public swapchain_image_data
{
public:
   display_image_data(int drm_fd, uint32_t fb_id)
      : m_drm_fd(drm_fd)
      , m_fb_id(fb_id)
   {
      assert(drm_fd != -1);
   }

   ~display_image_data()
   {
      int result = drmModeRmFB(m_drm_fd, m_fb_id);
      assert(result == 0);
      UNUSED(result);
   }

   uint32_t get_fb_id()
   {
      return m_fb_id;
   }

private:
   uint32_t m_drm_fd;
   uint32_t m_fb_id;
};

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator,
                     surface &wsi_surface)
   : wsi::swapchain_base(dev_data, pAllocator)
   , m_display_mode(wsi_surface.get_display_mode())
   , m_image_factory(m_allocator, m_device_data)
{
}

swapchain::~swapchain()
{
   /* Call the base class teardown */
   teardown();
}

static void page_flip_event(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data)
{
   UNUSED(fd);
   UNUSED(sequence);
   UNUSED(tv_sec);
   UNUSED(tv_usec);
   bool *done = (bool *)user_data;
   *done = true;
}

VkResult swapchain::add_required_extensions(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info)
{
   UNUSED(device);

   if (m_device_data.is_present_id_enabled() ||
       (swapchain_create_info->flags & VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR))
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_present_id>()))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   if (m_device_data.should_layer_handle_frame_boundary_events())
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_frame_boundary>(m_device_data)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   bool present_wait2;
   constexpr VkSwapchainCreateFlagsKHR present_wait2_mask =
      (VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR);
   present_wait2 = (swapchain_create_info->flags & present_wait2_mask) == present_wait2_mask;

   if (m_device_data.is_present_wait_enabled() || present_wait2)
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_present_wait_display>(
             *get_swapchain_extension<wsi_ext_present_id>(true), present_wait2)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   return VK_SUCCESS;
}

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread)
{
   UNUSED(device);
   UNUSED(swapchain_create_info);
   UNUSED(use_presentation_thread);

   auto wsi_allocator = swapchain_wsialloc_allocator::create();
   if (!wsi_allocator.has_value())
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   m_wsi_allocator = m_allocator.make_unique<swapchain_wsialloc_allocator>(std::move(*wsi_allocator));
   if (m_wsi_allocator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (is_mutable_format_enabled())
   {
      WSI_LOG_ERROR("Mutable format swapchain is not supported for display backend");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   return init_image_factory(*swapchain_create_info);
}

VkResult swapchain::init_image_factory(const VkSwapchainCreateInfoKHR &swapchain_create_info)
{
   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      WSI_LOG_ERROR("DRM display not available.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   std::variant<VkResult, util::unique_ptr<vulkan_image_handle_creator>> image_handle_creator_result =
      create_image_creator(swapchain_create_info);
   if (auto error = std::get_if<VkResult>(&image_handle_creator_result))
   {
      return *error;
   }

   auto image_handle_creator =
      std::get<util::unique_ptr<vulkan_image_handle_creator>>(std::move(image_handle_creator_result));

   auto compression_control = image_create_compression_control::create(m_device, &swapchain_create_info);
   auto sc_img_create_ext_mem_result = swapchain_image_create_external_memory::create(
      image_handle_creator->get_image_create_info(), compression_control, *m_wsi_allocator,
      *display->get_supported_formats(), m_device_data.physical_device, m_allocator);
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

   m_image_factory.init(std::move(image_handle_creator), std::move(backing_memory_creator), true, true);
   return VK_SUCCESS;
}

VkResult swapchain::create_framebuffer(image_backing_memory_external &image_external_memory, uint32_t &out_fb_id)
{
   VkResult ret_code = VK_SUCCESS;
   std::array<uint32_t, util::MAX_PLANES> strides{ 0, 0, 0, 0 };
   std::array<uint64_t, util::MAX_PLANES> modifiers{ 0, 0, 0, 0 };

   wsialloc_create_info_args img_create_info = image_external_memory.get_image_create_info();
   external_memory &ext_memory = image_external_memory.get_external_memory();

   const util::drm::drm_format_pair allocated_format{ img_create_info.selected_format.fourcc,
                                                      img_create_info.selected_format.modifier };

   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   drm_gem_handle_array<util::MAX_PLANES> buffer_handles{ display->get_drm_fd() };

   const auto &buffer_fds = ext_memory.get_buffer_fds();

   for (uint32_t plane = 0; plane < ext_memory.get_num_planes(); plane++)
   {
      assert(ext_memory.get_strides()[plane] > 0);
      strides[plane] = ext_memory.get_strides()[plane];
      modifiers[plane] = allocated_format.modifier;
      if (drmPrimeFDToHandle(display->get_drm_fd(), buffer_fds[plane], &buffer_handles[plane]) != 0)
      {
         WSI_LOG_ERROR("Failed to convert buffer FD to GEM handle: %s", std::strerror(errno));
         return VK_ERROR_INITIALIZATION_FAILED;
      }
   }

   if (!display->is_format_supported(allocated_format))
   {
      WSI_LOG_ERROR("Format not supported.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   int error = 0;
   if (display->supports_fb_modifiers())
   {
      error = drmModeAddFB2WithModifiers(display->get_drm_fd(), img_create_info.extent.width,
                                         img_create_info.extent.height, allocated_format.fourcc, buffer_handles.data(),
                                         strides.data(), ext_memory.get_offsets().data(), modifiers.data(), &out_fb_id,
                                         DRM_MODE_FB_MODIFIERS);
   }
   else
   {
      error = drmModeAddFB2(display->get_drm_fd(), img_create_info.extent.width, img_create_info.extent.height,
                            allocated_format.fourcc, buffer_handles.data(), strides.data(),
                            ext_memory.get_offsets().data(), &out_fb_id, 0);
   }

   if (error != 0)
   {
      WSI_LOG_ERROR("Failed to create framebuffer: %s", strerror(errno));
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   return ret_code;
}

VkResult swapchain::allocate_and_bind_swapchain_image(swapchain_image &image)
{
   util::unique_lock<util::recursive_mutex> image_status_lock(m_image_status_mutex);
   if (!image_status_lock)
   {
      WSI_LOG_ERROR("Failed to acquire image status lock in allocate_and_bind_swapchain_image.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto &backing_memory = swapchain_image_factory::get_backing_memory_from_image<image_backing_memory_external>(image);
   TRY_LOG_CALL(backing_memory.allocate());

   uint32_t fb_id = 0;
   TRY_LOG(create_framebuffer(backing_memory, fb_id), "Failed to create framebuffer");

   TRY_LOG_CALL(backing_memory.import_and_bind(image.get_image()));

   auto image_data = m_allocator.make_unique<display_image_data>(display->get_drm_fd(), fb_id);
   if (image_data == nullptr)
   {
      int result = drmModeRmFB(display->get_drm_fd(), fb_id);
      assert(result == 0);
      UNUSED(result);

      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   image.set_data(std::move(image_data));

   return VK_SUCCESS;
}

void swapchain::present_image(const pending_present_request &pending_present)
{

   auto &image = m_swapchain_images[pending_present.image_index];
   auto image_data = image.get_data<display_image_data>();

   const auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      set_error_state(VK_ERROR_SURFACE_LOST_KHR);
      return;
   }

   int drm_res = 0;
   if (m_first_present)
   {
      /* Now we can set the mode of the new swapchain. */
      drmModeModeInfo modeInfo = m_display_mode->get_drm_mode();

      uint32_t connector_id = display->get_connector_id();
      drm_res = drmModeSetCrtc(display->get_drm_fd(), display->get_crtc_id(), image_data->get_fb_id(), 0, 0,
                               &connector_id, 1, &modeInfo);

      if (drm_res != 0)
      {
         WSI_LOG_ERROR("drmModeSetCrtc failed: %s\n", std::strerror(errno));
         set_error_state(VK_ERROR_SURFACE_LOST_KHR);
         return;
      }
   }
   /* The swapchain has already started presenting. */
   else
   {

      bool page_flip_complete = false;

      drm_res = drmModePageFlip(display->get_drm_fd(), display->get_crtc_id(), image_data->get_fb_id(),
                                DRM_MODE_PAGE_FLIP_EVENT, (void *)&page_flip_complete);

      if (drm_res != 0)
      {
         WSI_LOG_ERROR("drmModePageFlip failed: %s\n", std::strerror(errno));
         set_error_state(VK_ERROR_SURFACE_LOST_KHR);
         return;
      }

      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(display->get_drm_fd(), &fds);

      do
      {
         struct timeval t;
         t.tv_sec = 1;
         t.tv_usec = 0;
         drm_res = select(display->get_drm_fd() + 1, &fds, NULL, NULL, &t);

         if (drm_res < 0)
         {
            if (errno != EINTR && errno != EAGAIN)
            {
               WSI_LOG_ERROR("select() failed with errno: %d\n", errno);
               set_error_state(VK_ERROR_SURFACE_LOST_KHR);
               break;
            }
            WSI_LOG_ERROR("select() failed with %d, carrying on with page flip\n", errno);
         }
         else if (drm_res == 0)
         {
            WSI_LOG_ERROR("select() timed out, carrying on with page flip\n");
         }
         else
         {
            int result = FD_ISSET(display->get_drm_fd(), &fds);
            assert(result > 0);
            UNUSED(result);
            drmEventContext ev = {};
            ev.version = DRM_EVENT_CONTEXT_VERSION;
            ev.page_flip_handler = page_flip_event;

            drmHandleEvent(display->get_drm_fd(), &ev);
         }
      } while ((drm_res == -1 && (errno == EINTR || errno == EAGAIN)) || drm_res == 0 || !page_flip_complete);
   }

   /* Find currently presented image */
   uint32_t presented_index = m_swapchain_images.size();
   if (!m_first_present)
   {
      for (uint32_t i = 0; i < m_swapchain_images.size(); ++i)
      {
         if (m_swapchain_images[i].get_status() == swapchain_image::PRESENTED)
         {
            presented_index = i;
            break;
         }
      }
      /* There should always be a presented image, unless there was an error */
      assert(presented_index < m_swapchain_images.size());
   }
   /* The image is on screen, change the image status to PRESENTED. */
   m_swapchain_images[pending_present.image_index].set_status(swapchain_image::PRESENTED);

   auto *ext = get_swapchain_extension<wsi_ext_present_id>();
   if (ext != nullptr)
   {
      ext->mark_delivered(pending_present.present_id);
   }

   /* And release the old one. */
   if (presented_index < m_swapchain_images.size())
   {
      unpresent_image(presented_index);
   }

   return;
}

std::variant<VkResult, util::unique_ptr<vulkan_image_handle_creator>> swapchain::create_image_creator(
   const VkSwapchainCreateInfoKHR &swapchain_create_info)
{
   UNUSED(swapchain_create_info);

   auto image_handle_creator = m_allocator.make_unique<vulkan_image_handle_creator>(m_allocator, swapchain_create_info);
   if (image_handle_creator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return image_handle_creator;
}

swapchain_image_factory &swapchain::get_image_factory()
{
   return m_image_factory;
}

} /* namespace display */

} /* namespace wsi*/
