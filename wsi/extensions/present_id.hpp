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
    * @brief Set the present ID for the swapchain.
    *
    * @param value Value to set for the present_id.
    */
   void set_present_id(uint64_t value);

private:
   /**
    * @brief Current present ID for this swapchain.
    */
   uint64_t m_present_id{ 0 };
};

} /* namespace wsi */
