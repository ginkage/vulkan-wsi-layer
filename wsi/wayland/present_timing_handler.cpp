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
 * @file present_timing_handler.cpp
 *
 * @brief Contains the functionality to implement features for present timing extension.
 */

#include "present_timing_handler.hpp"

wsi_ext_present_timing_wayland::wsi_ext_present_timing_wayland(const util::allocator &allocator, VkDevice device,
                                                               uint32_t num_images)
   : wsi_ext_present_timing(allocator, device, num_images)
{
}

util::unique_ptr<wsi_ext_present_timing_wayland> wsi_ext_present_timing_wayland::create(
   VkDevice device, const util::allocator &allocator,
   std::optional<VkTimeDomainKHR> image_first_pixel_visible_time_domain, uint32_t num_images)
{

   util::vector<util::unique_ptr<wsi::vulkan_time_domain>> domains(allocator);
   if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
          VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT, VK_TIME_DOMAIN_DEVICE_KHR)))
   {
      return nullptr;
   }

   if (image_first_pixel_visible_time_domain.has_value())
   {
      std::tuple<VkTimeDomainEXT, bool> monotonic_query = { *image_first_pixel_visible_time_domain, false };

      const layer::device_private_data &device_data = layer::device_private_data::get(device);
      auto result = wsi::check_time_domain_support(device_data.physical_device, &monotonic_query, 1);
      if (result != VK_SUCCESS)
      {
         return nullptr;
      }

      if (std::get<1>(monotonic_query))
      {
         if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
                VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT, image_first_pixel_visible_time_domain.value())))
         {
            return nullptr;
         }
      }
   }

   return wsi_ext_present_timing::create<wsi_ext_present_timing_wayland>(allocator, domains.data(), domains.size(),
                                                                         device, num_images);
}

VkResult wsi_ext_present_timing_wayland::get_swapchain_timing_properties(
   uint64_t &timing_properties_counter, VkSwapchainTimingPropertiesEXT &timing_properties)
{
   timing_properties_counter = 0;
   timing_properties.refreshDuration = 0;
   timing_properties.variableRefreshDelay = 0;

   return VK_SUCCESS;
}
