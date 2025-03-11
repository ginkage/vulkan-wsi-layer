/*
 * Copyright (c) 2024 Arm Limited.
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

#include "wsialloc_helpers.h"
#include "format_table.h"

#include <assert.h>

/** Default alignment */
#define WSIALLOCP_MIN_ALIGN_SZ (64u)
/** Maximum image size allowed for each dimension */
#define MAX_IMAGE_SIZE 128000

typedef struct wsialloc_format_descriptor
{
   wsialloc_format format;
   fmt_spec format_spec;
} wsialloc_format_descriptor;

static uint64_t round_size_up_to_align(uint64_t size)
{
   return (size + WSIALLOCP_MIN_ALIGN_SZ - 1) & ~(WSIALLOCP_MIN_ALIGN_SZ - 1);
}

static wsialloc_error calculate_format_properties(const wsialloc_format_descriptor *descriptor,
                                                  const wsialloc_allocate_info *info, int *strides, uint32_t *offsets,
                                                  uint64_t *total_size)
{
   assert(descriptor != NULL);
   assert(info != NULL);
   assert(strides != NULL);
   assert(offsets != NULL);
   assert(total_size != NULL);

   const uint8_t *bits_per_pixel = descriptor->format_spec.bpp;
   const uint64_t flags = descriptor->format.flags;
   const uint64_t modifier = descriptor->format.modifier;
   const uint32_t num_planes = descriptor->format_spec.nr_planes;

   /* We currently don't support any kind of custom modifiers */
   if (modifier != DRM_FORMAT_MOD_LINEAR)
   {
      return WSIALLOC_ERROR_NOT_SUPPORTED;
   }
   /* No multi-plane format support */
   if (num_planes > 1)
   {
      return WSIALLOC_ERROR_NOT_SUPPORTED;
   }

   size_t size = 0;
   for (size_t plane = 0; plane < num_planes; plane++)
   {
      /* Assumes multiple of 8--rework otherwise. */
      const uint32_t plane_bytes_per_pixel = bits_per_pixel[plane] / 8;
      assert(plane_bytes_per_pixel * 8 == bits_per_pixel[plane]);

      /* With large enough width, this can overflow as strides are signed. In practice, this shouldn't happen */
      strides[plane] = round_size_up_to_align(info->width * plane_bytes_per_pixel);

      offsets[plane] = size;

      size += strides[plane] * info->height;
   }
   *total_size = size;
   return WSIALLOC_ERROR_NONE;
}

static const fmt_spec *find_format(uint32_t fourcc)
{
   /* Mask off any bits not necessary for allocation size */
   fourcc = fourcc & (~(uint32_t)DRM_FORMAT_BIG_ENDIAN);

   /* Search table for the format*/
   for (size_t i = 0; i < fourcc_format_table_len; i++)
   {
      if (fourcc == fourcc_format_table[i].drm_format)
      {
         const fmt_spec *found_fmt = &fourcc_format_table[i];
         assert(found_fmt->nr_planes <= WSIALLOC_MAX_PLANES);

         return found_fmt;
      }
   }

   return NULL;
}

static bool validate_parameters(const wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                                wsialloc_allocate_result *result)
{
   if (allocator == NULL)
   {
      return false;
   }
   else if (!result)
   {
      return false;
   }
   else if (info->format_count == 0 || info->formats == NULL)
   {
      return false;
   }
   else if (info->width < 1 || info->height < 1 || info->width > MAX_IMAGE_SIZE || info->height > MAX_IMAGE_SIZE)
   {
      return false;
   }

   return true;
}

wsialloc_error wsiallocp_alloc(wsialloc_allocator *allocator, wsiallocp_alloc_callback fn_alloc,
                               const wsialloc_allocate_info *info, wsialloc_allocate_result *result)
{
   if (!validate_parameters(allocator, info, result))
   {
      return WSIALLOC_ERROR_INVALID;
   }

   int local_strides[WSIALLOC_MAX_PLANES] = {};
   int local_offsets[WSIALLOC_MAX_PLANES] = {};
   wsialloc_error err = WSIALLOC_ERROR_NONE;
   wsialloc_format_descriptor selected_format_desc = {};

   uint64_t total_size = 0;
   for (size_t i = 0; i < info->format_count; i++)
   {
      const wsialloc_format *current_format = &info->formats[i];
      const fmt_spec *format_spec = find_format(current_format->fourcc);
      if (!format_spec)
      {
         err = WSIALLOC_ERROR_NOT_SUPPORTED;
         continue;
      }

      wsialloc_format_descriptor current_format_desc = { *current_format, *format_spec };
      err = calculate_format_properties(&current_format_desc, info, local_strides, local_offsets, &total_size);
      if (err != WSIALLOC_ERROR_NONE)
      {
         continue;
      }

      /* A compatible format was found */
      selected_format_desc = current_format_desc;
      break;
   }

   if (err != WSIALLOC_ERROR_NONE)
   {
      return err;
   }

   int local_fds[WSIALLOC_MAX_PLANES] = { -1, -1, -1, -1 };
   if (!(info->flags & WSIALLOC_ALLOCATE_NO_MEMORY))
   {
      local_fds[0] = fn_alloc(allocator, info, total_size);
      if (local_fds[0] < 0)
      {
         return WSIALLOC_ERROR_NO_RESOURCE;
      }

      assert(result->buffer_fds != NULL);
      result->buffer_fds[0] = local_fds[0];
      for (size_t plane = 1; plane < selected_format_desc.format_spec.nr_planes; plane++)
      {
         result->buffer_fds[plane] = result->buffer_fds[0];
      }
   }
   result->format = selected_format_desc.format;
   for (size_t plane = 0; plane < selected_format_desc.format_spec.nr_planes; plane++)
   {
      result->average_row_strides[plane] = local_strides[plane];
      result->offsets[plane] = local_offsets[plane];
   }

   result->is_disjoint = false;
   return WSIALLOC_ERROR_NONE;
}
