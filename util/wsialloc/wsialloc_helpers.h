/*
 * Copyright (c) 2024, 2026 Arm Limited.
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

/**
 * @brief Internal callback used in wsiallocp_alloc(). Different wsialloc implementations define this
 * callback and use wsiallocp_alloc to implement the wsialloc_alloc entrypoint.
 */
typedef int (*wsiallocp_alloc_callback)(const wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                                        uint64_t size);
/**
 *
 * @brief Allocate a new buffer using the allocator
 *
 * A helper function to pick the best format out of a list of formats
 * and allocate selected format using the allocator
 *
 * @param      allocator               The wsialloc allocator
 * @param      fn_alloc                The function that will be called to perform the actual memory allocation
 * @param      info                    The requested allocation info
 * @param[out] result                  The allocation result.
 * @retval     WSIALLOC_ERROR_NONE            Indicates success
 * @retval     WSIALLOC_ERROR_INVALID         Indicates failure in formats such as invalid fourcc
 *                                            or lack of any formats provided for selection or invalid parameters
 * @retval     WSIALLOC_ERROR_NOT_SUPPORTED   Can indicate multiple errors, namely:
 *                                               * None of the formats are supported by the wsialloc implementation
 *                                               * The allocator does not support allocating with the selected flags
 */
wsialloc_error wsiallocp_alloc(wsialloc_allocator *allocator, wsiallocp_alloc_callback fn_alloc,
                               const wsialloc_allocate_info *info, wsialloc_allocate_result *result);
