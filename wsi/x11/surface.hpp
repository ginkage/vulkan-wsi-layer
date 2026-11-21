/*
 * Copyright (c) 2021 Arm Limited.
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

/** @file
 * @brief Definitions for a x11 WSI Surface
 */

#pragma once
#include <vulkan/vk_icd.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/shm.h>
#include <mutex>
#include "wsi/surface.hpp"
#include "surface_properties.hpp"

namespace wsi
{
namespace x11
{

class surface : public wsi::surface
{
public:
   /**
    * @brief Initialize the WSI surface.
    *
    * @return true on success, false otherwise.
    */
   bool init();

   surface() = delete;
   struct init_parameters;

   surface(const init_parameters &);
   ~surface();

   wsi::surface_properties &get_properties() override;
   util::unique_ptr<swapchain_base> allocate_swapchain(layer::device_private_data &dev_data,
                                                       const VkAllocationCallbacks *allocator) override;
   static util::unique_ptr<surface> make_surface(const util::allocator &allocator, xcb_connection_t *conn,
                                                 xcb_window_t window);

   /**
    * @brief Get the window's size and depth.
    *
    * While size tracking is on (a DRI3 swapchain of this surface exists), the surface keeps the size from
    * the previous query and reads the window's Present ConfigureNotify events itself, without blocking:
    * a query costs a GetGeometry round trip only when the window has been resized since the previous one
    * (or before the first), not on every query - zink's kopper queries the surface capabilities every frame.
    * Without tracking, every query is a round trip.
    */
   bool get_size_and_depth(uint32_t *width, uint32_t *height, int *depth);

   /**
    * @brief Start or stop size tracking. Reference counted, because a swapchain and the one replacing it
    * coexist; the last swapchain to stop turns it off.
    */
   void begin_size_tracking();
   void end_size_tracking();

   xcb_connection_t *get_connection()
   {
      return m_connection;
   }

   xcb_window_t get_window()
   {
      return m_window;
   };

   bool has_shm() const
   {
      return m_has_shm;
   }

private:
   /** @brief Drain the ConfigureNotify queue. True if the window was resized or destroyed meanwhile. */
   bool size_events_pending();

   std::mutex m_size_lock;
   unsigned m_size_tracking_refs = 0;
   /** Present event context selecting ConfigureNotify on the window, and its event queue. */
   uint32_t m_configure_event_id = 0;
   xcb_special_event_t *m_configure_events = nullptr;
   /** m_width/m_height/m_depth are the window's as of the last query, kept current by the events. */
   bool m_size_valid = false;
   bool m_window_destroyed = false;
   uint32_t m_width = 0;
   uint32_t m_height = 0;
   int m_depth = 0;

   xcb_connection_t *m_connection;
   xcb_window_t m_window;
   /** Surface properties specific to the X11 surface. */
   surface_properties properties;

   /** X11 extension capabilities */
   bool m_has_shm = false;
};

} /* namespace x11 */
} /* namespace wsi */
