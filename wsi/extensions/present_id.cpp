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
namespace wsi
{

void wsi_ext_present_id::mark_delivered(uint64_t present_id)
{
   /* Stale reads are acceptable as we only care that the ID is increasing */
   if (present_id > m_last_delivered_id.load(std::memory_order_relaxed))
   {
      std::unique_lock lock(m_mutex);
      m_last_delivered_id.store(present_id, std::memory_order_relaxed);
   }
   m_present_id_changed.notify_all();
}

bool wsi_ext_present_id::wait_for_present_id(uint64_t present_id, uint64_t timeout_in_ns)
{
   if (m_last_delivered_id.load() >= present_id)
   {
      return VK_SUCCESS;
   }

   std::unique_lock lock(m_mutex);
   try
   {
      return m_present_id_changed.wait_for(lock, std::chrono::nanoseconds(timeout_in_ns),
                                           [&]() { return m_last_delivered_id.load() >= present_id; });
   }
   catch (const std::system_error &e)
   {
      WSI_LOG_ERROR("Failed to wait for conditional variable. Code: %d, message: %s\n", e.code().value(), e.what());
   }

   return false;
}

uint64_t wsi_ext_present_id::get_last_delivered_present_id() const
{
   return m_last_delivered_id.load();
}

};
