/*
 * Copyright (c) 2026 Arm Limited.
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
 * @file low_address_map_api.hpp
 *
 * @brief Mapped pointers below 4 GiB for 32-bit applications run by a 64-bit process, such as Windows games under
 *        Wine's WoW64. Ported from mali-vulkan-icd-wrapper's low-address mapping (MIT).
 */

#pragma once

#include <vulkan/vulkan.h>

#include "util/macros.hpp"

namespace layer
{
/**
 * @brief Whether WSI_LOW_ADDRESS_MAP=1 asks the layer to return memory mappings below 4 GiB.
 */
bool low_address_map_enabled();

/**
 * @brief Drop the low-address mappings of a device that is being destroyed.
 */
void low_address_map_destroy_device(VkDevice device);

/**
 * @brief Drop the low-address mapping of memory that is being freed, which unmaps it implicitly.
 */
void low_address_map_free_memory(VkDevice device, VkDeviceMemory memory);
} /* namespace layer */

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                      VkMemoryMapFlags flags, void **ppData) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkMapMemory2(VkDevice device, const VkMemoryMapInfo *pMemoryMapInfo, void **ppData) VWL_API_POST;

VWL_VKAPI_CALL(void) wsi_layer_vkUnmapMemory(VkDevice device, VkDeviceMemory memory) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo *pMemoryUnmapInfo) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                    const VkMappedMemoryRange *pMemoryRanges) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                         const VkMappedMemoryRange *pMemoryRanges) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits,
                         VkFence fence) VWL_API_POST;
