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
 * @brief Contains the implementation for external memory management for swapchain images.
 */

#include "image_backing_memory_external.hpp"

using util::MAX_PLANES;

namespace wsi
{

VkResult image_backing_memory_external::allocate()
{
   allocation_params params = { (m_create_info.flags & VK_IMAGE_CREATE_PROTECTED_BIT) != 0,
                                m_create_info.extent,
                                &m_create_info.selected_format,
                                1,
                                m_create_info.explicit_compression,
                                false };

   wsialloc_allocate_result alloc_result = { {}, { 0 }, { 0 }, { -1 }, false };
   TRY(m_wsialloc_allocator.allocate(params, &alloc_result));

   m_external_mem.set_strides(alloc_result.average_row_strides);
   m_external_mem.set_buffer_fds(alloc_result.buffer_fds);
   m_external_mem.set_offsets(alloc_result.offsets);

   uint32_t num_planes = util::drm::drm_fourcc_format_get_num_planes(alloc_result.format.fourcc);
   uint32_t num_memory_planes = 0;
   for (uint32_t i = 0; i < num_planes; ++i)
   {
      auto it = std::find(std::begin(alloc_result.buffer_fds) + i + 1, std::end(alloc_result.buffer_fds),
                          alloc_result.buffer_fds[i]);
      if (it == std::end(alloc_result.buffer_fds))
      {
         num_memory_planes++;
      }
   }

   assert(alloc_result.is_disjoint == (num_memory_planes > 1));
   m_external_mem.set_num_memories(num_memory_planes);

   m_external_mem.set_format_info(alloc_result.is_disjoint, num_planes);
   m_external_mem.set_memory_handle_type(VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   return VK_SUCCESS;
}

VkResult image_backing_memory_external::import_and_bind(VkImage image)
{
   TRY_LOG(m_external_mem.import_memory_and_bind_swapchain_image(image),
           "Failed to import memory and bind swapchain image");

   return VK_SUCCESS;
}

VkResult image_backing_memory_external::bind(const VkBindImageMemoryInfo *bind_image_mem_info)
{
   return m_external_mem.bind_swapchain_image_memory(bind_image_mem_info->image);
}

external_memory &image_backing_memory_external::get_external_memory()
{
   return m_external_mem;
}

wsialloc_create_info_args image_backing_memory_external::get_image_create_info()
{
   return m_create_info;
}

uint64_t image_backing_memory_external::get_modifier() const
{
   return m_create_info.selected_format.modifier;
}

}