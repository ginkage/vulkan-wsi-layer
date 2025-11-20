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
 * @file vulkan_image_handle_creator.cpp
 */

#include "vulkan_image_handle_creator.hpp"
#include "extensions/image_create_info_extension.hpp"
#include "util/helpers.hpp"

namespace wsi
{

vulkan_image_handle_creator::vulkan_image_handle_creator(util::allocator allocator,
                                                         const VkSwapchainCreateInfoKHR &swapchain_create_info)
   : m_image_create_info()
   , m_extensions(allocator)
{
   m_image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   m_image_create_info.pNext = nullptr;
   m_image_create_info.imageType = VK_IMAGE_TYPE_2D;
   m_image_create_info.format = swapchain_create_info.imageFormat;
   m_image_create_info.extent = { swapchain_create_info.imageExtent.width, swapchain_create_info.imageExtent.height,
                                  1 };
   m_image_create_info.mipLevels = 1;
   m_image_create_info.arrayLayers = swapchain_create_info.imageArrayLayers;
   m_image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
   m_image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
   m_image_create_info.usage = swapchain_create_info.imageUsage;
   m_image_create_info.flags = 0;
   m_image_create_info.sharingMode = swapchain_create_info.imageSharingMode;
   m_image_create_info.queueFamilyIndexCount = swapchain_create_info.queueFamilyIndexCount;
   m_image_create_info.pQueueFamilyIndices = swapchain_create_info.pQueueFamilyIndices;
   m_image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

VkResult vulkan_image_handle_creator::create_image(layer::device_private_data &device_data,
                                                   const util::allocator &allocator, VkImage &out_image_handle)
{
   return device_data.disp.CreateImage(device_data.device, &m_image_create_info, allocator.get_original_callbacks(),
                                       &out_image_handle);
}

VkResult vulkan_image_handle_creator::add_extension(util::unique_ptr<image_create_info_extension> extension)
{
   TRY(extension->extend_image_create_info(&m_image_create_info));
   return m_extensions.try_push_back(std::move(extension)) ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

} /* namespace wsi */