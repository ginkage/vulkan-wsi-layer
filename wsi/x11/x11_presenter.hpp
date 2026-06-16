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
 * @file x11_presenter.hpp
 *
 * @brief Abstract presentation strategy for the X11 backend.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

namespace wsi
{

class swapchain_image;

namespace x11
{

class surface;
struct x11_image_data;

/**
 * @brief Abstract presentation strategy for the X11 backend.
 *
 * Two implementations exist: @ref shm_presenter (MIT-SHM CPU copy - the universal fallback) and
 * @ref dri3_presenter (DRI3 + Present zero-copy, used when the X server supports it). The swapchain
 * holds one of these behind this interface and chooses which at init_platform time.
 */
class x11_presenter
{
public:
   virtual ~x11_presenter() = default;

   /** @brief Whether this strategy can be used with the given connection/surface. */
   virtual bool is_available(xcb_connection_t *connection, surface *wsi_surface) = 0;

   /** @brief One-time setup (graphics context / event registration / fences). */
   virtual VkResult init(xcb_connection_t *connection, xcb_window_t window, surface *wsi_surface) = 0;

   /**
    * @brief Create the per-image presentation resources (SHM segments, or a DRI3 pixmap).
    *
    * Populates @p image_data 's width/height/depth. @p image gives access to the image's backing
    * memory, which the DRI3 path uses to wrap the dma-buf as a pixmap; the SHM path ignores it.
    */
   virtual VkResult create_image_resources(swapchain_image &image, x11_image_data *image_data, uint32_t width,
                                           uint32_t height, int depth) = 0;

   /**
    * @brief Present an image (the base has already waited for render completion). @p target_msc is
    * the vsync count to present at for FIFO pacing (0 = as soon as possible); SHM ignores it.
    */
   virtual VkResult present_image(x11_image_data *image_data, uint32_t serial, uint64_t target_msc) = 0;

   /** @brief Tear down the per-image resources created by @ref create_image_resources. */
   virtual void destroy_image_resources(x11_image_data *image_data) = 0;

   /**
    * @brief Present special-event queue for the swapchain's event thread to drain (DRI3 Idle/Complete
    * notifications). Returns null for strategies without one (SHM).
    */
   virtual xcb_special_event_t *get_present_special_event()
   {
      return nullptr;
   }
};

} /* namespace x11 */
} /* namespace wsi */
