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
 * @file present_id_wayland.hpp
 *
 * @brief Contains the functionality to implement Wayland specific features for present id extension.
 */
#pragma once

#if VULKAN_WSI_LAYER_EXPERIMENTAL

#include <wsi/extensions/present_id.hpp>
#include <util/ring_buffer.hpp>
#include "surface_properties.hpp"
#include "wp_presentation_feedback.hpp"
#include <mutex>

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
   presentation_feedback *insert_into_pending_present_feedback_list(uint64_t present_id,
                                                                    struct wp_presentation_feedback *feedback_obj);

   /**
    * @brief Remove a present id from the pending present id list.
    */
   void remove_from_pending_present_feedback_list(uint64_t present_id);

private:
   /**
    * @brief Mutex for synchronising accesses to the pending present id list.
    */
   std::mutex m_pending_presents_lock;

   /**
    * @brief Stores the presentation feedbacks that have been queued.
    */
   util::ring_buffer<presentation_feedback, wsi::surface_properties::MAX_SWAPCHAIN_IMAGE_COUNT * 2> m_pending_presents;
};

} // namespace wayland
} // namespace wsi

#endif // VULKAN_WSI_LAYER_EXPERIMENTAL