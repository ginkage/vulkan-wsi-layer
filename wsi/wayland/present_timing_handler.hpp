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
#include <optional>
#include "surface_properties.hpp"
#include "wp_presentation_feedback.hpp"
#include "wl_helpers.hpp"
#include <util/custom_mutex.hpp>

namespace wsi
{
namespace wayland
{

/**
 * @brief Present timing extension class
 *
 * This class implements present timing features declarations that are specific to the Wayland backend.
 */
class wsi_ext_present_timing_wayland : public wsi::wsi_ext_present_timing
{
public:
   static util::unique_ptr<wsi_ext_present_timing_wayland> create(
      VkDevice device, const util::allocator &allocator,
      std::optional<VkTimeDomainKHR> image_first_pixel_visible_time_domain, uint32_t num_images);

   VkResult get_swapchain_timing_properties(uint64_t &timing_properties_counter,
                                            VkSwapchainTimingPropertiesEXT &timing_properties) override;

   /**
    * @brief Insert into pending present id list.
    *
    * @param image_index The index of the image to be inserted in the list.
    *
    * @param feedback_obj The feedback object containing the callback.
    *
    * @return Pointer to the presentation feedback object in the pending presents list.
    */
   presentation_feedback *insert_into_pending_present_feedback_list(uint32_t image_index,
                                                                    struct wp_presentation_feedback *feedback_obj);
   /**
    * @brief Remove a present id from the pending present id list.
    *
    * @param image_index The index of the image to be inserted in the list.
    *
    */
   void remove_from_pending_present_feedback_list(uint32_t image_index);

   /**
    * @brief Updates the first pixel out timing in the internal array.
    *
    * @param image_index The index of the image in the swapchain.
    *
    * @param time The time to set for the first pixel out stage.
    *
    */
   void pixelout_callback(uint32_t image_index, uint64_t time);

   /*
    * @brief Copies the pixel out timestamp from the internal array to the present timing queue.
    *
    * @param image_index The index of the image.
    *
    * @param stage_timing_optional The optional stage timing reference wrapper.
    *
    * @return VK_SUCCESS when success, VK_ERROR_SURFACE_LOST_KHR otherwise.
    */
   VkResult get_pixel_out_timing_to_queue(
      uint32_t image_index,
      std::optional<std::reference_wrapper<swapchain_presentation_timing>> stage_timing_optional) override;

   /*
    * @brief Initializes the member variables display and event queue.
    *
    * @param display Pointer to the Wayland display.
    *
    * @param queue Pointer to the Wayland event queue.
    */
   void init(wl_display *display, struct wl_event_queue *queue);

private:
   /**
    * @brief Mutex for synchronising accesses to the pending present id list.
    */
   util::mutex m_pending_presents_lock;

   /**
    * @brief Stores the presentation feedbacks that have been queued.
    */

   util::vector<presentation_feedback> m_pending_presents;
   wsi_ext_present_timing_wayland(const util::allocator &allocator, VkDevice device, uint32_t num_images,
                                  util::vector<std::optional<uint64_t>> &&timestamp_first_pixel_out_storage);

   wl_display *m_display{};
   struct wl_event_queue *m_queue{};
   /**
    * @brief Placeholder for wp_presentation and wp_discarded to write the timestamps.
    * The timestamps are later copied to the first pixel out timing stage in presen_timing's m_queue.
    */
   util::vector<std::optional<uint64_t>> m_timestamp_first_pixel_out;

   /* Allow util::allocator to access the private constructor */
   friend util::allocator;
};

} // namespace wayland
} // namespace wsi

#endif
