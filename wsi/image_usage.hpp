/*
 * Copyright (c) 2026 Arm Limited.
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
 * @file image_usage.hpp
 *
 * @brief Helpers for VkImageUsageFlags2CreateInfoKHR compatibility.
 */

#pragma once

#include <vulkan/vulkan.h>

#include "layer/wsi_layer_experimental.hpp"
#include "util/helpers.hpp"

namespace wsi
{

inline const VkImageUsageFlags2CreateInfoKHR *find_image_usage_flags_2_create_info(const void *pNext)
{
   return util::find_extension<VkImageUsageFlags2CreateInfoKHR>(VK_STRUCTURE_TYPE_IMAGE_USAGE_FLAGS_2_CREATE_INFO_KHR,
                                                                pNext);
}

inline void prepend_image_usage_flags_2_create_info(VkImageUsageFlags2CreateInfoKHR &image_usage_flags_2_create_info,
                                                    VkImageUsageFlags2KHR usage, const void *&pNext)
{
   image_usage_flags_2_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_USAGE_FLAGS_2_CREATE_INFO_KHR;
   image_usage_flags_2_create_info.pNext = const_cast<void *>(pNext);
   image_usage_flags_2_create_info.usage = usage;
   pNext = &image_usage_flags_2_create_info;
}

} /* namespace wsi */
