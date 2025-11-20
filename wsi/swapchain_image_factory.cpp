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
 * @file swapchain_image_factory.cpp
 *
 * @brief Contains the implementation for the swapchain image factory that
 * is used to create swapchain images.
 */

#include "swapchain_image_factory.hpp"
#include <util/helpers.hpp>

namespace wsi
{

swapchain_image_factory::swapchain_image_factory(util::allocator allocator, layer::device_private_data &device_data)
   : m_allocator(allocator)
   , m_device_data(device_data)
   , m_image_handle_creator(nullptr)
{
}

void swapchain_image_factory::init(util::unique_ptr<vulkan_image_handle_creator> image_handle_creator,
                                   util::unique_ptr<image_backing_memory_creator> image_memory_creator,
                                   bool exportable_fence, bool wait_on_present_fence)
{
   m_image_handle_creator = std::move(image_handle_creator);
   m_image_backing_memory_creator = std::move(image_memory_creator);
   m_exportable_fence = exportable_fence;
   m_wait_on_present_fence = wait_on_present_fence;
}

vulkan_image_handle_creator &swapchain_image_factory::get_image_handle_creator()
{
   return *m_image_handle_creator;
}

std::variant<VkResult, VkImage> swapchain_image_factory::create_image_handle()
{
   assert(m_image_handle_creator != nullptr);

   VkImage image_handle = VK_NULL_HANDLE;
   TRY_LOG_CALL(m_image_handle_creator->create_image(m_device_data, m_allocator, image_handle));

   return image_handle;
}

std::variant<VkResult, swapchain_image> swapchain_image_factory::create_swapchain_image()
{
   assert(m_image_backing_memory_creator != nullptr);

   util::unique_ptr<image_backing_memory> backing_memory =
      m_image_backing_memory_creator->create_image_backing_memory(m_allocator);
   if (backing_memory == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   std::variant<VkResult, VkImage> handle_result = create_image_handle();
   if (auto error = std::get_if<VkResult>(&handle_result))
   {
      return *error;
   }

   VkImage image_handle = std::get<VkImage>(handle_result);
   swapchain_image::create_args args{ &m_device_data,     m_allocator,
                                      image_handle,       std::move(backing_memory),
                                      m_exportable_fence, m_wait_on_present_fence };

   auto swapchain_image = swapchain_image::create(args);
   if (auto error = std::get_if<VkResult>(&swapchain_image))
   {
      m_device_data.disp.DestroyImage(m_device_data.device, image_handle, m_allocator.get_original_callbacks());
      return *error;
   }
   return swapchain_image;
}

} /* namespace wsi */