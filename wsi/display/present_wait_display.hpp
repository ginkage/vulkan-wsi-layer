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
 * @file present_wait_headless.hpp
 *
 * @brief Contains the base class declaration for the VK_KHR_present_wait extension.
 */

#pragma once

#include <wsi/extensions/present_wait.hpp>

namespace wsi
{

namespace display
{

/**
 * @brief Present wait extension class
 *
 * This class defines the present wait extension
 * implementation.
 */
class wsi_ext_present_wait_display : public wsi::wsi_ext_present_wait
{
public:
   /**
    * @brief Constructs present ID class.
    *
    * @param present_id_extension Present ID extension that this class will use to query
    * the last delivered present ID for the swapchain. This extension pointer must outlive
    * this class.
    */
   wsi_ext_present_wait_display(wsi_ext_present_id &present_id_extension);

private:
   /**
    * @brief Backend specific implementation that will wait for the present ID to
    *        be updated.
    * @param present_id Present ID value to wait for.
    * @param timeout_in_ns Timeout in nanoseconds.
    * @return VK_SUCCESS if present ID value was updated to be above or equal to the requested wait value.
    *         Other error codes otherwise.
    */
   VkResult wait_for_update(uint64_t present_id, uint64_t timeout_in_ns) override;
};

} /* namespace wayland */
} /* namespace wsi */
