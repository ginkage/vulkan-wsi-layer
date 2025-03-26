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
 * @file swapchain_image_creator.hpp
 */

#pragma once

#include <vulkan/vulkan.h>
#include <util/custom_allocator.hpp>
#include <wsi/swapchain_image_create_extensions/swapchain_image_create_info_extension.hpp>

namespace wsi
{

/**
 * @brief This class is responsible for owning and extending a swapchain's image
 * creation info.
 */
class swapchain_image_creator
{
public:
   swapchain_image_creator(const util::allocator &allocator)
      : m_image_create_info()
      , m_extensions(allocator)
   {
      m_image_create_info.format = VK_FORMAT_UNDEFINED;
   };

   /**
    * @brief Create image create info.
    *
    * @param swapchain_create_info Swapchain create info.
    */
   void init(const VkSwapchainCreateInfoKHR &swapchain_create_info);

   /**
    * @brief Extend create info with extension data/
    *
    * @param extensions Swapchain image create info extensions.
    *
    * @return VK_SUCCESS on success, an appropriate error code on failure.
    */
   VkResult add_extensions(util::vector<util::unique_ptr<swapchain_image_create_info_extension>> *extensions);

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
   util::vector<util::unique_ptr<swapchain_image_create_info_extension>> m_extensions;
};

} /* namespace wsi */