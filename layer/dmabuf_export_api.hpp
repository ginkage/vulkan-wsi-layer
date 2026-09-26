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
 * @file dmabuf_export_api.hpp
 *
 * @brief Emulation of dma-buf memory export for drivers that can only import dma-bufs.
 */

#pragma once

#include <vulkan/vulkan.h>

#include "util/macros.hpp"

namespace layer
{
/**
 * @brief Whether the layer emulates dma-buf export for @p physical_device.
 *
 * True when its driver can import dma-bufs but not export them (the Mali driver), a dma-buf heap is available,
 * and WSI_EMULATE_DMABUF_EXPORT is not 0. Exportable allocations are then made from the heap and imported.
 */
bool dmabuf_export_emulated(VkPhysicalDevice physical_device);
} /* namespace layer */

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                           const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory) VWL_API_POST;

VWL_VKAPI_CALL(void)
wsi_layer_vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR *pGetFdInfo, int *pFd) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                                    const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
                                                    VkImageFormatProperties2 *pImageFormatProperties) VWL_API_POST;

VWL_VKAPI_CALL(void)
wsi_layer_vkGetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties) VWL_API_POST;
