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

#include <wsi/extensions/present_id.hpp>
#include <wsi/extensions/swapchain_maintenance.hpp>
#include "util/macros.hpp"

#include "present_timing_handler.hpp"
#include "present_wait_headless.hpp"

#include <wsi/swapchain_image_create_extensions/image_compression_control.hpp>

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
             wsi_ext_present_timing_headless::create(device, m_allocator, swapchain_create_info->minImageCount)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
#endif

   bool present_wait2;
#if VULKAN_WSI_LAYER_EXPERIMENTAL
   constexpr VkSwapchainCreateFlagsKHR present_wait2_mask =
      (VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR);
   present_wait2 = (swapchain_create_info->flags & present_wait2_mask) == present_wait2_mask;
#else
   present_wait2 = false;
#endif

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

   return VK_SUCCESS;
}

VkResult swapchain::allocate_and_bind_swapchain_image(VkImageCreateInfo image_create, swapchain_image &image)
{
   UNUSED(image_create);
   VkResult res = VK_SUCCESS;
   const util::unique_lock<util::recursive_mutex> lock(m_image_status_mutex);
   if (!lock)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   VkMemoryRequirements memory_requirements = {};
   m_device_data.disp.GetImageMemoryRequirements(m_device, image.image, &memory_requirements);

   /* Find a memory type */
   size_t mem_type_idx = 0;
   for (; mem_type_idx < 8 * sizeof(memory_requirements.memoryTypeBits); ++mem_type_idx)
   {
      if (memory_requirements.memoryTypeBits & (1u << mem_type_idx))
      {
         break;
      }
   }

   assert(mem_type_idx <= 8 * sizeof(memory_requirements.memoryTypeBits) - 1);

   VkMemoryAllocateInfo mem_info = {};
   mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mem_info.allocationSize = memory_requirements.size;
   mem_info.memoryTypeIndex = mem_type_idx;
   image_data *data = nullptr;

   /* Create image_data */
   data = m_allocator.create<image_data>(1);
   if (data == nullptr)
   {
      m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   image.data = reinterpret_cast<void *>(data);
   image.status = wsi::swapchain_image::FREE;

   res = m_device_data.disp.AllocateMemory(m_device, &mem_info, get_allocation_callbacks(), &data->memory);
   assert(VK_SUCCESS == res);
   if (res != VK_SUCCESS)
   {
      destroy_image(image);
      return res;
   }

   res = m_device_data.disp.BindImageMemory(m_device, image.image, data->memory, 0);
   assert(VK_SUCCESS == res);
   if (res != VK_SUCCESS)
   {
      destroy_image(image);
      return res;
   }

   /* Initialize presentation fence. */
   auto present_fence = fence_sync::create(m_device_data);
   if (!present_fence.has_value())
   {
      destroy_image(image);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   data->present_fence = std::move(present_fence.value());

   return res;
}

VkResult swapchain::create_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image)
{
   return m_device_data.disp.CreateImage(m_device, &image_create_info, get_allocation_callbacks(), &image.image);
}

void swapchain::present_image(const pending_present_request &pending_present)
{
#if VULKAN_WSI_LAYER_EXPERIMENTAL
   auto *ext_present_timing = get_swapchain_extension<wsi_ext_present_timing_headless>();
   if (ext_present_timing)
   {
      auto presentation_target = ext_present_timing->get_presentation_target_entry(pending_present.image_index);
      if (presentation_target)
      {
         /* No support for relative presentation mode currently */
         assert(!(presentation_target->m_flags & VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT));
         if (!(presentation_target->m_flags & VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT))
         {
            /* No need to check whether we need to present at nearest refresh cycle since this backend is not
               limited by the refresh cycles. */
            uint64_t absolute_future_present_time_ns = presentation_target->m_target_present_time;
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

void swapchain::destroy_image(wsi::swapchain_image &image)
{
   util::unique_lock<util::recursive_mutex> image_status_lock(m_image_status_mutex);
   if (!image_status_lock)
   {
      WSI_LOG_ERROR("Failed to acquire image status lock in destroy_image.");
      abort();
   }
   if (image.status != wsi::swapchain_image::INVALID)
   {
      if (image.image != VK_NULL_HANDLE)
      {
         m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
         image.image = VK_NULL_HANDLE;
      }

      image.status = wsi::swapchain_image::INVALID;
   }

   image_status_lock.unlock();

   if (image.data != nullptr)
   {
      auto *data = reinterpret_cast<image_data *>(image.data);
      if (data->memory != VK_NULL_HANDLE)
      {
         m_device_data.disp.FreeMemory(m_device, data->memory, get_allocation_callbacks());
         data->memory = VK_NULL_HANDLE;
      }
      m_allocator.destroy(1, data);
      image.data = nullptr;
   }
}

VkResult swapchain::image_set_present_payload(swapchain_image &image, VkQueue queue,
                                              const queue_submit_semaphores &semaphores, const void *submission_pnext)
{
   auto data = reinterpret_cast<image_data *>(image.data);
   return data->present_fence.set_payload(queue, semaphores, submission_pnext);
}

VkResult swapchain::image_wait_present(swapchain_image &image, uint64_t timeout)
{
   auto data = reinterpret_cast<image_data *>(image.data);
   return data->present_fence.wait_payload(timeout);
}

VkResult swapchain::bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                         const VkBindImageMemorySwapchainInfoKHR *bind_sc_info)
{
   auto &device_data = layer::device_private_data::get(device);

   const wsi::swapchain_image &swapchain_image = m_swapchain_images[bind_sc_info->imageIndex];
   VkDeviceMemory memory = reinterpret_cast<image_data *>(swapchain_image.data)->memory;

   return device_data.disp.BindImageMemory(device, bind_image_mem_info->image, memory, 0);
}

VkResult swapchain::get_required_image_creator_extensions(
   const VkSwapchainCreateInfoKHR &swapchain_create_info,
   util::vector<util::unique_ptr<swapchain_image_create_info_extension>> *extensions)
{
   assert(extensions != nullptr);

   auto compression_control = swapchain_image_create_compression_control::create(
      m_device_data.is_swapchain_compression_control_enabled(), swapchain_create_info);
   if (compression_control)
   {
      if (!extensions->try_push_back(
             m_allocator.make_unique<swapchain_image_create_compression_control>(*compression_control)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   return VK_SUCCESS;
}

} /* namespace headless */
} /* namespace wsi */
