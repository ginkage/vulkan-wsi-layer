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
 * @file wsi_alloc_utils.hpp
 */

#include "wsi_alloc_utils.hpp"
#include "util/log.hpp"
#include <vulkan/vulkan.h>

namespace wsi
{

std::optional<swapchain_wsialloc_allocator> swapchain_wsialloc_allocator::create()
{
   wsialloc_allocator *allocator = nullptr;
   WSIALLOC_ASSERT_VERSION();
   if (wsialloc_new(&allocator) != WSIALLOC_ERROR_NONE)
   {
      WSI_LOG_ERROR("Failed to create wsi allocator.");
      return std::nullopt;
   }
   return swapchain_wsialloc_allocator(allocator);
}

VkResult swapchain_wsialloc_allocator::allocate(const allocation_params &input, wsialloc_allocate_result *alloc_result)
{
   assert(alloc_result != nullptr);

   uint64_t allocation_flags = input.is_protected_memory ? WSIALLOC_ALLOCATE_PROTECTED : 0;
   if (input.avoid_allocation)
   {
      allocation_flags |= WSIALLOC_ALLOCATE_NO_MEMORY;
   }

   if (input.enable_fixed_rate)
   {
      allocation_flags |= WSIALLOC_ALLOCATE_HIGHEST_FIXED_RATE_COMPRESSION;
   }

   wsialloc_allocate_info alloc_info = { input.importable_formats.data(),
                                         static_cast<unsigned>(input.importable_formats.size()), input.extent.width,
                                         input.extent.height, allocation_flags };

   /* Clear buffer_fds and average_row_strides for error purposes */
   for (int i = 0; i < WSIALLOC_MAX_PLANES; ++i)
   {
      alloc_result->buffer_fds[i] = -1;
      alloc_result->average_row_strides[i] = -1;
   }
   const auto res = wsialloc_alloc(m_allocator, &alloc_info, alloc_result);
   if (res != WSIALLOC_ERROR_NONE)
   {
      WSI_LOG_ERROR("Failed allocation of DMA Buffer. WSI error: %d", static_cast<int>(res));
      if (res == WSIALLOC_ERROR_NOT_SUPPORTED)
      {
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

} /* namespace wsi */