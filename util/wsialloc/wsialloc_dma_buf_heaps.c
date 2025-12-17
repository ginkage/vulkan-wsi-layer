/*
 * Copyright (c) 2024-2025 Arm Limited.
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

#include "wsialloc.h"
#include "wsialloc_helpers.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/dma-heap.h>

/**
 * @brief Version of the wsialloc interface we are implementing in this file.
 *
 * This should only be increased when this implementation is updated to match newer versions of wsialloc.h.
 */
#define WSIALLOC_IMPLEMENTATION_VERSION 3

/* Ensure we are implementing the wsialloc version matching the wsialloc.h header we are using. */
#if WSIALLOC_IMPLEMENTATION_VERSION != WSIALLOC_INTERFACE_VERSION
#error "Version mismatch between wsialloc implementation and interface version"
#endif

const uint32_t WSIALLOC_IMPLEMENTATION_VERSION_SYMBOL = WSIALLOC_IMPLEMENTATION_VERSION;

/* Some string may contain commas, setting the macro as variadic */
#define STR_EXPAND(tok...) #tok
#define STR(tok) STR_EXPAND(tok)

struct wsialloc_allocator
{
   /* File descriptor for a DMA-BUF heap for allocating memory accessible to
    * the windowing system (display, compositor, etc.)
    */
   int memory_fd;

   /* File descriptor for a DMA-BUF heap for allocating protected memory
    * accessible to the windowing system.
    */
   int protected_fd;
};

static int allocate(int fd, uint64_t size)
{
   assert(size > 0);
   assert(fd != -1);

   struct dma_heap_allocation_data heap_data = {
      .len = size,
      .fd_flags = O_RDWR | O_CLOEXEC,
   };

   int ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &heap_data);
   if (ret != 0)
   {
      return -errno;
   }
   assert(heap_data.fd > 0);
   return (int)heap_data.fd;
}

static int dma_allocate(const wsialloc_allocator *allocator, const wsialloc_allocate_info *info, uint64_t size)
{
   assert(allocator != NULL);
   assert(info != NULL);
   assert(size > 0);

   /* The only error that can be encountered on allocations is lack of resources. Other parameter validation and
    * support checks are done on format selection. */
   int alloc_fd = allocator->memory_fd;
   if (info->flags & WSIALLOC_ALLOCATE_PROTECTED)
   {
      alloc_fd = allocator->protected_fd;
   }
   if (alloc_fd < 0)
   {
      assert(false);
      return -1;
   }

   return allocate(alloc_fd, size);
}

wsialloc_error wsialloc_new(wsialloc_allocator **allocator)
{
   assert(allocator != NULL);

   wsialloc_allocator *dma_buf_heaps = malloc(sizeof(*dma_buf_heaps));
   if (NULL == dma_buf_heaps)
   {
      return WSIALLOC_ERROR_NO_RESOURCE;
   }

   dma_buf_heaps->memory_fd = open("/dev/dma_heap/" STR(WSIALLOC_MEMORY_HEAP_NAME), O_RDWR);
   dma_buf_heaps->protected_fd = -1;

   if (dma_buf_heaps->memory_fd < 0)
   {
      free(dma_buf_heaps);
      return WSIALLOC_ERROR_NO_RESOURCE;
   }

   *allocator = dma_buf_heaps;
   return WSIALLOC_ERROR_NONE;
}

static void close_fd(int fd)
{
   if (fd >= 0)
   {
      close(fd);
   }
}

void wsialloc_delete(wsialloc_allocator *allocator)
{
   assert(allocator != NULL);
   if (NULL == allocator)
   {
      return;
   }

   close_fd(allocator->memory_fd);
   close_fd(allocator->protected_fd);

   allocator->memory_fd = -1;
   allocator->protected_fd = -1;

   free(allocator);
}

wsialloc_error wsialloc_alloc(wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                              wsialloc_allocate_result *result)
{
   int fd_to_use = allocator->memory_fd;
   if (info->flags & WSIALLOC_ALLOCATE_PROTECTED)
   {
      fd_to_use = allocator->protected_fd;
   }
   if (fd_to_use < 0)
   {
      return WSIALLOC_ERROR_NO_RESOURCE;
   }
   return wsiallocp_alloc(allocator, dma_allocate, info, result);
}
