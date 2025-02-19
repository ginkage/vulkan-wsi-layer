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
 * @file present_timing_handler.hpp
 *
 * @brief Contains the functionality to implement features for present timing extension.
 */
#pragma once

#if VULKAN_WSI_LAYER_EXPERIMENTAL

#include <wsi/extensions/present_timing.hpp>

/**
 * @brief Present timing extension class
 *
 * This class implements present timing features declarations that are specific to the Wayland backend.
 */
class wsi_ext_present_timing_wayland : public wsi::wsi_ext_present_timing
{
public:
   static util::unique_ptr<wsi_ext_present_timing_wayland> create(const util::allocator &allocator);

   VkResult get_swapchain_timing_properties(uint64_t &timing_properties_counter,
                                            VkSwapchainTimingPropertiesEXT &timing_properties) override;

private:
   wsi_ext_present_timing_wayland(const util::allocator &allocator);

   /* Allow util::allocator to access the private constructor */
   friend util::allocator;
};

#endif