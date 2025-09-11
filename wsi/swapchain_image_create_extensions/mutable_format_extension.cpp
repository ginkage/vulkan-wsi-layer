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
 * @file mutable_format_extension.cpp
 */

#include "mutable_format_extension.hpp"

namespace wsi
{

util::unique_ptr<swapchain_image_create_mutable_format> swapchain_image_create_mutable_format::create_unique(
   const VkImageFormatListCreateInfo *image_format_list, util::allocator allocator)
{
   /** Allocate the object directly using the allocator so we don't need to move. */
   auto ptr = allocator.make_unique<swapchain_image_create_mutable_format>(allocator);
   if (!ptr)
   {
      return nullptr;
   }

   ptr->m_format_list.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
   ptr->m_format_list.pNext = nullptr;
   ptr->m_format_list.viewFormatCount = 0;
   ptr->m_format_list.pViewFormats = nullptr;

   if (image_format_list && image_format_list->viewFormatCount > 0)
   {
      if (!ptr->m_view_formats.try_resize(image_format_list->viewFormatCount))
      {
         return nullptr;
      }
      for (uint32_t i = 0; i < image_format_list->viewFormatCount; ++i)
      {
         ptr->m_view_formats[i] = image_format_list->pViewFormats[i];
      }
      ptr->m_format_list.viewFormatCount = image_format_list->viewFormatCount;
      ptr->m_format_list.pViewFormats = ptr->m_view_formats.data();
   }

   return ptr;
}

VkResult swapchain_image_create_mutable_format::extend_image_create_info(VkImageCreateInfo *image_create_info)
{
   /* If this extension is present, mutable format support is requested. */
   image_create_info->flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT_KHR;
   m_format_list.pNext = image_create_info->pNext;
   image_create_info->pNext = &m_format_list;
   return VK_SUCCESS;
}

} /* namespace wsi */
