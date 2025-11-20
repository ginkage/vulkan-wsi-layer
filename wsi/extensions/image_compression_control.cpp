/*
 * Copyright (c) 2024-2025 Arm Limited.
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
 * @file image_compression_control.cpp
 *
 * @brief Contains the implementation for VK_EXT_image_compression_control extension.
 */
#include <cassert>

#include <util/helpers.hpp>
#include <wsi/swapchain_base.hpp>

#include "image_compression_control.hpp"

namespace wsi
{
image_create_compression_control::image_create_compression_control(const VkImageCompressionControlEXT &extension)
   : m_compression_control{ VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT, nullptr, extension.flags,
                            extension.compressionControlPlaneCount, m_array_fixed_rate_flags }
{
   for (uint32_t i = 0; i < extension.compressionControlPlaneCount; i++)
   {
      m_compression_control.pFixedRateFlags[i] = extension.pFixedRateFlags[i];
   }
}

image_create_compression_control::image_create_compression_control(const image_create_compression_control &extension)
   : image_create_compression_control(extension.m_compression_control)
{
}

VkImageCompressionControlEXT image_create_compression_control::get_compression_control_properties()
{
   return m_compression_control;
}

VkImageCompressionFlagsEXT image_create_compression_control::get_bitmask_for_image_compression_flags()
{
   return m_compression_control.flags;
}

std::optional<image_create_compression_control> image_create_compression_control::create(
   VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info)
{
   const auto *image_compression_control = util::find_extension<VkImageCompressionControlEXT>(
      VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT, swapchain_create_info->pNext);

   layer::device_private_data &device_data = layer::device_private_data::get(device);
   if (device_data.is_swapchain_compression_control_enabled() && image_compression_control != nullptr)
   {
      return image_create_compression_control{ *image_compression_control };
   }

   return std::nullopt;
}

VkResult image_create_compression_control::extend_image_create_info(VkImageCreateInfo *image_create_info)
{
   assert(image_create_info != nullptr);

   m_compression_control.pNext = image_create_info->pNext;
   image_create_info->pNext = &m_compression_control;

   return VK_SUCCESS;
}

};
