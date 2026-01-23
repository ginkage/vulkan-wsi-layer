/*
 * Copyright (c) 2024-2026 Arm Limited.
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
 * @file wsi_layer_experimental.hpp
 *
 * @brief Contains the declarations of the structures and entry point APIs of experimental extensions.
 *
 */

#pragma once
#include "util/macros.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkSetSwapchainPresentTimingQueueSizeEXT(VkDevice device, VkSwapchainKHR swapchain,
                                                  uint32_t size) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainTimingPropertiesEXT(VkDevice device, VkSwapchainKHR swapchain,
                                            VkSwapchainTimingPropertiesEXT *pSwapchainTimingProperties,
                                            uint64_t *pSwapchainTimingPropertiesCounter) VWL_API_POST;
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainTimeDomainPropertiesEXT(VkDevice device, VkSwapchainKHR swapchain,
                                                VkSwapchainTimeDomainPropertiesEXT *pSwapchainTimeDomainProperties,
                                                uint64_t *pTimeDomainsCounter) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPastPresentationTimingEXT(
   VkDevice device, const VkPastPresentationTimingInfoEXT *pPastPresentationTimingInfo,
   VkPastPresentationTimingPropertiesEXT *pPastPresentationTimingProperties) VWL_API_POST;

#endif
