/*
 * Copyright (c) 2017-2025 Arm Limited.
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
 * @brief Contains the implementation for a headless swapchain.
 */

#include <cassert>
#include <cstdlib>

#include "swapchain.hpp"

#include <util/custom_allocator.hpp>
#include <util/timed_semaphore.hpp>
#include <util/macros.hpp>

#include <wsi/extensions/present_id.hpp>
#include <wsi/extensions/swapchain_maintenance.hpp>
#include <wsi/extensions/image_compression_control.hpp>
#include <wsi/extensions/mutable_format_extension.hpp>
#include <wsi/image_backing_memory_device.hpp>

#include "present_timing_handler.hpp"
#include "present_wait_headless.hpp"

namespace wsi
{
namespace headless
{

struct image_data
{
   /* Device memory backing the image. */
   VkDeviceMemory memory{};
   fence_sync present_fence;
};

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator)
   : wsi::swapchain_base(dev_data, pAllocator)
   , m_image_factory(m_allocator, m_device_data)
{
}

swapchain::~swapchain()
{
   /* Call the base's teardown */
   teardown();
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

   if (m_device_data.is_swapchain_maintenance1_enabled())
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_swapchain_maintenance1>(m_allocator)))
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

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   bool swapchain_support_enabled = swapchain_create_info->flags & VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
   if (swapchain_support_enabled)
   {
      if (!add_swapchain_extension(
             wsi_ext_present_timing_headless::create(m_allocator, device, swapchain_create_info->minImageCount)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
#endif

   bool present_wait2;
   constexpr VkSwapchainCreateFlagsKHR present_wait2_mask =
      (VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR);
   present_wait2 = (swapchain_create_info->flags & present_wait2_mask) == present_wait2_mask;

   if (m_device_data.is_present_wait_enabled() || present_wait2)
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_present_wait_headless>(
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
   if (swapchain_create_info->presentMode == VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR)
   {
      use_presentation_thread = false;
   }
   else
   {
      use_presentation_thread = true;
   }

   return init_image_factory(*swapchain_create_info);
}

VkResult swapchain::init_image_factory(const VkSwapchainCreateInfoKHR &swapchain_create_info)
{
   std::variant<VkResult, util::unique_ptr<vulkan_image_handle_creator>> image_handle_creator_result =
      create_image_creator(swapchain_create_info);
   if (auto error = std::get_if<VkResult>(&image_handle_creator_result))
   {
      return *error;
   }

   auto image_handle_creator =
      std::get<util::unique_ptr<vulkan_image_handle_creator>>(std::move(image_handle_creator_result));
   auto backing_memory_creator = m_allocator.make_unique<device_backing_memory_creator>(m_device_data);
   if (backing_memory_creator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   m_image_factory.init(std::move(image_handle_creator), std::move(backing_memory_creator), false, true);
   return VK_SUCCESS;
}

VkResult swapchain::allocate_and_bind_swapchain_image(swapchain_image &image)
{
   const util::unique_lock<util::recursive_mutex> lock(m_image_status_mutex);
   if (!lock)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto &backing_memory = swapchain_image_factory::get_backing_memory_from_image<image_backing_memory_device>(image);
   return backing_memory.allocate_and_bind(image.get_image());
}

void swapchain::present_image(const pending_present_request &pending_present)
{
#if VULKAN_WSI_LAYER_EXPERIMENTAL
   auto *ext_present_timing = get_swapchain_extension<wsi_ext_present_timing_headless>();
   if (ext_present_timing)
   {
      auto presentation_target = ext_present_timing->get_presentation_target_entry(pending_present.image_index);
      uint64_t absolute_future_present_time_ns = 0;
      if (presentation_target)
      {
         if (presentation_target->m_flags & VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT)
         {
            std::optional<uint64_t> first_pixel_visible_timestamp_for_last_image =
               ext_present_timing->get_first_pixel_visible_timestamp_for_last_image();

            if (first_pixel_visible_timestamp_for_last_image.has_value())
            {
               absolute_future_present_time_ns =
                  first_pixel_visible_timestamp_for_last_image.value() + presentation_target->m_target_present_time;
            }
         }
         else
         {
            /* No need to check whether we need to present at nearest refresh cycle since this backend is not
               limited by the refresh cycles. */
            absolute_future_present_time_ns = presentation_target->m_target_present_time;
         }
         auto current_time_ns = ext_present_timing->get_current_clock_time_ns();
         if (*current_time_ns < absolute_future_present_time_ns)
         {
            /* Sleep until we can schedule the image for completion.
             * This is OK as the sleep should only be dispatched on the page_flip thread and not on main. */
            assert(m_page_flip_thread_run);

            int64_t time_diff = absolute_future_present_time_ns - *current_time_ns;
            std::this_thread::sleep_for(std::chrono::nanoseconds(time_diff));
         }
      }

      ext_present_timing->remove_presentation_target_entry(pending_present.image_index);
   }
#endif

   auto *ext = get_swapchain_extension<wsi_ext_present_id>();
   if (ext != nullptr)
   {
      ext->mark_delivered(pending_present.present_id);
   }

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   if (ext_present_timing && ext_present_timing->get_monotonic_domain().has_value())
   {
      auto current_time = ext_present_timing->get_current_clock_time_ns();
      if (!current_time.has_value())
      {
         /* Set all times to 0 as we were not able to query them. */
         current_time = 0;
      }
      ext_present_timing->set_first_pixel_visible_timestamp_for_last_image(*current_time);

      VkPresentStageFlagBitsEXT stages[] = {
         VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT,
         VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT,
         VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT,
      };
      for (auto stage : stages)
      {
         ext_present_timing->set_pending_stage_time(pending_present.image_index, stage, *current_time);
      }
   }
#endif

   unpresent_image(pending_present.image_index);
}

std::variant<VkResult, util::unique_ptr<vulkan_image_handle_creator>> swapchain::create_image_creator(
   const VkSwapchainCreateInfoKHR &swapchain_create_info)
{
   auto image_handle_creator = m_allocator.make_unique<vulkan_image_handle_creator>(m_allocator, swapchain_create_info);
   if (image_handle_creator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   auto compression_control = image_create_compression_control::create(m_device, &swapchain_create_info);
   if (compression_control.has_value())
   {
      auto sc_compresson_control =
         m_allocator.make_unique<image_create_compression_control>(compression_control.value());
      if (sc_compresson_control == nullptr)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      TRY_LOG_CALL(image_handle_creator->add_extension(std::move(sc_compresson_control)));
   }

   if (is_mutable_format_enabled())
   {
      const auto *image_format_list = util::find_extension<VkImageFormatListCreateInfo>(
         VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, swapchain_create_info.pNext);
      auto mutable_format_uptr = swapchain_image_create_mutable_format::create_unique(image_format_list, m_allocator);
      if (!mutable_format_uptr)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      TRY_LOG_CALL(image_handle_creator->add_extension(std::move(mutable_format_uptr)));
   }

   return image_handle_creator;
}

swapchain_image_factory &swapchain::get_image_factory()
{
   return m_image_factory;
}

} /* namespace headless */
} /* namespace wsi */
