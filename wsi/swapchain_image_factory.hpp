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
 * @file swapchain_image_factory.hpp
 *
 * @brief Contains the implementation for the swapchain image factory that
 * is used to create swapchain images.
 */

#pragma once

#include <variant>
#include <functional>

#include <vulkan/vulkan.h>
#include <util/custom_allocator.hpp>
#include <wsi/extensions/image_create_info_extension.hpp>

#include "swapchain_image.hpp"
#include "vulkan_image_handle_creator.hpp"

namespace wsi
{

class swapchain_image_factory
{
public:
   swapchain_image_factory(util::allocator allocator, layer::device_private_data &device_data);

   /**
    * @brief Get the image backing memory object
    *
    * @tparam T The type of the backing memory object
    * @return The image backing memory object
    */
   template <typename T>
   static T &get_backing_memory_from_image(swapchain_image &image)
   {
      assert(image.get_backing_memory<T>() != nullptr);
      return *image.get_backing_memory<T>();
   }

   /**
    * @brief Initalise image factory.
    *
    * @param image_handle_creator Vulkan image handle creator.
    * @param image_memory_creator Backing image memory creator.
    * @param exportable_fence Whether swapchain image needs an exportable fence
    * @param wait_on_present_fence Whether swapchain image should wait on the exportable fence
    *                              when asked to wait on present.
    */
   void init(util::unique_ptr<vulkan_image_handle_creator> image_handle_creator,
             util::unique_ptr<image_backing_memory_creator> image_memory_creator, bool exportable_fence,
             bool wait_on_present_fence);

   /**
    * @brief Create a swapchain image.
    *
    * @return If error occurred, returns VkResult, swapchain_image object otherwise.
    */
   std::variant<VkResult, swapchain_image> create_swapchain_image();

   /**
    * @brief Get the image handle creator.
    *
    * @return Image handle creator.
    */
   vulkan_image_handle_creator &get_image_handle_creator();

private:
   /**
    * @brief Create a Vulkan image handle
    *
    * @return If error occurred, returns VkResult, VkImage handle otherwise.
    */
   std::variant<VkResult, VkImage> create_image_handle();

   util::allocator m_allocator;
   layer::device_private_data &m_device_data;

   /**
    * @brief Creates backing memory for swapchain images
    */
   util::unique_ptr<image_backing_memory_creator> m_image_backing_memory_creator;

   /**
    * @brief Holds the VkImageCreateInfo and backend specific image create info extensions.
    */
   util::unique_ptr<vulkan_image_handle_creator> m_image_handle_creator;

   /**
    * @brief Whether swapchain image needs an exportable fence.
    */
   bool m_exportable_fence;

   /**
    * @brief Whether swapchain image should wait on the exportable fence when asked to wait on present.
    */
   bool m_wait_on_present_fence;
};

} /* namespace wsi */