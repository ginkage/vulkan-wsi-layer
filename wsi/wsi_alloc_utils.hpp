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

#pragma once

#include <optional>
#include "util/wsialloc/wsialloc.h"
#include <util/helpers.hpp>

#include "util/custom_allocator.hpp"

#include <vulkan/vulkan.h>

namespace wsi
{

struct allocation_params
{
   bool is_protected_memory;
   VkExtent3D extent;
   wsialloc_format *importable_formats;
   size_t importable_formats_size;
   bool enable_fixed_rate;
   bool avoid_allocation;
};

class swapchain_wsialloc_allocator
{
public:
   static std::optional<swapchain_wsialloc_allocator> create();

   swapchain_wsialloc_allocator(swapchain_wsialloc_allocator &other) = delete;
   swapchain_wsialloc_allocator &operator=(swapchain_wsialloc_allocator &other) = delete;

   swapchain_wsialloc_allocator(swapchain_wsialloc_allocator &&other)
      : m_allocator(other.m_allocator)
   {
      other.m_allocator = nullptr;
   }

   swapchain_wsialloc_allocator &operator=(swapchain_wsialloc_allocator &&other)
   {
      if (this == &other)
      {
         return *this;
      }

      if (m_allocator != nullptr)
      {
         wsialloc_delete(m_allocator);
         m_allocator = nullptr;
      }
      std::swap(m_allocator, other.m_allocator);
      return *this;
   }

   ~swapchain_wsialloc_allocator()
   {
      if (m_allocator != nullptr)
      {
         wsialloc_delete(m_allocator);
      }
   }

   wsialloc_allocator *get()
   {
      return m_allocator;
   }

   VkResult allocate(const allocation_params &input, wsialloc_allocate_result *alloc_result);

private:
   /**
    * @brief Handle to the WSI allocator.
    */
   wsialloc_allocator *m_allocator;

   swapchain_wsialloc_allocator(wsialloc_allocator *allocator)
      : m_allocator(allocator)
   {
   }
};

} /* namespace wsi */