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
 * @file image_create_info_extension.hpp
 *
 * @brief Base class for swapchain image create info extensions.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <util/wsi_extension.hpp>

namespace wsi
{

/**
 * @brief This class should be used with the vulkan_image_handle_creator class
 * to expand the image create properties that are passed to the ICD.
 */
class image_create_info_extension
{
public:
   /**
    * @brief Extend image_create_info pNext with extension specific data.
    *
    * A swapchain image create info extension will use this function to add its
    * extension specific data to pNext of image_create_info.
    *
    * @param[in, out] image_create_info VkImageCreateInfo for creating swapchain images
    */
   virtual VkResult extend_image_create_info(VkImageCreateInfo *image_create_info) = 0;

   virtual ~image_create_info_extension() = default;
};

} /* namespace wsi */