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
 * @file present_timing.cpp
 *
 * @brief Contains the functionality to implement features for present timing extension.
 */

#include "present_timing_handler.hpp"
#include <cstdint>

wsi_ext_present_timing_headless::wsi_ext_present_timing_headless(const util::allocator &allocator)
   : wsi::wsi_ext_present_timing(allocator)
{
}

util::unique_ptr<wsi_ext_present_timing_headless> wsi_ext_present_timing_headless::create(
   const util::allocator &allocator)
{
   std::array<util::unique_ptr<wsi::vulkan_time_domain>, 4> time_domains_array = {
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT,
                                                     VK_TIME_DOMAIN_DEVICE_KHR),
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT,
                                                     VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR),
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT,
                                                     VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR),
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT,
                                                     VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR)
   };

   return wsi_ext_present_timing::create<wsi_ext_present_timing_headless>(allocator, time_domains_array);
}

VkResult wsi_ext_present_timing_headless::get_swapchain_timing_properties(
   uint64_t &timing_properties_counter, VkSwapchainTimingPropertiesEXT &timing_properties)
{
   /* Use a reasonable approximate (5ms) that most devices should be able to match. */
   const uint64_t fixed_refresh_duration_ns = 5e+6;

   timing_properties_counter = 1;
   timing_properties.refreshDuration = fixed_refresh_duration_ns;
   timing_properties.variableRefreshDelay = UINT64_MAX;

   return VK_SUCCESS;
}
