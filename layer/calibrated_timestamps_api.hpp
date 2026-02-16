/*
 * Copyright (c) 2025-2026 Arm Limited.
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
 * @file calibrated_timestamps_api.hpp
 *
 * @brief Contains the Vulkan entrypoints for Calibrated Timestamps extension.
 *
 */

#pragma once
#include "util/macros.hpp"

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetCalibratedTimestampsEXT(VkDevice device, uint32_t timestampCount,
                                       const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
                                       uint64_t *pMaxDeviation) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetCalibratedTimestampsKHR(VkDevice device, uint32_t timestampCount,
                                       const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
                                       uint64_t *pMaxDeviation) VWL_API_POST;

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(VkPhysicalDevice physicalDevice, uint32_t *pTimeDomainCount,
                                                         VkTimeDomainKHR *pTimeDomains) VWL_API_POST;
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(VkPhysicalDevice physicalDevice, uint32_t *pTimeDomainCount,
                                                         VkTimeDomainEXT *pTimeDomains) VWL_API_POST;
