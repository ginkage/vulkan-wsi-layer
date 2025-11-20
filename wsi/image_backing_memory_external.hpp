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
 * @file
 *
 * @brief Contains the header definitoins for external memory management for swapchain images.
 */

#pragma once

#include <memory>
#include <array>
#include <vulkan/vulkan.h>

#include <layer/private_data.hpp>

#include <util/custom_allocator.hpp>
#include <util/drm/drm_utils.hpp>

#include <wsi/external_memory.hpp>
#include <wsi/swapchain_image.hpp>
#include <wsi/wsi_alloc_utils.hpp>

namespace wsi
{

struct wsialloc_create_info_args
{
   wsialloc_format selected_format;
   VkImageCreateFlags flags;
   VkExtent3D extent;
   bool explicit_compression;
};

class image_backing_memory_external : public image_backing_memory
{
public:
   image_backing_memory_external(const layer::device_private_data *device_data,
                                 swapchain_wsialloc_allocator &wsi_allocator, util::allocator allocator,
                                 wsialloc_create_info_args create_info)
      : m_device_data(device_data)
      , m_external_mem(m_device_data->device, allocator)
      , m_allocator(allocator)
      , m_wsialloc_allocator(wsi_allocator)
      , m_create_info(create_info)
   {
   }

   virtual ~image_backing_memory_external() = default;

   /**
    * @brief Allocate external memory for the image.
    *
    * @return VK_SUCCESS if successful, otherwise an error.
    */
   VkResult allocate();

   /**
    * @brief Import external memory and bind it to the image.
    * Note: The backing memory must be allocated before calling this function.
    * Note: The backing memory file descriptors will be released after this call.
    * @param image Vulkan image to bind the external memory to.
    * @return VkResult VK_SUCCESS if successful, otherwise an error.
    */
   VkResult import_and_bind(VkImage image);

   /**
    * @brief Bind Vulkan image
    *
    * @param bind_image_mem_info Bind info
    * @return VkResult VK_SUCCESS if successful, otherwise an error.
    */
   VkResult bind(const VkBindImageMemoryInfo *bind_image_mem_info) override;

   /**
    * @brief Get the external memory that belongs to the swapchain image
    *
    * @return External memory that belongs to the swapchain image
    */
   external_memory &get_external_memory();

   /**
    * @brief Get the image create information
    *
    * @return Image create information
    */
   wsialloc_create_info_args get_image_create_info();

   /**
    * @brief Get the modifier used for the image
    *
    * @return uint64_t DRM format modifier
    */
   uint64_t get_modifier() const override;

private:
   const layer::device_private_data *m_device_data;
   external_memory m_external_mem;
   util::allocator m_allocator;

   swapchain_wsialloc_allocator &m_wsialloc_allocator;
   wsialloc_create_info_args m_create_info;
};

class external_image_backing_memory_creator : public image_backing_memory_creator
{
public:
   external_image_backing_memory_creator(layer::device_private_data &device_data,
                                         swapchain_wsialloc_allocator &wsi_allocator,
                                         wsialloc_create_info_args create_info)
      : m_device_data(device_data)
      , m_wsialloc_allocator(wsi_allocator)
      , m_create_info(create_info)
   {
   }

   virtual ~external_image_backing_memory_creator() = default;

   util::unique_ptr<image_backing_memory> create_image_backing_memory(util::allocator &allocator) override
   {
      return allocator.make_unique<image_backing_memory_external>(&m_device_data, m_wsialloc_allocator, allocator,
                                                                  m_create_info);
   }

private:
   layer::device_private_data &m_device_data;
   swapchain_wsialloc_allocator &m_wsialloc_allocator;
   wsialloc_create_info_args m_create_info;
};

}