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
 * @file image_backing_memory_device.cpp
 *
 * @brief Contains the implementatation for the image_backing_memory_device class.
 */

#include "image_backing_memory_device.hpp"
#include <util/helpers.hpp>

namespace wsi
{

image_backing_memory_device::image_backing_memory_device(const layer::device_private_data &device_data,
                                                         util::allocator allocator)
   : m_device_data(device_data)
   , m_allocator(allocator)
{
}

image_backing_memory_device::~image_backing_memory_device()
{
   if (m_device_memory != VK_NULL_HANDLE)
   {
      m_device_data.disp.FreeMemory(m_device_data.device, m_device_memory, m_allocator.get_original_callbacks());
      m_device_memory = VK_NULL_HANDLE;
   }
}

VkResult image_backing_memory_device::allocate_and_bind(VkImage image)
{
   const auto &disp_table = m_device_data.disp;

   VkMemoryRequirements memory_requirements = {};
   disp_table.GetImageMemoryRequirements(m_device_data.device, image, &memory_requirements);

   /* Find a memory type */
   size_t mem_type_idx = 0;
   for (; mem_type_idx < 8 * sizeof(memory_requirements.memoryTypeBits); ++mem_type_idx)
   {
      if (memory_requirements.memoryTypeBits & (1u << mem_type_idx))
      {
         break;
      }
   }

   assert(mem_type_idx <= 8 * sizeof(memory_requirements.memoryTypeBits) - 1);

   VkMemoryAllocateInfo mem_info = {};
   mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mem_info.allocationSize = memory_requirements.size;
   mem_info.memoryTypeIndex = mem_type_idx;

   TRY(disp_table.AllocateMemory(m_device_data.device, &mem_info, m_allocator.get_original_callbacks(),
                                 &m_device_memory));
   TRY(disp_table.BindImageMemory(m_device_data.device, image, m_device_memory, 0));

   return VK_SUCCESS;
}

VkResult image_backing_memory_device::bind(const VkBindImageMemoryInfo *bind_image_mem_info)
{
   return m_device_data.disp.BindImageMemory(m_device_data.device, bind_image_mem_info->image, m_device_memory, 0);
}

uint64_t image_backing_memory_device::get_modifier() const
{
   /* The driver choses the modifier, we have no control over it. */
   return 0;
}

}