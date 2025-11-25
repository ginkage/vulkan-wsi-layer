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
 * @file present_timing_handler.hpp
 *
 * @brief Contains the functionality to implement features for present timing extension.
 */
#pragma once

#if VULKAN_WSI_LAYER_EXPERIMENTAL

#include <wsi/extensions/present_timing.hpp>
#include <wsi/swapchain_base.hpp>

/**
 * @brief Present timing extension class
 *
 * This class implements present timing features declarations that are specific to the headless backend.
 */
class wsi_ext_present_timing_headless : public wsi::wsi_ext_present_timing
{
public:
   static util::unique_ptr<wsi_ext_present_timing_headless> create(const util::allocator &allocator,
                                                                   const VkDevice &device, uint32_t num_images);

   VkResult get_swapchain_timing_properties(uint64_t &timing_properties_counter,
                                            VkSwapchainTimingPropertiesEXT &timing_properties) override;

   /**
    * @brief Get a monotonic time domain supported by the driver.
    *
    * If both MONOTONIC_RAW and MONOTONIC are supported, MONOTONIC_RAW is preferred.
    *
    * @return A supported monotonic time domain, or std::nullopt if no monotonic time domain is supported.
    */
   std::optional<VkTimeDomainEXT> get_monotonic_domain() const
   {
      return m_monotonic_domain;
   }

   /**
    * @brief Get the current clock time by using clock_gettime with the monotonic time domain
    *
    * @return Current time in specified domain or std::nullopt in case of error.
    */
   std::optional<uint64_t> get_current_clock_time_ns() const;

   /**
    * @brief Get the first pixel visible timestamp for the last presented image.
    *
    * @return first pixel visible timestamp for the last presented image or std::nullopt in case of error.
    */
   std::optional<uint64_t> get_first_pixel_visible_timestamp_for_last_image() const;

   /**
    * @brief Caches the first pixel visible timestamp for the last presented image.
    *
    */
   void set_first_pixel_visible_timestamp_for_last_image(uint64_t timestamp);

   /*
    * @brief The stages that are supported by the headless backend.
    *
    * @return A bitmask of supported presentation stages.
    */
   VkPresentStageFlagsEXT stages_supported() override;

private:
   wsi_ext_present_timing_headless(const util::allocator &allocator, VkDevice device, uint32_t num_images,
                                   std::optional<VkTimeDomainEXT> monotonic_domain);

   /* Allow util::allocator to access the private constructor */
   friend util::allocator;

   /* Monotonic time domain supported by the driver */
   std::optional<VkTimeDomainEXT> m_monotonic_domain;

   /**
    * Timestamp for the last VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT stage.
    */
   std::optional<uint64_t> m_first_pixel_visible_timestamp_for_last_image;
};

#endif
