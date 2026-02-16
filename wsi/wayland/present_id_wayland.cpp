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
 * @file present_id_wayland.cpp
 *
 * @brief Contains the functionality to implement Wayland specific features for present ID extension.
 */

#include <util/custom_mutex.hpp>
#include "present_id_wayland.hpp"

namespace wsi
{
namespace wayland
{

void wsi_ext_present_id_wayland::mark_delivered(uint64_t present_id)
{
   wsi_ext_present_id::mark_delivered(present_id);
   remove_from_pending_present_feedback_list(present_id);
}

void wsi_ext_present_id_wayland::mark_buffer_release(uint32_t image_index)
{
   const auto present_id = get_present_id_from_image_index(image_index);
   if (present_id > 0)
   {
      mark_delivered(present_id);
   }
}

uint64_t wsi_ext_present_id_wayland::get_present_id_from_image_index(uint32_t image_index)
{
   util::unique_lock<util::mutex> lock(m_pending_presents_lock);
   if (!lock)
   {
      WSI_LOG_ERROR("Failed to acquire pending presents lock in insert_into_pending_present_feedback_list.\n");
      abort();
   }

   auto feedback = m_pending_presents.find(
      [image_index](const presentation_feedback &candidate) { return candidate.get_image_index() == image_index; });

   return feedback != nullptr ? feedback->get_present_id() : 0;
}

presentation_feedback *wsi_ext_present_id_wayland::insert_into_pending_present_feedback_list(
   uint64_t present_id, uint32_t image_index, struct wp_presentation_feedback *feedback_obj)
{
   util::unique_lock<util::mutex> lock(m_pending_presents_lock);
   if (!lock)
   {
      WSI_LOG_ERROR("Failed to acquire pending presents lock in insert_into_pending_present_feedback_list.\n");
      abort();
   }

   /* We should not be replacing pending entries. This most likely has happened due to events not being triggered
    * that would discard or mark the pending present request as completed which could be an inidcation of a bug somewhere. */
   assert(m_pending_presents.size() != m_pending_presents.capacity());

   bool ret = m_pending_presents.push_back(presentation_feedback(feedback_obj, this, present_id, image_index));
   if (!ret)
   {
      return nullptr;
   }
   return m_pending_presents.back();
}

void wsi_ext_present_id_wayland::remove_from_pending_present_feedback_list(uint64_t present_id)
{
   util::unique_lock<util::mutex> lock(m_pending_presents_lock);
   if (!lock)
   {
      WSI_LOG_ERROR("Failed to acquire pending presents lock in remove_from_pending_present_feedback_list.\n");
      abort();
   }
   while (m_pending_presents.size() > 0 && m_pending_presents.front()->get_present_id() <= present_id)
   {
      m_pending_presents.pop_front();
   }
}

} // namespace wayland
} // namespace wsi
