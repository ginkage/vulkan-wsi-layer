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
#if VULKAN_WSI_LAYER_EXPERIMENTAL
#include <cstdint>
#include <array>
#include <optional>
#include <algorithm>
#include "present_timing_handler.hpp"
#include "layer/private_data.hpp"

wsi_ext_present_timing_headless::wsi_ext_present_timing_headless(const util::allocator &allocator, VkDevice device,
                                                                 uint32_t num_images)
   : wsi::wsi_ext_present_timing(allocator, device, num_images)
{
}
/**
 * @brief Queries whether the driver supports the raw monotonic clock domain.
 *
 * This function invokes vkGetPhysicalDeviceCalibrateableTimeDomainsKHR twice:
 * 1. To query the count of supported time domains.
 * 2. To retrieve the list of supported time domains.
 *
 * @param device The Vulkan logical device whose physical device is queried. Must be valid.
 * @return A std::optional<bool> with:
 *         - true if VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR is supported.
 *         - false if VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR is not supported.
 *         - std::nullopt if the query fails (e.g., vkGetPhysicalDeviceCalibrateableTimeDomainsKHR
 *           returns an error or memory allocation fails).
 */
static std::optional<bool> is_time_domain_clock_monotonic_raw_supported(const VkDevice &device)
{
   auto &dev_data = layer::device_private_data::get(device);
   auto &physicalDevice = dev_data.physical_device;
   auto &instance = dev_data.instance_data;

   uint32_t supported_domains_count = 0;
   VkResult result =
      instance.disp.GetPhysicalDeviceCalibrateableTimeDomainsKHR(physicalDevice, &supported_domains_count, nullptr);
   if (result != VK_SUCCESS)
   {
      return std::nullopt;
   }

   util::allocator allocator(instance.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   util::vector<VkTimeDomainEXT> supported_domains(allocator);
   if (!supported_domains.try_resize(supported_domains_count))
   {
      return std::nullopt;
   }

   result = instance.disp.GetPhysicalDeviceCalibrateableTimeDomainsKHR(physicalDevice, &supported_domains_count,
                                                                       supported_domains.data());
   if (result != VK_SUCCESS)
   {
      return std::nullopt;
   }

   bool supported = std::find(supported_domains.begin(), supported_domains.end(),
                              VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR) != supported_domains.end();
   return supported;
}

util::unique_ptr<wsi_ext_present_timing_headless> wsi_ext_present_timing_headless::create(
   const VkDevice &device, const util::allocator &allocator, uint32_t num_images)
{
   /*
    * Select the hardware raw monotonic clock domain (unaffected by NTP or adjtime adjustments)
    * when the driver supports it; otherwise use the standard monotonic clock.
    */
   VkTimeDomainKHR monotonic_time_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR;

   auto clock_monotonic_raw_support = is_time_domain_clock_monotonic_raw_supported(device);
   if (!clock_monotonic_raw_support.has_value())
   {
      return nullptr;
   }
   else if (clock_monotonic_raw_support.value() == false)
   {
      monotonic_time_domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
   }

   std::array<util::unique_ptr<wsi::vulkan_time_domain>, 4> time_domains_array = {
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT,
                                                     VK_TIME_DOMAIN_DEVICE_KHR),
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT, monotonic_time_domain),
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT,
                                                     monotonic_time_domain),
      allocator.make_unique<wsi::vulkan_time_domain>(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT,
                                                     monotonic_time_domain)
   };

   return wsi_ext_present_timing::create<wsi_ext_present_timing_headless>(allocator, time_domains_array, device,
                                                                          num_images);
}

VkResult wsi_ext_present_timing_headless::get_swapchain_timing_properties(
   uint64_t &timing_properties_counter, VkSwapchainTimingPropertiesEXT &timing_properties)
{
   /* Use a reasonable approximate (5ms) that most devices should be able to match. */
   const uint64_t fixed_refresh_duration_ns = 1;

   timing_properties_counter = 1;
   timing_properties.refreshDuration = fixed_refresh_duration_ns;
   timing_properties.variableRefreshDelay = UINT64_MAX;

   return VK_SUCCESS;
}
#endif