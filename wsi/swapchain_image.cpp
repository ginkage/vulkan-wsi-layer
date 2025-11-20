/*
 * Copyright (c) 2025 Arm Limited.
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
 * @file
 *
 * @brief Contains the implementation for swapchain images.
 */

#include "swapchain_image.hpp"

namespace wsi
{

std::variant<VkResult, swapchain_image> swapchain_image::create(create_args &create_args)
{
   VkSemaphoreCreateInfo semaphore_info = {};
   semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

   VkDevice device = create_args.m_device_data->device;
   auto *device_data = create_args.m_device_data;

   VkSemaphore present_semaphore = VK_NULL_HANDLE;
   TRY_LOG_CALL(device_data->disp.CreateSemaphore(
      device, &semaphore_info, create_args.m_allocator.get_original_callbacks(), &present_semaphore));

   VkSemaphore present_fence_wait = VK_NULL_HANDLE;
   VkResult result = device_data->disp.CreateSemaphore(
      device, &semaphore_info, create_args.m_allocator.get_original_callbacks(), &present_fence_wait);
   if (result != VK_SUCCESS)
   {
      device_data->disp.DestroySemaphore(device, present_semaphore, create_args.m_allocator.get_original_callbacks());
      return result;
   }

   util::unique_ptr<fence_sync> present_fence;
   if (create_args.m_exportable_fence)
   {
      auto present_fence_opt = sync_fd_fence_sync::create(*device_data);
      if (!present_fence_opt.has_value())
      {
         device_data->disp.DestroySemaphore(device, present_semaphore,
                                            create_args.m_allocator.get_original_callbacks());
         device_data->disp.DestroySemaphore(device, present_fence_wait,
                                            create_args.m_allocator.get_original_callbacks());
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      present_fence = create_args.m_allocator.make_unique<sync_fd_fence_sync>(std::move(present_fence_opt.value()));
   }
   else
   {
      auto present_fence_opt = fence_sync::create(*device_data);
      if (!present_fence_opt.has_value())
      {
         device_data->disp.DestroySemaphore(device, present_semaphore,
                                            create_args.m_allocator.get_original_callbacks());
         device_data->disp.DestroySemaphore(device, present_fence_wait,
                                            create_args.m_allocator.get_original_callbacks());
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      present_fence = create_args.m_allocator.make_unique<fence_sync>(std::move(present_fence_opt.value()));
   }

   if (present_fence == nullptr)
   {
      device_data->disp.DestroySemaphore(device, present_semaphore, create_args.m_allocator.get_original_callbacks());
      device_data->disp.DestroySemaphore(device, present_fence_wait, create_args.m_allocator.get_original_callbacks());
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return swapchain_image(create_args.m_image_handle, present_semaphore, present_fence_wait, std::move(present_fence),
                          create_args.m_wait_on_present_fence, create_args.m_device_data, create_args.m_allocator,
                          std::move(create_args.m_backing_memory));
}

void swapchain_image::destroy()
{
   /* Set UNALLOCATED state for debugging purposes in case there are any uses of image after it has been destroyed
    * as we don't have hold error state anymore. */
   set_status(swapchain_image::UNALLOCATED);

   if (m_present_semaphore != VK_NULL_HANDLE)
   {
      m_device_data->disp.DestroySemaphore(m_device_data->device, m_present_semaphore,
                                           m_allocator.get_original_callbacks());
      m_present_semaphore = VK_NULL_HANDLE;
   }

   if (m_present_fence_wait_semaphore != VK_NULL_HANDLE)
   {
      m_device_data->disp.DestroySemaphore(m_device_data->device, m_present_fence_wait_semaphore,
                                           m_allocator.get_original_callbacks());
      m_present_fence_wait_semaphore = VK_NULL_HANDLE;
   }

   if (m_image != VK_NULL_HANDLE)
   {
      m_device_data->disp.DestroyImage(m_device_data->device, m_image, m_allocator.get_original_callbacks());
      m_image = VK_NULL_HANDLE;
   }

   if (m_present_fence)
   {
      m_present_fence.reset();
   }

   if (m_image_memory)
   {
      m_image_memory.reset();
   }

   if (m_data)
   {
      m_data.reset();
   }
}

VkResult swapchain_image::bind(const VkBindImageMemoryInfo *bind_image_mem_info)
{
   return m_image_memory->bind(bind_image_mem_info);
}

VkResult swapchain_image::set_present_payload(VkQueue queue, const queue_submit_semaphores &semaphores,
                                              const void *submission_pnext)
{
   return m_present_fence->set_payload(queue, semaphores, submission_pnext);
}

VkResult swapchain_image::wait_present(uint64_t timeout_ns)
{
   if (m_wait_on_present_fence)
   {
      return m_present_fence->wait_payload(timeout_ns);
   }

   return VK_SUCCESS;
}
}