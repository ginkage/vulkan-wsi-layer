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
 * @file image_compression_control.hpp
 *
 * @brief Contains the implementation for VK_EXT_image_compression_control extension.
 *
 */

#pragma once

#include <optional>
#include <utility>

#include <util/custom_allocator.hpp>
#include <util/macros.hpp>

#include "wsi_extension.hpp"

namespace wsi
{
using util::MAX_PLANES;

/**
 * @brief Image compresssion control extension class
 *
 * This class implements the image compression control features.
 * Backends needing additional features will create its own local
 * copy and inherit this class.
 */
class wsi_ext_image_compression_control : public wsi_ext
{
public:
   /**
    * @brief The name of the extension.
    */
   WSI_DEFINE_EXTENSION(VK_EXT_IMAGE_COMPRESSION_CONTROL_EXTENSION_NAME);

   /**
    * @brief Constructor for the wsi_ext_image_compression_control class.
    *
    * @param extension Reference to VkImageCompressionControlEXT structure.
    */
   wsi_ext_image_compression_control(const VkImageCompressionControlEXT &extension);

   wsi_ext_image_compression_control(const wsi_ext_image_compression_control &extension);

   wsi_ext_image_compression_control &operator=(const wsi_ext_image_compression_control &extension)
   {
      if (this == &extension)
      {
         return *this;
      }

      auto compression_control = wsi_ext_image_compression_control(extension);
      std::swap(m_compression_control, compression_control.m_compression_control);
      for (uint32_t i = 0; i < compression_control.m_compression_control.compressionControlPlaneCount; i++)
      {
         m_compression_control.pFixedRateFlags[i] = compression_control.m_compression_control.pFixedRateFlags[i];
      }

      return *this;
   }

   /**
    * @brief Create wsi_ext_image_compression_control class if deemed necessary.
    *
    * @param device The Vulkan device
    * @param swapchain_create_info Swapchain create info
    * @return Valid wsi_ext_image_compression_control if requested by application,
    * otherwise - an empty optional.
    */
   static std::optional<wsi_ext_image_compression_control> create(
      VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info);

   /**
    * @brief This API is used to get the compression control properties of an image.
    *
    * @return The image compression control properties.
    */
   VkImageCompressionControlEXT get_compression_control_properties();

   /**
    * @brief This API is used to get the bitmask for image compression flags.
    *
    * @return The bitmask for image compression flags.
    */
   VkImageCompressionFlagsEXT get_bitmask_for_image_compression_flags();

private:
   /**
    * @brief Array to hold the pFixedRateFlags.
    */
   VkImageCompressionFixedRateFlagsEXT m_array_fixed_rate_flags[MAX_PLANES];

   /**
    * @brief Image compression control properties.
    */
   VkImageCompressionControlEXT m_compression_control;
};

} /* namespace wsi */
