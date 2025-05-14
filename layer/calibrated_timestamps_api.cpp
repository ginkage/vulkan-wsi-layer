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
 * @file calibrated_timestamps_api.cpp
 *
 * @brief Contains the Vulkan entrypoints for the calibrated timestamps extension.
 */

#include <wsi/extensions/present_timing.hpp>
#include <wsi/swapchain_base.hpp>

#if VULKAN_WSI_LAYER_EXPERIMENTAL
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetCalibratedTimestampsKHR(VkDevice device, uint32_t timestampCount,
                                       const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
                                       uint64_t *pMaxDeviation) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   struct stage_local_index_and_offset
   {
      uint32_t index;
      uint64_t calibration_offset;
   };
   util::vector<VkCalibratedTimestampInfoKHR> time_stamp_info{ util::allocator(device_data.get_allocator(),
                                                                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT) };
   util::vector<stage_local_index_and_offset> calibration_index_and_offset{ util::allocator(
      device_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT) };

   for (uint32_t i = 0; i < timestampCount; ++i)
   {
      /* Intercept VkSwapchainCalibratedTimestampInfoEXT struct. */
      auto *ext = util::find_extension<VkSwapchainCalibratedTimestampInfoEXT>(
         VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT, pTimestampInfos[i].pNext);

      /* Make a copy of the pTimestampsInfo. */
      if (!time_stamp_info.try_push_back(pTimestampInfos[i]))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      /* The layer is only handling for present stage local time domains,
         every other time domains including swapchain local (VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT)
         is not handled by the layer. */
      if ((ext != nullptr) && (pTimestampInfos[i].timeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT))
      {
         assert(ext->swapchain != VK_NULL_HANDLE);
         if (!device_data.layer_owns_swapchain(ext->swapchain))
         {
            continue;
         }
         /* Check only one present stage is stated. */
         assert(((ext->presentStage & (~(ext->presentStage) + 1)) == ext->presentStage));
         auto *swapchain = reinterpret_cast<wsi::swapchain_base *>(ext->swapchain);
         auto *present_timing_extension = swapchain->get_swapchain_extension<wsi::wsi_ext_present_timing>(true);
         wsi::swapchain_calibrated_time calibrated_time;
         TRY_LOG_CALL(present_timing_extension->get_swapchain_time_domains().calibrate(
            static_cast<VkPresentStageFlagBitsEXT>(ext->presentStage), &calibrated_time));
         stage_local_index_and_offset index_and_offset = { i, calibrated_time.offset };

         if (!calibration_index_and_offset.try_push_back(index_and_offset))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
         time_stamp_info[i].timeDomain = calibrated_time.time_domain;
      }
   }
   TRY_LOG_CALL(device_data.disp.GetCalibratedTimestampsKHR(device, timestampCount, &time_stamp_info[0], pTimestamps,
                                                            pMaxDeviation));

   /* Loop through the calibration_index_and_offset vector and update the timestamps that are stage local
   with its respective offset. */
   for (const auto &iter : calibration_index_and_offset)
   {
      pTimestamps[iter.index] += iter.calibration_offset;
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
#endif /* VULKAN_WSI_LAYER_EXPERIMENTAL */
