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
 * @file present_timing.cpp
 *
 * @brief Contains the Vulkan entrypoints for the present timing.
 */
#include <cassert>

#include "present_timing_api.hpp"
#include <wsi/extensions/present_timing.hpp>
#include <wsi/swapchain_base.hpp>
#include "util/macros.hpp"

/**
 * @brief Implements vkSetSwapchainPresentTimingQueueSizeEXT Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkSetSwapchainPresentTimingQueueSizeEXT(VkDevice device, VkSwapchainKHR swapchain, uint32_t size) VWL_API_POST
{
   assert(swapchain != VK_NULL_HANDLE);

   auto &device_data = layer::device_private_data::get(device);
   if (!device_data.layer_owns_swapchain(swapchain))
   {
      return device_data.disp.SetSwapchainPresentTimingQueueSizeEXT(device, swapchain, size);
   }

   auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapchain);
   auto *ext = sc->get_swapchain_extension<wsi::wsi_ext_present_timing>(true);

   return ext->present_timing_queue_set_size(size);
}

/**
 * @brief Implements vkGetSwapchainTimingPropertiesEXT Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainTimingPropertiesEXT(VkDevice device, VkSwapchainKHR swapchain,
                                            VkSwapchainTimingPropertiesEXT *pSwapchainTimingProperties,
                                            uint64_t *pSwapchainTimingPropertiesCounter) VWL_API_POST
{
   assert(swapchain != VK_NULL_HANDLE);

   auto &device_data = layer::device_private_data::get(device);
   if (!device_data.layer_owns_swapchain(swapchain))
   {
      return device_data.disp.GetSwapchainTimingPropertiesEXT(device, swapchain, pSwapchainTimingProperties,
                                                              pSwapchainTimingPropertiesCounter);
   }

   auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapchain);
   auto *ext = sc->get_swapchain_extension<wsi::wsi_ext_present_timing>(true);

   return ext->get_swapchain_timing_properties(*pSwapchainTimingPropertiesCounter, *pSwapchainTimingProperties);
}

/**
 * @brief Implements vkGetSwapchainTimeDomainPropertiesEXT Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetSwapchainTimeDomainPropertiesEXT(VkDevice device, VkSwapchainKHR swapchain,
                                                VkSwapchainTimeDomainPropertiesEXT *pSwapchainTimeDomainProperties,
                                                uint64_t *pTimeDomainsCounter) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);

   if (!device_data.layer_owns_swapchain(swapchain))
   {
      return device_data.disp.GetSwapchainTimeDomainPropertiesEXT(device, swapchain, pSwapchainTimeDomainProperties,
                                                                  pTimeDomainsCounter);
   }
   return wsi::swapchain_time_domains::get_swapchain_time_domain_properties(pSwapchainTimeDomainProperties,
                                                                            pTimeDomainsCounter);
}

/**
 * @brief Implements vkGetPastPresentationTimingEXT Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPastPresentationTimingEXT(
   VkDevice device, const VkPastPresentationTimingInfoEXT *pPastPresentationTimingInfo,
   VkPastPresentationTimingPropertiesEXT *pPastPresentationTimingProperties) VWL_API_POST
{
   assert(pPastPresentationTimingInfo != nullptr);
   auto &device_data = layer::device_private_data::get(device);
   if (!device_data.layer_owns_swapchain(pPastPresentationTimingInfo->swapchain))
   {
      return device_data.disp.GetPastPresentationTimingEXT(device, pPastPresentationTimingInfo,
                                                           pPastPresentationTimingProperties);
   }
   auto *sc = reinterpret_cast<wsi::swapchain_base *>(pPastPresentationTimingInfo->swapchain);
   auto *ext = sc->get_swapchain_extension<wsi::wsi_ext_present_timing>(true);
   return ext->get_past_presentation_results(pPastPresentationTimingProperties, pPastPresentationTimingInfo->flags);
}
