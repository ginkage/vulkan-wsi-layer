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
 * @file vulkan_image_handle_creator.hpp
 */

#pragma once

#include <vulkan/vulkan.h>
#include <util/custom_allocator.hpp>
#include <wsi/extensions/image_create_info_extension.hpp>

#include "swapchain_image.hpp"

namespace wsi
{

/**
 * @brief This class is responsible for owning and extending a swapchain's image
 * creation info and creating Vulkan image handles.
 */
class vulkan_image_handle_creator
{
public:
   /**
    * @brief Construct a new vulkan image handle creator
    *
    * @param allocator Allocator
    * @param swapchain_create_info Swapchain create info passed by application.
    */
   vulkan_image_handle_creator(util::allocator allocator, const VkSwapchainCreateInfoKHR &swapchain_create_info);

   /**
    * @brief Create Vulkan swapchain image handle
    *
    * @param device_data Device data
    * @param allocator Allocator
    * @param out_image_handle If successful, Vulkan image handle
    * @return Vulkan result code.
    */
   VkResult create_image(layer::device_private_data &device_data, const util::allocator &allocator,
                         VkImage &out_image_handle);

   /**
    * @brief Extend create info with extension data.
    *
    * @param extension Swapchain image create info extension.
    *
    * @return VK_SUCCESS on success, an appropriate error code on failure.
    */
   VkResult add_extension(util::unique_ptr<image_create_info_extension> extension);

   VkImageCreateInfo get_image_create_info() const
   {
      return m_image_create_info;
   }

private:
   /**
    * @brief Image create info used for all swapchain images.
    */
   VkImageCreateInfo m_image_create_info;

   /**
    * @brief Swapchain image extensions needed for extending image create info.
    */
   util::vector<util::unique_ptr<image_create_info_extension>> m_extensions;
};

} /* namespace wsi */