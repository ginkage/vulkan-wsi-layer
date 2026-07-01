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
 * @file dri3_presenter.cpp
 *
 * @brief DRI3 + Present based (zero-copy) X11 presenter implementation.
 */

#include "dri3_presenter.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include "util/log.hpp"

#include "wsi/swapchain_image_factory.hpp"
#include "wsi/image_backing_memory_external.hpp"
#include "wsi/external_memory.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace wsi
{
namespace x11
{

dri3_presenter::dri3_presenter() = default;

dri3_presenter::~dri3_presenter()
{
   if (m_special_event != nullptr && m_connection != nullptr)
   {
      xcb_unregister_for_special_event(m_connection, m_special_event);
      m_special_event = nullptr;
   }
}

bool dri3_presenter::is_available(xcb_connection_t *connection, surface * /*wsi_surface*/)
{
   const xcb_query_extension_reply_t *dri3_ext = xcb_get_extension_data(connection, &xcb_dri3_id);
   if (dri3_ext == nullptr || !dri3_ext->present)
   {
      WSI_LOG_INFO("DRI3 extension not available, cannot use DRI3 presenter");
      return false;
   }

   const xcb_query_extension_reply_t *present_ext = xcb_get_extension_data(connection, &xcb_present_id);
   if (present_ext == nullptr || !present_ext->present)
   {
      WSI_LOG_INFO("Present extension not available, cannot use DRI3 presenter");
      return false;
   }

   /* Both extensions must answer a version query. */
   xcb_dri3_query_version_cookie_t dri3_cookie = xcb_dri3_query_version(connection, 1, 2);
   xcb_dri3_query_version_reply_t *dri3_reply = xcb_dri3_query_version_reply(connection, dri3_cookie, nullptr);
   if (dri3_reply == nullptr)
   {
      return false;
   }
   free(dri3_reply);

   xcb_present_query_version_cookie_t present_cookie = xcb_present_query_version(connection, 1, 0);
   xcb_present_query_version_reply_t *present_reply =
      xcb_present_query_version_reply(connection, present_cookie, nullptr);
   if (present_reply == nullptr)
   {
      return false;
   }
   free(present_reply);

   return true;
}

VkResult dri3_presenter::init(xcb_connection_t *connection, xcb_window_t window, surface *wsi_surface)
{
   m_connection = connection;
   m_window = window;
   m_wsi_surface = wsi_surface;

   /* Negotiate DRI3 1.2 for pixmap_from_buffers (multi-plane + DRM modifiers). */
   xcb_dri3_query_version_cookie_t cookie = xcb_dri3_query_version(connection, 1, 2);
   xcb_dri3_query_version_reply_t *reply = xcb_dri3_query_version_reply(connection, cookie, nullptr);
   if (reply != nullptr)
   {
      m_have_dri3_1_2 = (reply->major_version > 1) || (reply->major_version == 1 && reply->minor_version >= 2);
      free(reply);
   }

   /* Register for Present Complete/Idle notifications on this window. */
   m_event_id = xcb_generate_id(connection);
   xcb_present_select_input(connection, m_event_id, window,
                            XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY | XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);
   m_special_event = xcb_register_for_special_xge(connection, &xcb_present_id, m_event_id, nullptr);
   if (m_special_event == nullptr)
   {
      WSI_LOG_ERROR("Failed to register for Present special events");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   int flush_result = xcb_flush(connection);
   if (flush_result <= 0)
   {
      WSI_LOG_ERROR("DRI3 presenter xcb_flush failed: result=%d", flush_result);
   }

   return VK_SUCCESS;
}

VkResult dri3_presenter::create_image_resources(swapchain_image &image, x11_image_data *image_data, uint32_t width,
                                                uint32_t height, int depth)
{
   image_data->width = width;
   image_data->height = height;
   image_data->depth = depth;

   auto &backing = swapchain_image_factory::get_backing_memory_from_image<image_backing_memory_external>(image);
   external_memory &mem = backing.get_external_memory();

   const uint64_t modifier = backing.get_modifier();
   const uint32_t num_planes = mem.get_num_planes();
   if (num_planes == 0 || num_planes > 4)
   {
      WSI_LOG_ERROR("DRI3: unexpected plane count %u", num_planes);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   const uint8_t bpp = static_cast<uint8_t>((depth == 24) ? 32 : depth);
   image_data->stride = static_cast<uint32_t>(mem.get_strides()[0]);

   /* DRI3 takes ownership of (and closes) the fds passed to it, but the same fds are needed
    * afterwards for the Vulkan import in import_and_bind(), so hand DRI3 duplicates. */
   int32_t dup_fds[4] = { -1, -1, -1, -1 };
   for (uint32_t i = 0; i < num_planes; i++)
   {
      dup_fds[i] = fcntl(mem.get_buffer_fds()[i], F_DUPFD_CLOEXEC, 0);
      if (dup_fds[i] < 0)
      {
         WSI_LOG_ERROR("DRI3: failed to dup dma-buf fd for plane %u (errno=%d)", i, errno);
         for (uint32_t j = 0; j < i; j++)
         {
            close(dup_fds[j]);
         }
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   xcb_pixmap_t pixmap = xcb_generate_id(m_connection);

   if (m_have_dri3_1_2)
   {
      uint32_t strides[4] = { 0, 0, 0, 0 };
      uint32_t offsets[4] = { 0, 0, 0, 0 };
      for (uint32_t i = 0; i < num_planes; i++)
      {
         strides[i] = static_cast<uint32_t>(mem.get_strides()[i]);
         offsets[i] = mem.get_offsets()[i];
      }

      /* The Mali modifier is often DRM_FORMAT_MOD_INVALID; the patched Xwayland's
       * dri3_pixmap_from_buffer fix handles that on the server side. */
      xcb_dri3_pixmap_from_buffers(m_connection, pixmap, m_window, static_cast<uint8_t>(num_planes),
                                   static_cast<uint16_t>(width), static_cast<uint16_t>(height), strides[0],
                                   offsets[0], strides[1], offsets[1], strides[2], offsets[2], strides[3],
                                   offsets[3], static_cast<uint8_t>(depth), bpp, modifier, dup_fds);
   }
   else
   {
      /* DRI3 1.0: single plane, no modifier. */
      const uint32_t size = image_data->stride * height;
      xcb_dri3_pixmap_from_buffer(m_connection, pixmap, m_window, size, static_cast<uint16_t>(width),
                                  static_cast<uint16_t>(height), static_cast<uint16_t>(image_data->stride),
                                  static_cast<uint8_t>(depth), bpp, dup_fds[0]);
   }

   image_data->pixmap = pixmap;

   int flush_result = xcb_flush(m_connection);
   if (flush_result <= 0)
   {
      WSI_LOG_ERROR("DRI3 presenter xcb_flush failed: result=%d", flush_result);
   }

   return VK_SUCCESS;
}

VkResult dri3_presenter::present_image(x11_image_data *image_data, uint32_t serial, uint64_t target_msc)
{
   if (image_data->pixmap == XCB_PIXMAP_NONE)
   {
      WSI_LOG_ERROR("DRI3: present_image called with no pixmap");
      return VK_ERROR_UNKNOWN;
   }

   /* The base has already CPU-waited on the present fence (wait_on_present_fence=true), so the
    * dma-buf is fully rendered and safe for the server to read.
    *
    * OPTION_COPY (copy mode) asks the server to copy the pixmap into the window rather than flipping
    * our buffer onto the display; the buffer is then released promptly regardless of whether the
    * compositor would have flipped or composited, letting the swapchain recycle on a fixed schedule
    * (see swapchain::present_image). The default OPTION_NONE is truly zero-copy. */
   uint32_t options = m_copy_mode ? XCB_PRESENT_OPTION_COPY : XCB_PRESENT_OPTION_NONE;
   if (m_immediate_mode)
   {
      /* IMMEDIATE: present as soon as possible without waiting for vblank (tearing permitted). */
      options |= XCB_PRESENT_OPTION_ASYNC;
   }
   xcb_present_pixmap(m_connection, m_window, image_data->pixmap, serial, XCB_NONE /* valid */,
                      XCB_NONE /* update */, 0 /* x_off */, 0 /* y_off */, XCB_NONE /* target_crtc */,
                      XCB_NONE /* wait_fence */, XCB_NONE /* idle_fence */, options, target_msc,
                      0 /* divisor */, 0 /* remainder */, 0 /* notifies_len */, nullptr);

   int flush_result = xcb_flush(m_connection);
   if (flush_result <= 0)
   {
      WSI_LOG_ERROR("DRI3 presenter xcb_flush failed: result=%d", flush_result);
      return VK_ERROR_UNKNOWN;
   }

   /* Asynchronous: the swapchain's present_event_thread consumes this pixmap's PresentIdleNotify and
    * recycles the image, so do not block here. */
   return VK_SUCCESS;
}

void dri3_presenter::destroy_image_resources(x11_image_data *image_data)
{
   if (image_data->pixmap != XCB_PIXMAP_NONE)
   {
      xcb_free_pixmap(m_connection, image_data->pixmap);
      image_data->pixmap = XCB_PIXMAP_NONE;
   }
}

} /* namespace x11 */
} /* namespace wsi */
