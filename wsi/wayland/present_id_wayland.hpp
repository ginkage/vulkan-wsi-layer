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
 * @file present_id_wayland.hpp
 *
 * @brief Contains the functionality to implement Wayland specific features for present id extension.
 */
#pragma once

#include <wsi/extensions/present_id.hpp>
#include <util/ring_buffer.hpp>
#include "surface_properties.hpp"
#include "wp_presentation_feedback.hpp"
#include <util/custom_mutex.hpp>

namespace wsi
{
namespace wayland
{

/**
 * @brief Present ID extension class
 *
 * This class implements present ID features declarations that are specific to the Wayland backend.
 */
class wsi_ext_present_id_wayland : public wsi::wsi_ext_present_id
{
public:
   /**
    * @brief Insert into pending present id list.
    */
   presentation_feedback *insert_into_pending_present_feedback_list(uint64_t present_id, uint32_t image_index,
                                                                    struct wp_presentation_feedback *feedback_obj);

   /**
    * @brief Marks the given present ID delivered (i.e. its image has been displayed).
    *
    * @param present_id Present ID to mark as delivered.
    */
   virtual void mark_delivered(uint64_t present_id) override;

   /**
    * @brief Marks the buffer as released for the given image index.
    *
    * @param image_index The index of the image in the swapchain.
    */
   void mark_buffer_release(uint32_t image_index);

private:
   /**
    * @brief Remove a present id from the pending present id list.
    */
   void remove_from_pending_present_feedback_list(uint64_t present_id);

   /**
    * @brief Get the present id from image index object
    *
    * @param image_index Image index to check for.
    * @return Present ID associated with the image index or 0 otherwise.
    */
   uint64_t get_present_id_from_image_index(uint32_t image_index);

   /**
    * @brief Mutex for synchronising accesses to the pending present id list.
    */
   util::mutex m_pending_presents_lock;

   /**
    * @brief Stores the presentation feedbacks that have been queued.
    */
   util::ring_buffer<presentation_feedback, wsi::surface_properties::MAX_SWAPCHAIN_IMAGE_COUNT * 2> m_pending_presents;
};

} // namespace wayland
} // namespace wsi
