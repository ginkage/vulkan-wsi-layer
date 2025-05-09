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
 * @file present_id.hpp
 *
 * @brief Contains the base class declaration for the VK_KHR_present_id extension.
 */

#pragma once

#include <util/custom_allocator.hpp>
#include <util/macros.hpp>
#include <util/log.hpp>
#include <atomic>
#include <condition_variable>

#include "wsi_extension.hpp"

namespace wsi
{

/**
 * @brief Present ID extension class
 *
 * This class defines the present ID extension
 * features.
 */
class wsi_ext_present_id : public wsi_ext
{
public:
   /**
    * @brief The name of the extension.
    */
   WSI_DEFINE_EXTENSION(VK_KHR_PRESENT_ID_EXTENSION_NAME);

   /**
    * @brief Marks the given present ID delivered (i.e. its image has been displayed).
    */
   void mark_delivered(uint64_t present_id);

   /**
    * @brief Waits for present ID to be above or equal to the @p value.
    *
    * @param value The value to wait for.
    * @param timeout_in_ns Timeout in nanoseconds.
    * @return true The present ID value is equal or higher than @p value
    * @return false The present ID is lower than @p value and timeout occured
    */
   bool wait_for_present_id(uint64_t present_id, uint64_t timeout_in_ns);

   /**
    * @brief Get the last delivered present ID value.
    * @return Present ID
    */
   uint64_t get_last_delivered_present_id() const;

private:
   /**
    * @brief Most recently delivered present ID for this swapchain.
    */
   std::atomic<uint64_t> m_last_delivered_id{ 0 };

   /**
    * @brief Conditional variable that notifies whenever present ID value has changed.
    */
   std::condition_variable m_present_id_changed;

   /**
    * @brief Mutex for m_present_id_changed conditional variable.
    */
   std::mutex m_mutex;
};

} /* namespace wsi */
