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
 * @file image_backing_memory_device.hpp
 *
 * @brief Contains the class declaration for the image_backing_memory_device
 *        for swapchain device memory.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <util/custom_allocator.hpp>
#include <wsi/swapchain_image.hpp>
#include <layer/private_data.hpp>

namespace wsi
{

class image_backing_memory_device : public image_backing_memory
{
public:
   image_backing_memory_device(const layer::device_private_data &device_data, util::allocator allocator);
   virtual ~image_backing_memory_device();

   /**
    * @brief Allocates memory and binds it to Vulkan image.
    *
    * @param image Vulkan image handle.
    *
    * @return Returns VK_SUCCESS on success, otherwise an appropriate error code.
    */
   VkResult allocate_and_bind(VkImage image);

   /**
    * @brief Bind Vulkan image
    *
    * @param bind_image_mem_info Bind info
    * @return VkResult VK_SUCCESS if successful, otherwise an error.
    */
   VkResult bind(const VkBindImageMemoryInfo *bind_image_mem_info) override;

   /**
    * @brief Get the modifier used for the image
    *
    * @return uint64_t DRM format modifier
    */
   uint64_t get_modifier() const override;

private:
   VkDeviceMemory m_device_memory{ VK_NULL_HANDLE };

   const layer::device_private_data &m_device_data;
   const util::allocator m_allocator;
};

class device_backing_memory_creator : public image_backing_memory_creator
{
public:
   device_backing_memory_creator(layer::device_private_data &device_data)
      : m_device_data(device_data)
   {
   }

   virtual ~device_backing_memory_creator() = default;

   util::unique_ptr<image_backing_memory> create_image_backing_memory(util::allocator &allocator) override
   {
      return allocator.make_unique<image_backing_memory_device>(m_device_data, allocator);
   }

private:
   layer::device_private_data &m_device_data;
};

} // namespace wsi
