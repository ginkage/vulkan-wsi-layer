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
 * @file calibrated_timestamps_api.cpp
 *
 * @brief Contains the Vulkan entrypoints for the calibrated timestamps extension.
 */

#include <wsi/extensions/present_timing.hpp>
#include <wsi/swapchain_base.hpp>

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetCalibratedTimestampsKHR(VkDevice device, uint32_t timestampCount,
                                       const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
                                       uint64_t *pMaxDeviation) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   struct timestamp_output
   {
      uint32_t query_index;
      uint64_t calibration_offset;
      VkTimeDomainKHR domain;
      bool translated_stage_local;
   };
   const util::allocator allocator(device_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   util::vector<VkCalibratedTimestampInfoKHR> query_infos{ allocator };
   util::vector<timestamp_output> outputs{ allocator };
   if (!query_infos.try_reserve(timestampCount) || !outputs.try_reserve(timestampCount))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t i = 0; i < timestampCount; ++i)
   {
      /* Intercept VkSwapchainCalibratedTimestampInfoEXT struct. */
      auto *ext = util::find_extension<VkSwapchainCalibratedTimestampInfoEXT>(
         VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT, pTimestampInfos[i].pNext);

      VkCalibratedTimestampInfoKHR query_info = pTimestampInfos[i];
      uint64_t calibration_offset = 0;
      bool translated_stage_local = false;

      /* The layer is only handling for present stage local time domains,
         every other time domains including swapchain local (VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT)
         is not handled by the layer. */
      if (pTimestampInfos[i].timeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT)
      {
         /* If timeDomain is VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT or VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT,
          * the pNext chain must include a VkSwapchainCalibratedTimestampInfoEXT structure.
          */
         assert(ext != nullptr);
         assert(ext->swapchain != VK_NULL_HANDLE);
         if (device_data.layer_owns_swapchain(ext->swapchain))
         {
            /* Check only one present stage is stated. */
            assert(((ext->presentStage & (~(ext->presentStage) + 1)) == ext->presentStage));
            auto *swapchain = reinterpret_cast<wsi::swapchain_base *>(ext->swapchain);
            auto *present_timing_extension = swapchain->get_swapchain_extension<wsi::wsi_ext_present_timing>(true);
            wsi::swapchain_calibrated_time calibrated_time;
            TRY_LOG_CALL(present_timing_extension->get_swapchain_time_domains().calibrate(
               static_cast<VkPresentStageFlagBitsEXT>(ext->presentStage), &calibrated_time));
            query_info.timeDomain = calibrated_time.time_domain;
            query_info.pNext = nullptr;
            calibration_offset = calibrated_time.offset;
            translated_stage_local = true;
         }
      }

      auto query_index = static_cast<uint32_t>(query_infos.size());
      const bool query_is_swapchain_local = (query_info.timeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT) ||
                                            (query_info.timeDomain == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT);

      /* Several logical stage-local domains may use the same Vulkan time
       * domain. Query that physical domain once to preserve the uniqueness
       * required by vkGetCalibratedTimestampsKHR, then fan the value out to
       * each logical output below. Passthrough swapchain-local queries remain
       * distinct because their pNext data identifies the logical domain. */
      if (!query_is_swapchain_local)
      {
         for (uint32_t j = 0; j < query_infos.size(); ++j)
         {
            if (query_infos[j].timeDomain == query_info.timeDomain)
            {
               query_index = j;
               /* If a translated entry was seen first, retain the original
                * application's complete query when its physical domain is
                * encountered later. */
               if (!translated_stage_local)
               {
                  query_infos[j] = query_info;
               }
               break;
            }
         }
      }

      if (query_index == query_infos.size())
      {
         if (!query_infos.try_push_back(query_info))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }

      if (!outputs.try_push_back(
             timestamp_output{ query_index, calibration_offset, query_info.timeDomain, translated_stage_local }))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   util::vector<uint64_t> query_timestamps{ allocator };
   if (!query_timestamps.try_resize(query_infos.size()))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY_LOG_CALL(device_data.disp.GetCalibratedTimestampsKHR(
      device, static_cast<uint32_t>(query_infos.size()), query_infos.data(), query_timestamps.data(), pMaxDeviation));

   float timestamp_period = 0.0F;
   for (const auto &output : outputs)
   {
      if (output.translated_stage_local && (output.domain == VK_TIME_DOMAIN_DEVICE_KHR))
      {
         VkPhysicalDeviceProperties2KHR physical_device_properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
                                                                    nullptr,
                                                                    {} };
         device_data.instance_data.disp.GetPhysicalDeviceProperties2KHR(device_data.physical_device,
                                                                        &physical_device_properties);
         timestamp_period = physical_device_properties.properties.limits.timestampPeriod;
         break;
      }
   }

   for (uint32_t i = 0; i < timestampCount; ++i)
   {
      const auto &output = outputs[i];
      uint64_t timestamp = query_timestamps[output.query_index];
      if (output.translated_stage_local)
      {
         if (output.domain == VK_TIME_DOMAIN_DEVICE_KHR)
         {
            timestamp = wsi::ticks_to_ns(timestamp, timestamp_period);
         }
         timestamp += output.calibration_offset;
      }
      pTimestamps[i] = timestamp;
   }
   return VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetCalibratedTimestampsEXT(VkDevice device, uint32_t timestampCount,
                                       const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
                                       uint64_t *pMaxDeviation) VWL_API_POST
{
   return wsi_layer_vkGetCalibratedTimestampsKHR(device, timestampCount, pTimestampInfos, pTimestamps, pMaxDeviation);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(VkPhysicalDevice physicalDevice, uint32_t *pTimeDomainCount,
                                                         VkTimeDomainKHR *pTimeDomains) VWL_API_POST
{
   auto &instance_data = layer::instance_private_data::get(physicalDevice);
   assert(pTimeDomainCount != nullptr);
   VkSwapchainTimeDomainPropertiesEXT swapchain_time_domain_properties = {
      VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT, nullptr, 0, nullptr, nullptr
   };
   uint32_t requested_domain_count = *pTimeDomainCount;
   TRY(instance_data.disp.GetPhysicalDeviceCalibrateableTimeDomainsKHR(physicalDevice, pTimeDomainCount, pTimeDomains));
   auto present_timing_supported = wsi::present_timing_dependencies_supported(physicalDevice);
   if (std::holds_alternative<VkResult>(present_timing_supported))
   {
      /* Return error code */
      return std::get<VkResult>(present_timing_supported);
   }
   if (!std::get<bool>(present_timing_supported))
   {
      /* Present timing dependencies not supported */
      return VK_SUCCESS;
   }

   if (pTimeDomains == nullptr)
   {
      TRY(
         wsi::swapchain_time_domains::get_swapchain_time_domain_properties(&swapchain_time_domain_properties, nullptr));
      *pTimeDomainCount += swapchain_time_domain_properties.timeDomainCount;
      return VK_SUCCESS;
   }
   if (requested_domain_count == *pTimeDomainCount)
   {
      return VK_INCOMPLETE;
   }
   swapchain_time_domain_properties.pTimeDomains = &pTimeDomains[*pTimeDomainCount];
   swapchain_time_domain_properties.timeDomainCount = requested_domain_count - *pTimeDomainCount;
   VkResult result =
      wsi::swapchain_time_domains::get_swapchain_time_domain_properties(&swapchain_time_domain_properties, nullptr);
   if ((result == VK_SUCCESS) || (result == VK_INCOMPLETE))
   {
      *pTimeDomainCount += swapchain_time_domain_properties.timeDomainCount;
   }
   return result;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(VkPhysicalDevice physicalDevice, uint32_t *pTimeDomainCount,
                                                         VkTimeDomainEXT *pTimeDomains) VWL_API_POST
{
   return wsi_layer_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(physicalDevice, pTimeDomainCount, pTimeDomains);
}
