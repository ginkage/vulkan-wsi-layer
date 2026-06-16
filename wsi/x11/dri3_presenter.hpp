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
 * @file dri3_presenter.hpp
 *
 * @brief DRI3 + Present based (zero-copy) X11 presenter.
 */

#pragma once

#include "x11_presenter.hpp"

#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>

namespace wsi
{

class swapchain_image;

namespace x11
{

class surface;
struct x11_image_data;

/**
 * @brief DRI3 + Present based X11 presenter (zero-copy).
 *
 * Wraps each swapchain image's dma-buf as an X pixmap via DRI3 and presents it with the Present
 * extension, so there is no CPU copy. Used when the X server advertises both DRI3 and Present (e.g.
 * the patched Mali Xwayland); the swapchain falls back to @ref shm_presenter otherwise.
 *
 * Sync is implicit from our side: the swapchain's image factory is configured with
 * wait_on_present_fence=true, so the base CPU-waits for render completion before present_image - the
 * server never samples a half-rendered buffer (matching the no-implicit-sync Mali stack).
 */
class dri3_presenter : public x11_presenter
{
public:
   dri3_presenter();
   ~dri3_presenter() override;

   bool is_available(xcb_connection_t *connection, surface *wsi_surface) override;
   VkResult init(xcb_connection_t *connection, xcb_window_t window, surface *wsi_surface) override;
   VkResult create_image_resources(swapchain_image &image, x11_image_data *image_data, uint32_t width,
                                   uint32_t height, int depth) override;
   VkResult present_image(x11_image_data *image_data, uint32_t serial, uint64_t target_msc) override;
   void destroy_image_resources(x11_image_data *image_data) override;

   /**
    * @brief Special-event queue the swapchain's event thread drains for Present Complete/Idle
    * notifications. Null until @ref init succeeds.
    */
   xcb_special_event_t *get_present_special_event() override
   {
      return m_special_event;
   }

   /**
    * @brief Enable "copy" presentation mode (WSI_X11_DRI3_COPY).
    *
    * In copy mode @ref present_image uses XCB_PRESENT_OPTION_COPY: the X server copies the pixmap into
    * the window immediately rather than flipping/scanning out our dma-buf, so the buffer is released
    * promptly and the swapchain can recycle it on a deterministic schedule (see
    * swapchain::present_image). Costs one server-side (GPU) blit per frame but avoids the bimodal
    * PresentIdleNotify latency of the default zero-copy (OPTION_NONE) path.
    */
   void set_copy_mode(bool enable)
   {
      m_copy_mode = enable;
   }

private:
   xcb_connection_t *m_connection = nullptr;
   xcb_window_t m_window = 0;
   surface *m_wsi_surface = nullptr;

   uint32_t m_event_id = 0;
   xcb_special_event_t *m_special_event = nullptr;

   /** DRI3 >= 1.2: pixmap_from_buffers (multi-plane + DRM modifiers) is available. */
   bool m_have_dri3_1_2 = false;

   /** When true, present with XCB_PRESENT_OPTION_COPY instead of OPTION_NONE (see set_copy_mode). */
   bool m_copy_mode = false;
};

} /* namespace x11 */
} /* namespace wsi */
