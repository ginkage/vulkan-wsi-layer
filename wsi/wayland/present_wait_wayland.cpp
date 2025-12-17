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
 * @file present_wait_wayland.cpp
 *
 * @brief Contains the base class declaration for the VK_KHR_present_wait extension.
 */

#include "present_wait_wayland.hpp"
#include "wl_helpers.hpp"

#include <limits.h>

namespace wsi
{
namespace wayland
{

wsi_ext_present_wait_wayland::wsi_ext_present_wait_wayland(wsi_ext_present_id &present_id_extension, bool present_wait2)
   : wsi_ext_present_wait(present_id_extension, present_wait2)
{
}

void wsi_ext_present_wait_wayland::set_wayland_dispatcher(wl_display *display, struct wl_event_queue *queue)
{
   /* These objects shouldn't be set twice */
   assert(m_display == nullptr);
   assert(m_queue == nullptr);

   m_display = display;
   m_queue = queue;
}

VkResult wsi_ext_present_wait_wayland::wait_for_update(uint64_t present_id, uint64_t timeout_in_ns)
{
   assert(m_display != nullptr);
   assert(m_queue != nullptr);

   do
   {
      VkResult error_state = m_present_id_ext.get_error_state();
      if (error_state != VK_SUCCESS)
      {
         return error_state;
      }
      else if (m_present_id_ext.get_last_delivered_present_id() >= present_id)
      {
         return VK_SUCCESS;
      }

      int delay_in_ms = 0;
      if (timeout_in_ns != UINT64_MAX)
      {
         const uint64_t min_delay_in_ns = std::min(static_cast<uint64_t>(5000000ULL /* 5ms */), timeout_in_ns);
         const uint64_t min_delay_in_ms = min_delay_in_ns / 1000000ULL;

         timeout_in_ns -= min_delay_in_ns;
         delay_in_ms = static_cast<int>(std::min<uint64_t>(min_delay_in_ms, static_cast<uint64_t>(INT_MAX)));
      }
      else
      {
         /* For the infinite wait, do a repeated timeout of 5s to avoid deadlock where
          * the queue could be dispatched by another thread and no other events would be
          * dispatched from the queue anymore from the application until this call returns. */
         delay_in_ms = 5000;
      }

      /* timeout in dispatch_queue is not an issue as it could have been dispatched by another thread
       * at the same time. */
      if (dispatch_queue(m_display, m_queue, delay_in_ms) == -1 /*error but not timeout*/)
      {
         return VK_ERROR_SURFACE_LOST_KHR;
      }

   } while (timeout_in_ns > 0);

   return VK_TIMEOUT;
}

} /* namespace wayland */
} /* namespace wsi */
