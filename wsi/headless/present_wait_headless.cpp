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
 * @file present_wait_headless.cpp
 *
 * @brief Contains the base class declaration for the VK_KHR_present_wait extension.
 */

#include "present_wait_headless.hpp"

namespace wsi
{
namespace headless
{

wsi_ext_present_wait_headless::wsi_ext_present_wait_headless(wsi_ext_present_id &present_id_extension,
                                                             bool present_wait2)
   : wsi_ext_present_wait(present_id_extension, present_wait2)
{
}

VkResult wsi_ext_present_wait_headless::wait_for_update(uint64_t present_id, uint64_t timeout_in_ns)
{
   return m_present_id_ext.wait_for_present_id(present_id, timeout_in_ns);
}

} /* namespace headless */
} /* namespace wsi */