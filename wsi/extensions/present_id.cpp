/*
 * Copyright (c) 2024-2025 Arm Limited.
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
 * @file present_id.cpp
 *
 * @brief Contains the implementation for the VK_KHR_present_id extension.
 */

#include "present_id.hpp"
#include <system_error>
#include <cassert>

namespace wsi
{

void wsi_ext_present_id::mark_delivered(uint64_t present_id)
{
   /* Stale reads are acceptable as we only care that the ID is increasing */
   if (present_id > m_last_delivered_id.load(std::memory_order_relaxed))
   {
      util::unique_lock lock(m_mutex);
      if (!lock)
      {
         WSI_LOG_ERROR("Failed to acquire mutex lock in mark_delivered.\n");
         abort();
      }
      m_last_delivered_id.store(present_id, std::memory_order_relaxed);
   }
   m_present_state_changed.notify_all();
}

void wsi_ext_present_id::set_error_state(VkResult error_code)
{
   util::unique_lock lock(m_mutex);
   if (!lock)
   {
      WSI_LOG_ERROR("Failed to acquire mutex lock in set_error_state.\n");
      abort();
   }
   m_error_state.store(error_code);
   m_present_state_changed.notify_all();
}

VkResult wsi_ext_present_id::get_error_state()
{
   return m_error_state;
}

VkResult wsi_ext_present_id::wait_for_present_id(uint64_t present_id, uint64_t timeout_in_ns)
{
   if (m_last_delivered_id.load() >= present_id)
   {
      return VK_SUCCESS;
   }

   try
   {
      util::unique_lock lock(m_mutex);
      if (!lock)
      {
         return VK_ERROR_UNKNOWN;
      }
      /* Move ownership into a std::unique_lock so we can call the
       * std::condition_variable APIs, which accept only
       * std::unique_lock<std::mutex>.  The mutex is already held by
       * util::unique_lock, which acquired it via try_lock() and converts any
       * std::system_error into a simple ‘false’.  We then release the
       * util::unique_lock to avoid double‑unlocking.
       */
      std::unique_lock<std::mutex> wait_lock(lock.native_mutex(), std::adopt_lock);
      lock.release();
      if (timeout_in_ns == UINT64_MAX)
      {
         /* Infinite wait */
         m_present_state_changed.wait(wait_lock, [&]() {
            return (m_last_delivered_id.load() >= present_id || m_error_state.load() != VK_SUCCESS);
         });

         /* The condition can either return when present_id condition has been reached or there has been an error */
         return m_error_state;
      }
      else
      {
         /* Note: With very long timeouts it is possible that the clock in condition_variable will overflow.
          * This will result in wait_for immediately returning with a failed result. Considering the
          * duration needed to overflow the clock, we can probably ignore this. */
         bool wait_success =
            m_present_state_changed.wait_for(wait_lock, std::chrono::nanoseconds(timeout_in_ns), [&]() {
               return (m_last_delivered_id.load() >= present_id || m_error_state.load() != VK_SUCCESS);
            });

         if (!wait_success)
         {
            /* We timed out */
            return VK_TIMEOUT;
         }

         /* The condition can either return when present_id condition has been reached or there has been an error */
         return m_error_state;
      }
   }
   catch (const std::system_error &e)
   {
      WSI_LOG_ERROR("Failed to wait for conditional variable. Code: %d, message: %s\n", e.code().value(), e.what());
   }

   /* The mutex lock has failed */
   return VK_ERROR_SURFACE_LOST_KHR;
}

uint64_t wsi_ext_present_id::get_last_delivered_present_id() const
{
   return m_last_delivered_id.load();
}

};
