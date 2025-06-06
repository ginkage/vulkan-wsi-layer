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
 * @file present_wait_api.cpp
 *
 * @brief Contains the Vulkan entrypoints for the present wait.
 */
#include <cassert>
#include <wsi/extensions/present_wait.hpp>
#include <wsi/swapchain_base.hpp>

#include "present_wait_api.hpp"

/**
 * @brief Implements vkSetSwapchainPresentTimingQueueSizeEXT Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkWaitForPresentKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t present_id,
                              uint64_t timeout) VWL_API_POST
{
   assert(swapchain != VK_NULL_HANDLE);

   auto &device_data = layer::device_private_data::get(device);
   if (!device_data.layer_owns_swapchain(swapchain))
   {
      return device_data.disp.WaitForPresentKHR(device, swapchain, present_id, timeout);
   }

   auto *sc = reinterpret_cast<wsi::swapchain_base *>(swapchain);
   auto *ext = sc->get_swapchain_extension<wsi::wsi_ext_present_wait>(true);

   return ext->wait_for_present_id(present_id, timeout);
}
