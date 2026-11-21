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
 * @brief Implementation of a x11 WSI Surface
 */

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/shm.h>
#include <xcb/present.h>
#include <cstdlib>
#include "surface.hpp"
#include "swapchain.hpp"
#include "surface_properties.hpp"

namespace wsi
{
namespace x11
{

struct surface::init_parameters
{
   const util::allocator &allocator;
   xcb_connection_t *connection;
   xcb_window_t window;
};

surface::surface(const init_parameters &params)
   : wsi::surface()
   , m_connection(params.connection)
   , m_window(params.window)
   , properties(this, params.allocator)
{
   /* The X11 backend fabricates its VkSurfaceKHR handle and never calls down the loader chain, so
    * vkDestroySurfaceKHR must not be forwarded to the ICD for it. */
   m_created_by_icd_calldown = false;
}

surface::~surface()
{
}

/* Set in a ConfigureNotify sent when the window is destroyed (presentproto has no name for it in libxcb). */
#ifndef PresentWindowDestroyed
#define PresentWindowDestroyed (1 << 0)
#endif

void surface::begin_size_tracking()
{
   std::lock_guard<std::mutex> lock(m_size_lock);
   if (m_size_tracking_refs++ > 0)
   {
      return;
   }

   const xcb_query_extension_reply_t *present = xcb_get_extension_data(m_connection, &xcb_present_id);
   if (present == nullptr || !present->present)
   {
      return;
   }

   /* Register the queue before selecting the events, so none of them can reach the application's event queue. */
   m_configure_event_id = xcb_generate_id(m_connection);
   m_configure_events = xcb_register_for_special_xge(m_connection, &xcb_present_id, m_configure_event_id, nullptr);
   if (m_configure_events == nullptr)
   {
      return;
   }
   xcb_discard_reply(m_connection, xcb_present_select_input_checked(m_connection, m_configure_event_id, m_window,
                                                                    XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY)
                                      .sequence);
   /* The next query reads the geometry after the selection, so no resize falls between the two. */
   m_size_valid = false;
   m_window_destroyed = false;
}

void surface::end_size_tracking()
{
   std::lock_guard<std::mutex> lock(m_size_lock);
   if (m_size_tracking_refs == 0 || --m_size_tracking_refs > 0)
   {
      return;
   }

   if (m_configure_events != nullptr)
   {
      xcb_discard_reply(m_connection,
                        xcb_present_select_input_checked(m_connection, m_configure_event_id, m_window, 0).sequence);
      /* Events the server sent before it processed the deselection must still land in our queue: a round
       * trip receives them all before the queue goes. */
      free(xcb_get_input_focus_reply(m_connection, xcb_get_input_focus(m_connection), nullptr));
      xcb_unregister_for_special_event(m_connection, m_configure_events);
      m_configure_events = nullptr;
   }
   m_size_valid = false;
}

bool surface::size_events_pending()
{
   bool resized = false;
   while (xcb_generic_event_t *event = xcb_poll_for_special_event(m_connection, m_configure_events))
   {
      auto *generic = reinterpret_cast<xcb_present_generic_event_t *>(event);
      if (generic->evtype == XCB_PRESENT_CONFIGURE_NOTIFY)
      {
         auto *configure = reinterpret_cast<xcb_present_configure_notify_event_t *>(event);
         if (configure->pixmap_flags & PresentWindowDestroyed)
         {
            m_window_destroyed = true;
         }
         /* A move alone leaves the size as it is: only a new size needs the round trip. */
         if (configure->width != m_width || configure->height != m_height)
         {
            resized = true;
         }
      }
      free(event);
   }
   return resized || m_window_destroyed;
}

bool surface::init()
{
   auto shm_cookie = xcb_shm_query_version_unchecked(m_connection);
   auto shm_reply = xcb_shm_query_version_reply(m_connection, shm_cookie, nullptr);

   m_has_shm = shm_reply != nullptr;
   free(shm_reply);
   return true;
}

bool surface::get_size_and_depth(uint32_t *width, uint32_t *height, int *depth)
{
   std::lock_guard<std::mutex> lock(m_size_lock);
   if (m_configure_events != nullptr && m_size_valid && !size_events_pending())
   {
      *width = m_width;
      *height = m_height;
      *depth = m_depth;
      return true;
   }

   auto cookie = xcb_get_geometry(m_connection, m_window);
   if (auto *geom = xcb_get_geometry_reply(m_connection, cookie, nullptr))
   {
      m_width = *width = static_cast<uint32_t>(geom->width);
      m_height = *height = static_cast<uint32_t>(geom->height);
      m_depth = *depth = static_cast<int>(geom->depth);
      free(geom);
      m_size_valid = m_configure_events != nullptr && !m_window_destroyed;
      return true;
   }
   m_size_valid = false;
   return false;
}

wsi::surface_properties &surface::get_properties()
{
   return properties;
}

util::unique_ptr<swapchain_base> surface::allocate_swapchain(layer::device_private_data &dev_data,
                                                             const VkAllocationCallbacks *allocator)
{
   util::allocator alloc{ dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, allocator };
   return util::unique_ptr<swapchain_base>(alloc.make_unique<swapchain>(dev_data, allocator, *this));
}

util::unique_ptr<surface> surface::make_surface(const util::allocator &allocator, xcb_connection_t *conn,
                                                xcb_window_t window)
{
   xcb_get_geometry_cookie_t test_cookie = xcb_get_geometry(conn, window);
   xcb_generic_error_t *test_error = nullptr;
   xcb_get_geometry_reply_t *test_geom = xcb_get_geometry_reply(conn, test_cookie, &test_error);
   if (test_error)
   {
      free(test_error);
   }
   else if (test_geom)
   {
      free(test_geom);
   }
   else
   {
      WSI_LOG_WARNING("Window 0x%x query returned NULL during surface creation\n", window);
   }

   init_parameters params{ allocator, conn, window };
   auto wsi_surface = allocator.make_unique<surface>(params);
   if (wsi_surface != nullptr)
   {
      if (wsi_surface->init())
      {
         return wsi_surface;
      }
      else
      {
         WSI_LOG_ERROR("Surface init failed for window 0x%x\n", window);
      }
   }
   else
   {
      WSI_LOG_ERROR("Failed to allocate surface for window 0x%x\n", window);
   }
   return nullptr;
}

} /* namespace x11 */
} /* namespace wsi */
