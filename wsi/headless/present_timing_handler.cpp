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
 * @file present_timing.cpp
 *
 * @brief Contains the functionality to implement features for present timing extension.
 */
#include <cstdint>
#include <array>
#include <algorithm>
#include "present_timing_handler.hpp"
#include "layer/private_data.hpp"

wsi_ext_present_timing_headless::wsi_ext_present_timing_headless(const util::allocator &allocator, VkDevice device,
                                                                 uint32_t num_images,
                                                                 std::optional<VkTimeDomainEXT> monotonic_domain,
                                                                 bool is_swapchain_using_shared_present_mode)
   : wsi::wsi_ext_present_timing(allocator, device, num_images)
   , m_monotonic_domain(monotonic_domain)
   , m_is_swapchain_using_shared_present_mode(is_swapchain_using_shared_present_mode)
{
}

util::unique_ptr<wsi_ext_present_timing_headless> wsi_ext_present_timing_headless::create(
   const util::allocator &allocator, const VkDevice &device, uint32_t num_images,
   bool is_swapchain_using_shared_present_mode)
{
   auto &dev_data = layer::device_private_data::get(device);

   /*
    * Select the hardware raw monotonic clock domain (unaffected by NTP or adjtime adjustments)
    * when the driver supports it; otherwise use the standard monotonic clock.
    */
   std::array monotonic_domains = {
      std::tuple{ VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT, false },
      std::tuple{ VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT, false },
   };
   auto result =
      wsi::check_time_domain_support(dev_data.physical_device, monotonic_domains.data(), monotonic_domains.size());
   if (result != VK_SUCCESS)
   {
      return nullptr;
   }

   std::optional<VkTimeDomainEXT> monotonic_domain;
   for (auto [domain, supported] : monotonic_domains)
   {
      if (supported)
      {
         monotonic_domain = domain;
         break;
      }
   }

   util::vector<util::unique_ptr<wsi::vulkan_time_domain>> domains(allocator);
   if (!is_swapchain_using_shared_present_mode)
   {
      if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
             VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT, VK_TIME_DOMAIN_DEVICE_KHR)))
      {
         return nullptr;
      }

      if (monotonic_domain)
      {
         if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
                VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT, *monotonic_domain)))
         {
            return nullptr;
         }
         if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
                VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT, *monotonic_domain)))
         {
            return nullptr;
         }
         if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
                VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT, *monotonic_domain)))
         {
            return nullptr;
         }
      }
   }

   return wsi_ext_present_timing::create<wsi_ext_present_timing_headless>(allocator, domains.data(), domains.size(),
                                                                          device, num_images, monotonic_domain,
                                                                          is_swapchain_using_shared_present_mode);
}

VkResult wsi_ext_present_timing_headless::get_swapchain_timing_properties(
   uint64_t &timing_properties_counter, VkSwapchainTimingPropertiesEXT &timing_properties)
{
   /*
    * The headless backend does not have a limit on how fast the swapchain can be presented.
    * We set the refresh duration to 1, because on some platforms, 0 is returned until after
    * at least one image had been presented. On these platforms, 0 is used to indicate timing
    * properties are currently unavailable.
    */
   const uint64_t fixed_refresh_duration_ns = 1;

   timing_properties_counter = 1;
   timing_properties.refreshDuration = fixed_refresh_duration_ns;
   timing_properties.refreshInterval = fixed_refresh_duration_ns;

   return VK_SUCCESS;
}

std::optional<uint64_t> wsi_ext_present_timing_headless::get_current_clock_time_ns() const
{
   if (!m_monotonic_domain.has_value())
   {
      return std::nullopt;
   }

   clockid_t clockid = CLOCK_MONOTONIC_RAW;
   switch (*m_monotonic_domain)
   {
   case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR:
      clockid = CLOCK_MONOTONIC_RAW;
      break;
   case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR:
      clockid = CLOCK_MONOTONIC;
      break;
   default:
      return std::nullopt;
   }

   struct timespec now = {};
   if (clock_gettime(clockid, &now) != 0)
   {
      WSI_LOG_ERROR("Failed to get time of clock %d, error: %d (%s)", clockid, errno, strerror(errno));
      return std::nullopt;
   }

   return (static_cast<uint64_t>(now.tv_sec) * static_cast<uint64_t>(1e9)) + static_cast<uint64_t>(now.tv_nsec);
}

std::optional<uint64_t> wsi_ext_present_timing_headless::get_first_pixel_visible_timestamp_for_last_image() const
{
   if (!m_first_pixel_visible_timestamp_for_last_image.has_value())
   {
      return std::nullopt;
   }
   return m_first_pixel_visible_timestamp_for_last_image;
}

void wsi_ext_present_timing_headless::set_first_pixel_visible_timestamp_for_last_image(uint64_t timestamp)
{
   m_first_pixel_visible_timestamp_for_last_image = timestamp;
}

VkPresentStageFlagsEXT wsi_ext_present_timing_headless::stages_supported()
{
   VkPresentStageFlagsEXT stages = {};

   /* Do not expose any stage when using shared present modes. */
   if (!m_is_swapchain_using_shared_present_mode)
   {
      stages |= VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT | VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT |
                VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT | VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT;
   }
   return stages;
}
