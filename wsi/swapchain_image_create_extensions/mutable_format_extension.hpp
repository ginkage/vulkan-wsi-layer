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
 * @file mutable_format_extension.hpp
 *
 * @brief Swapchain image create info extension for VK_KHR_swapchain_mutable_format handling.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <util/custom_allocator.hpp>
#include "swapchain_image_create_info_extension.hpp"

namespace wsi
{

class swapchain_image_create_mutable_format : public swapchain_image_create_info_extension
{
public:
   swapchain_image_create_mutable_format() = delete;
   swapchain_image_create_mutable_format(const swapchain_image_create_mutable_format &) = delete;
   swapchain_image_create_mutable_format &operator=(const swapchain_image_create_mutable_format &) = delete;
   /**
    * @brief Create and return a unique_ptr-managed instance using allocator.
    *
    * Allocates the object with the provided allocator and initializes its
    * internal state from the optional VkImageFormatListCreateInfo.
    *
    * @return util::unique_ptr owning the instance, or nullptr on failure.
    */
   static util::unique_ptr<swapchain_image_create_mutable_format> create_unique(
      const VkImageFormatListCreateInfo *image_format_list, util::allocator allocator);
   /**
    * @brief Extend image_create_info pNext with extension specific data.
    * A swapchain image create info extension will use this function to add its
    * extension specific data to pNext of image_create_info.
    * @param[in, out] image_create_info VkImageCreateInfo for creating swapchain images
    * @return VkResult indicating success or failure
    */
   VkResult extend_image_create_info(VkImageCreateInfo *image_create_info) override;

private:
   friend class util::allocator;
   /**
    * @brief Construct the extension.
    * @param allocator Allocator used to own copied view formats.
    */
   swapchain_image_create_mutable_format(util::allocator &allocator)
      : m_view_formats(allocator)
   {
   }

   util::vector<VkFormat> m_view_formats;
   VkImageFormatListCreateInfo m_format_list{};
};

} /* namespace wsi */
