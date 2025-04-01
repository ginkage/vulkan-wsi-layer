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
 * @file present_id_wayland.cpp
 *
 * @brief Contains the functionality to implement Wayland specific features for present ID extension.
 */
#if VULKAN_WSI_LAYER_EXPERIMENTAL

#include "present_id_wayland.hpp"

namespace wsi
{
namespace wayland
{

presentation_feedback *wsi_ext_present_id_wayland::insert_into_pending_present_feedback_list(
   uint64_t present_id, struct wp_presentation_feedback *feedback_obj)
{
   scoped_mutex lock(m_pending_presents_lock);
   bool ret = m_pending_presents.push_back(presentation_feedback(present_id, feedback_obj, this));
   if (!ret)
   {
      return nullptr;
   }
   return m_pending_presents.back();
}

void wsi_ext_present_id_wayland::remove_from_pending_present_feedback_list(uint64_t present_id)
{
   scoped_mutex lock(m_pending_presents_lock);
   while (m_pending_presents.size() > 0 && m_pending_presents.front()->present_id() <= present_id)
   {
      m_pending_presents.pop_front();
   }
}

} // namespace wayland
} // namespace wsi

#endif // VULKAN_WSI_LAYER_EXPERIMENTAL