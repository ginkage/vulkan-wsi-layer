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
#include <wsi/extensions/present_timing.hpp>
#include <wsi/extensions/swapchain_maintenance.hpp>
#include <wsi/extensions/image_compression_control.hpp>
#include "util/macros.hpp"

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
   auto compression_control = wsi_ext_image_compression_control::create(device, swapchain_create_info);
   if (compression_control)
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_image_compression_control>(*compression_control)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   if (m_device_data.is_present_id_enabled())
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
      if (!add_swapchain_extension(wsi_ext_present_timing_headless::create(m_allocator)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
#endif

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
   const std::lock_guard<std::recursive_mutex> lock(m_image_status_mutex);

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
   m_image_create_info = image_create_info;
   VkImageCompressionControlEXT image_compression_control = {};

   if (m_device_data.is_swapchain_compression_control_enabled())
   {
      auto *ext = get_swapchain_extension<wsi_ext_image_compression_control>();
      /* For image compression control, additional requirements to be satisfied such as
       * existence of VkImageCompressionControlEXT in swaphain_create_info for
       * the ext to be added to the list. So we check whether we got a valid pointer
       * and proceed if yes. */
      if (ext)
      {
         image_compression_control = ext->get_compression_control_properties();
         image_compression_control.pNext = m_image_create_info.pNext;
         m_image_create_info.pNext = &image_compression_control;
      }
   }
   return m_device_data.disp.CreateImage(m_device, &m_image_create_info, get_allocation_callbacks(), &image.image);
}

void swapchain::present_image(const pending_present_request &pending_present)
{
   if (m_device_data.is_present_id_enabled())
   {
      auto *ext = get_swapchain_extension<wsi_ext_present_id>(true);
      ext->set_present_id(pending_present.present_id);
   }
   unpresent_image(pending_present.image_index);
}

void swapchain::destroy_image(wsi::swapchain_image &image)
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);
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

} /* namespace headless */
} /* namespace wsi */
