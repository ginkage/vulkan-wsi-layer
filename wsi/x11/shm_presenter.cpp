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
 * @file shm_presenter.cpp
 *
 * @brief MIT-SHM based X11 presenter implementation.
 */

#include "shm_presenter.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include "util/log.hpp"

#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <future>
#include <vector>
#include <chrono>
#include <cmath>
#ifdef ENABLE_ARM_NEON
#include <arm_neon.h>
#endif
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <xcb/sync.h>
#include <xcb/randr.h>

namespace wsi
{
namespace x11
{

static constexpr uint32_t THREADING_PIXEL_THRESHOLD = 400 * 400;
static constexpr uint32_t MAX_WORKER_THREADS = 8u;
static constexpr uint32_t SIMD_VECTOR_SIZE = 4;
static constexpr uint32_t LOOP_UNROLL_BOUNDARY = 3;
static constexpr int SHM_PERMISSIONS = 0666;
static constexpr uint32_t GC_COLOR_MASK = XCB_GC_BACKGROUND | XCB_GC_FOREGROUND;

shm_presenter::shm_presenter()
   : m_sync_pending(false)
   , m_frame_interval(std::chrono::microseconds(16667))
   , m_refresh_rate_hz(60.0)
{
}

shm_presenter::~shm_presenter()
{
   if (m_sync_pending)
   {
      ensure_sync_completion();
   }
   cleanup_fence_sync();
}

bool shm_presenter::is_aligned(const void *ptr, size_t alignment)
{
   return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

#ifdef ENABLE_ARM_NEON
bool shm_presenter::are_pointers_neon_aligned(const void *src, void *dst)
{
   constexpr size_t NEON_ALIGNMENT = 16;
   return is_aligned(src, NEON_ALIGNMENT) && is_aligned(dst, NEON_ALIGNMENT);
}
#endif

double shm_presenter::get_window_refresh_rate()
{
   double detected_refresh_rate = 60.0;
   bool found_refresh_rate = false;

   Display *display = XOpenDisplay(nullptr);
   if (!display)
   {
      WSI_LOG_WARNING("Failed to open X11 display, using 60Hz default");
      return detected_refresh_rate;
   }

   Window root = DefaultRootWindow(display);

   int event_base, error_base;
   if (XRRQueryExtension(display, &event_base, &error_base))
   {
      XRRScreenResources *resources = XRRGetScreenResources(display, root);
      if (resources)
      {
         int active_crtc_count = 0;
         for (int i = 0; i < resources->ncrtc; i++)
         {
            XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, resources, resources->crtcs[i]);
            if (crtc_info && crtc_info->mode != None && crtc_info->noutput > 0)
            {
               active_crtc_count++;
            }
            if (crtc_info)
               XRRFreeCrtcInfo(crtc_info);
         }

         bool single_screen = (active_crtc_count == 1);

         int window_x = 0, window_y = 0;

         if (!single_screen)
         {
            xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data;
            xcb_window_t root_window = screen->root;

            xcb_translate_coordinates_cookie_t translate_cookie =
               xcb_translate_coordinates(m_connection, m_window, root_window, 0, 0);
            xcb_translate_coordinates_reply_t *translate_reply =
               xcb_translate_coordinates_reply(m_connection, translate_cookie, nullptr);

            if (translate_reply)
            {
               window_x = translate_reply->dst_x;
               window_y = translate_reply->dst_y;
               free(translate_reply);
            }
         }

         for (int i = 0; i < resources->ncrtc; i++)
         {
            XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(display, resources, resources->crtcs[i]);
            if (crtc_info && crtc_info->mode != None && crtc_info->noutput > 0)
            {
               bool window_on_crtc = false;

               if (single_screen)
               {
                  window_on_crtc = true;
               }
               else
               {
                  window_on_crtc = (window_x >= crtc_info->x && window_x < crtc_info->x + (int)crtc_info->width &&
                                    window_y >= crtc_info->y && window_y < crtc_info->y + (int)crtc_info->height);
               }

               for (int j = 0; j < resources->nmode; j++)
               {
                  XRRModeInfo *mode = &resources->modes[j];
                  if (mode->id == crtc_info->mode)
                  {
                     double refresh = (double)mode->dotClock / ((double)mode->hTotal * (double)mode->vTotal);

                     if (window_on_crtc)
                     {
                        detected_refresh_rate = refresh;
                        found_refresh_rate = true;
                        break;
                     }
                     else if (!found_refresh_rate)
                     {
                        detected_refresh_rate = refresh;
                        found_refresh_rate = true;
                     }
                     break;
                  }
               }

               if (crtc_info)
                  XRRFreeCrtcInfo(crtc_info);
               if (found_refresh_rate && window_on_crtc)
                  break;
            }
         }

         XRRFreeScreenResources(resources);
      }
      else
      {
         WSI_LOG_WARNING("Failed to get XRandR screen resources");
      }
   }
   else
   {
      WSI_LOG_WARNING("XRandR extension not available");
   }

   XCloseDisplay(display);

   if (!found_refresh_rate)
   {
      WSI_LOG_WARNING("Could not detect refresh rate, using 60Hz default");
   }

   // Reasonable bounds for display refresh rates
   if (detected_refresh_rate < 30.0 || detected_refresh_rate > 240.0)
   {
      WSI_LOG_WARNING("Detected refresh rate %.2f Hz seems invalid, using 60Hz", detected_refresh_rate);
      detected_refresh_rate = 60.0;
   }

   return detected_refresh_rate;
}

void shm_presenter::detect_refresh_rate()
{
   double detected_refresh_rate = get_window_refresh_rate();

   m_refresh_rate_hz = detected_refresh_rate;
   auto interval_us = static_cast<long>(1000000.0 / detected_refresh_rate);
   m_frame_interval = std::chrono::microseconds(interval_us);
}

void shm_presenter::precompute_scaling_lut(uint32_t gpu_width, uint32_t display_width)
{
   if (m_last_gpu_width == gpu_width && m_last_display_width == display_width)
   {
      return;
   }

   m_scaling_lut.reserve(display_width);
   m_scaling_lut.resize(display_width);

   for (uint32_t dst_x = 0; dst_x < display_width; dst_x++)
   {
      uint32_t src_x = (dst_x * gpu_width) / display_width;
      if (src_x >= gpu_width)
         src_x = gpu_width - 1;
      m_scaling_lut[dst_x] = src_x;
   }

   m_last_gpu_width = gpu_width;
   m_last_display_width = display_width;
}

#ifdef ENABLE_ARM_NEON
void shm_presenter::copy_pixels_simd(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                                     uint32_t dst_width, uint32_t height)
{
   if (m_scaling_lut.empty() || m_scaling_lut[dst_width - 1] == dst_width - 1)
   {
      for (uint32_t row = 0; row < height; row++)
      {
         const uint32_t *src_row = src_pixels + (row * src_stride_pixels);
         uint32_t *dst_row = dst_pixels + (row * dst_width);

         uint32_t x = 0;
         bool use_aligned_simd = are_pointers_neon_aligned(&src_row[0], &dst_row[0]);

         if (use_aligned_simd)
         {
            for (; x + LOOP_UNROLL_BOUNDARY < dst_width; x += SIMD_VECTOR_SIZE)
            {
               uint32x4_t pixels = vld1q_u32(&src_row[x]);
               vst1q_u32(&dst_row[x], pixels);
            }
         }
         else
         {
            for (; x + LOOP_UNROLL_BOUNDARY < dst_width; x += SIMD_VECTOR_SIZE)
            {
               uint8x16_t bytes = vld1q_u8(reinterpret_cast<const uint8_t *>(&src_row[x]));
               vst1q_u8(reinterpret_cast<uint8_t *>(&dst_row[x]), bytes);
            }
         }

         for (; x < dst_width; x++)
         {
            dst_row[x] = src_row[x];
         }
      }
   }
   else
   {
      copy_pixels_scalar(src_pixels, dst_pixels, src_stride_pixels, dst_width, height);
   }
}
#endif

void shm_presenter::copy_pixels_scalar(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                                       uint32_t dst_width, uint32_t height)
{
   uint32_t src_width = src_stride_pixels;

   if (src_width == dst_width &&
       (m_scaling_lut.empty() || (dst_width > 0 && m_scaling_lut[dst_width - 1] == dst_width - 1)))
   {
      size_t copy_size = dst_width * sizeof(uint32_t);
      for (uint32_t row = 0; row < height; row++)
      {
         const uint32_t *src_row = src_pixels + (row * src_stride_pixels);
         uint32_t *dst_row = dst_pixels + (row * dst_width);

         if (row + 1 < height)
         {
            __builtin_prefetch(src_row + src_stride_pixels, 0, 3);
         }

         memcpy(dst_row, src_row, copy_size);
      }
      return;
   }

   for (uint32_t row = 0; row < height; row++)
   {
      const uint32_t *src_row = src_pixels + (row * src_stride_pixels);
      uint32_t *dst_row = dst_pixels + (row * dst_width);

      if (row + 1 < height)
      {
         __builtin_prefetch(src_row + src_stride_pixels, 0, 3);
      }

      uint32_t dst_x = 0;
      for (; dst_x + LOOP_UNROLL_BOUNDARY < dst_width; dst_x += SIMD_VECTOR_SIZE)
      {
         dst_row[dst_x] = src_row[m_scaling_lut[dst_x]];
         dst_row[dst_x + 1] = src_row[m_scaling_lut[dst_x + 1]];
         dst_row[dst_x + 2] = src_row[m_scaling_lut[dst_x + 2]];
         dst_row[dst_x + 3] = src_row[m_scaling_lut[dst_x + 3]];
      }

      for (; dst_x < dst_width; dst_x++)
      {
         dst_row[dst_x] = src_row[m_scaling_lut[dst_x]];
      }
   }
}

void shm_presenter::copy_pixels_threaded(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                                         uint32_t dst_width, uint32_t height)
{
   if (!src_pixels || !dst_pixels || dst_width == 0 || height == 0)
   {
      return;
   }

   const uint32_t total_pixels = dst_width * height;

   if (total_pixels > THREADING_PIXEL_THRESHOLD)
   {
      const uint32_t num_threads = std::min(std::thread::hardware_concurrency(), MAX_WORKER_THREADS);
      if (num_threads > 1)
      {
         const uint32_t rows_per_thread = height / num_threads;
         std::vector<std::future<void>> futures;
         futures.reserve(num_threads);

         try
         {
            for (uint32_t t = 0; t < num_threads; t++)
            {
               uint32_t start_row = t * rows_per_thread;
               uint32_t end_row = (t == num_threads - 1) ? height : (t + 1) * rows_per_thread;

               if (start_row >= height)
                  break;
               if (end_row > height)
                  end_row = height;

               futures.emplace_back(std::async(std::launch::async, [=]() {
                  try
                  {
                     const uint32_t *thread_src = src_pixels + (start_row * src_stride_pixels);
                     uint32_t *thread_dst = dst_pixels + (start_row * dst_width);
                     uint32_t thread_height = end_row - start_row;

                     if (thread_height > 0)
                     {
#ifdef ENABLE_ARM_NEON
                        copy_pixels_simd(thread_src, thread_dst, src_stride_pixels, dst_width, thread_height);
#else
                        copy_pixels_scalar(thread_src, thread_dst, src_stride_pixels, dst_width, thread_height);
#endif
                     }
                  }
                  catch (const std::exception &e)
                  {
                     WSI_LOG_ERROR("Thread pixel copy failed with exception: %s", e.what());
                     m_thread_error_occurred.store(true, std::memory_order_release);
                  }
                  catch (...)
                  {
                     WSI_LOG_ERROR("Thread pixel copy failed with unknown exception");
                     m_thread_error_occurred.store(true, std::memory_order_release);
                  }
               }));
            }

            for (auto &future : futures)
            {
               if (future.valid())
               {
                  future.wait();
               }
            }

            if (m_thread_error_occurred.load(std::memory_order_acquire))
            {
               std::lock_guard<std::mutex> lock(m_error_recovery_mutex);
               WSI_LOG_ERROR("Thread errors detected, falling back to single-threaded processing");
               m_thread_error_occurred.store(false, std::memory_order_release);
               copy_pixels_optimized_single_thread(src_pixels, dst_pixels, src_stride_pixels, dst_width, height);
            }
         }
         catch (...)
         {
            std::lock_guard<std::mutex> lock(m_error_recovery_mutex);
            WSI_LOG_ERROR("Threading setup failed, falling back to single-threaded processing");
            copy_pixels_optimized_single_thread(src_pixels, dst_pixels, src_stride_pixels, dst_width, height);
         }
         return;
      }
   }

   copy_pixels_optimized_single_thread(src_pixels, dst_pixels, src_stride_pixels, dst_width, height);
}

void shm_presenter::copy_pixels_optimized_single_thread(const uint32_t *src_pixels, uint32_t *dst_pixels,
                                                        uint32_t src_stride_pixels, uint32_t dst_width, uint32_t height)
{
#ifdef ENABLE_ARM_NEON
   copy_pixels_simd(src_pixels, dst_pixels, src_stride_pixels, dst_width, height);
#else
   copy_pixels_scalar(src_pixels, dst_pixels, src_stride_pixels, dst_width, height);
#endif
}

void shm_presenter::copy_pixels_optimized(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                                          uint32_t dst_width, uint32_t height)
{
   if (src_stride_pixels == dst_width && m_scaling_lut.empty())
   {
      const size_t copy_size = dst_width * height * sizeof(uint32_t);
      std::memcpy(dst_pixels, src_pixels, copy_size);
      return;
   }

   copy_pixels_threaded(src_pixels, dst_pixels, src_stride_pixels, dst_width, height);
}

void shm_presenter::start_async_sync()
{
   if (m_sync_pending)
   {
      return;
   }

   m_pending_sync_cookie = xcb_get_geometry(m_connection, m_window);
   m_sync_pending = true;
}

bool shm_presenter::check_pending_sync()
{
   if (!m_sync_pending)
   {
      return true;
   }

   xcb_generic_error_t *error = nullptr;
   xcb_get_geometry_reply_t *sync_reply = xcb_get_geometry_reply(m_connection, m_pending_sync_cookie, &error);

   if (sync_reply)
   {
      free(sync_reply);
      m_sync_pending = false;
      return true;
   }
   else if (error)
   {
      free(error);
      m_sync_pending = false;
      return false;
   }

   return false;
}

void shm_presenter::ensure_sync_completion()
{
   if (!m_sync_pending)
   {
      return;
   }

   xcb_generic_error_t *error = nullptr;
   xcb_get_geometry_reply_t *sync_reply = xcb_get_geometry_reply(m_connection, m_pending_sync_cookie, &error);

   if (sync_reply)
   {
      free(sync_reply);
   }
   else if (error)
   {
      free(error);
   }

   m_sync_pending = false;
}

bool shm_presenter::init_fence_sync()
{
   if (m_fence_available)
   {
      return true;
   }

   const xcb_query_extension_reply_t *sync_ext = xcb_get_extension_data(m_connection, &xcb_sync_id);
   if (!sync_ext || !sync_ext->present)
   {
      WSI_LOG_WARNING("XSync extension not available, falling back to geometry-based sync");
      return false;
   }

   m_presentation_fence = xcb_generate_id(m_connection);
   xcb_void_cookie_t fence_cookie = xcb_sync_create_fence_checked(m_connection, m_window, m_presentation_fence, 0);

   xcb_generic_error_t *fence_error = xcb_request_check(m_connection, fence_cookie);
   if (fence_error)
   {
      WSI_LOG_WARNING("Failed to create XSync fence: error %d, falling back to geometry-based sync",
                      fence_error->error_code);
      free(fence_error);
      if (m_presentation_fence != XCB_NONE)
      {
         m_presentation_fence = XCB_NONE;
      }
      return false;
   }

   xcb_flush(m_connection);

   xcb_get_input_focus_cookie_t sync_cookie = xcb_get_input_focus(m_connection);
   xcb_get_input_focus_reply_t *sync_reply = xcb_get_input_focus_reply(m_connection, sync_cookie, nullptr);

   if (sync_reply)
   {
      free(sync_reply);
      m_fence_available = true;
      return true;
   }
   else
   {
      WSI_LOG_WARNING("Failed to synchronize XSync fence setup, falling back to geometry-based sync");
      if (m_presentation_fence != XCB_NONE)
      {
         xcb_sync_destroy_fence(m_connection, m_presentation_fence);
         m_presentation_fence = XCB_NONE;
      }
      return false;
   }
}

void shm_presenter::cleanup_fence_sync()
{
   if (m_presentation_fence != XCB_NONE)
   {
      xcb_sync_destroy_fence(m_connection, m_presentation_fence);
      m_presentation_fence = XCB_NONE;
   }
   m_fence_available = false;
}

void shm_presenter::wait_for_presentation_fence()
{
   if (!m_fence_available || m_presentation_fence == XCB_NONE)
   {
      return;
   }

   xcb_sync_await_fence(m_connection, 1, &m_presentation_fence);
   xcb_flush(m_connection);
   xcb_sync_reset_fence(m_connection, m_presentation_fence);
   xcb_flush(m_connection);
}

void shm_presenter::trigger_presentation_fence()
{
   if (!m_fence_available || m_presentation_fence == XCB_NONE)
   {
      return;
   }

   xcb_sync_trigger_fence(m_connection, m_presentation_fence);
   xcb_flush(m_connection);
}

void shm_presenter::cache_x11_formats()
{
   const xcb_setup_t *setup = xcb_get_setup(m_connection);
   xcb_format_iterator_t format_iter = xcb_setup_pixmap_formats_iterator(setup);

   for (; format_iter.rem; xcb_format_next(&format_iter))
   {
      xcb_format_t *format = format_iter.data;
      m_depth_to_bpp_cache[format->depth] = format->bits_per_pixel;
   }
}

uint8_t shm_presenter::get_bits_per_pixel_for_depth(int depth)
{
   auto it = m_depth_to_bpp_cache.find(depth);
   if (it != m_depth_to_bpp_cache.end())
   {
      return it->second;
   }

   return (depth == 24) ? 32 : depth;
}

VkResult shm_presenter::init(xcb_connection_t *connection, xcb_window_t window, surface *wsi_surface)
{
   m_connection = connection;
   m_window = window;
   m_wsi_surface = wsi_surface;

   detect_refresh_rate();

   cache_x11_formats();

   VkResult result = create_graphics_context();
   if (result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Failed to create graphics context for SHM presentation");
      return result;
   }

   init_fence_sync();
   init_xrandr_events();

   return VK_SUCCESS;
}

VkResult shm_presenter::create_image_resources(x11_image_data *image_data, uint32_t width, uint32_t height, int depth)
{
   image_data->width = width;
   image_data->height = height;
   image_data->depth = depth;

   uint8_t bits_per_pixel = (depth == 24) ? 32 : depth;
   image_data->stride = width * (bits_per_pixel / 8);

   size_t shm_size = image_data->stride * height;
   image_data->shm_size = shm_size;

   image_data->shm_id = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | SHM_PERMISSIONS);
   if (image_data->shm_id < 0)
   {
      WSI_LOG_ERROR("Failed to create shared memory segment of size %zu", shm_size);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   image_data->shm_addr = shmat(image_data->shm_id, nullptr, 0);
   if (image_data->shm_addr == (void *)-1)
   {
      WSI_LOG_ERROR("Failed to attach shared memory segment");
      shmctl(image_data->shm_id, IPC_RMID, nullptr);
      image_data->shm_id = -1;
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   image_data->shm_seg = xcb_generate_id(m_connection);
   xcb_shm_attach(m_connection, image_data->shm_seg, image_data->shm_id, 0);

   image_data->shm_id_alt = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | SHM_PERMISSIONS);
   if (image_data->shm_id_alt < 0)
   {
      WSI_LOG_ERROR("Failed to create alternate shared memory segment");
   }
   else
   {
      image_data->shm_addr_alt = shmat(image_data->shm_id_alt, nullptr, 0);
      if (image_data->shm_addr_alt == (void *)-1)
      {
         WSI_LOG_ERROR("Failed to attach alternate shared memory segment");
         shmctl(image_data->shm_id_alt, IPC_RMID, nullptr);
         image_data->shm_id_alt = -1;
         image_data->shm_addr_alt = nullptr;
      }
      else
      {
         image_data->shm_seg_alt = xcb_generate_id(m_connection);
         xcb_shm_attach(m_connection, image_data->shm_seg_alt, image_data->shm_id_alt, 0);
      }
   }

   xcb_flush(m_connection);

   xcb_get_input_focus_cookie_t sync_cookie = xcb_get_input_focus(m_connection);
   xcb_get_input_focus_reply_t *sync_reply = xcb_get_input_focus_reply(m_connection, sync_cookie, nullptr);
   if (sync_reply)
   {
      free(sync_reply);
   }

   shmctl(image_data->shm_id, IPC_RMID, nullptr);
   if (image_data->shm_id_alt >= 0)
   {
      shmctl(image_data->shm_id_alt, IPC_RMID, nullptr);
   }

   return VK_SUCCESS;
}

bool shm_presenter::init_xrandr_events()
{
   const xcb_query_extension_reply_t *randr_ext = xcb_get_extension_data(m_connection, &xcb_randr_id);
   if (!randr_ext || !randr_ext->present)
   {
      WSI_LOG_WARNING("XRandR extension not available, using initial refresh rate detection only");
      return false;
   }

   m_xrandr_event_base = randr_ext->first_event;

   xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data;
   xcb_window_t root = screen->root;

   xcb_randr_select_input(m_connection, root, XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE | XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);

   xcb_flush(m_connection);

   m_xrandr_events_available = true;
   return true;
}

void shm_presenter::check_window_events()
{
   if (!m_xrandr_events_available)
   {
      return;
   }

   xcb_generic_event_t *event;
   while ((event = xcb_poll_for_event(m_connection)) != nullptr)
   {
      uint8_t event_type = event->response_type & 0x7f;

      if (event_type == XCB_CONFIGURE_NOTIFY)
      {
         xcb_configure_notify_event_t *config = (xcb_configure_notify_event_t *)event;
         if (config->window == m_window)
         {
            m_refresh_rate_changed.store(true, std::memory_order_release);
         }
      }

      xcb_window_t target_window = m_window;
      if (event_type == XCB_CONFIGURE_NOTIFY)
      {
         xcb_configure_notify_event_t *config = (xcb_configure_notify_event_t *)event;
         target_window = config->window;
      }

      xcb_send_event(m_connection, false, target_window, 0, (char *)event);
      xcb_flush(m_connection);
      free(event);
   }
}

void shm_presenter::handle_refresh_rate_change()
{
   if (!m_refresh_rate_changed.exchange(false, std::memory_order_acq_rel))
   {
      return;
   }

   double new_refresh_rate = get_window_refresh_rate();
   double rate_diff = std::abs(new_refresh_rate - m_refresh_rate_hz);

   if (rate_diff > 2.0)
   {
      WSI_LOG_INFO("Monitor change detected: %.2f Hz -> %.2f Hz", m_refresh_rate_hz, new_refresh_rate);
      m_refresh_rate_hz = new_refresh_rate;
      auto interval_us = static_cast<long>(1000000.0 / new_refresh_rate);
      m_frame_interval = std::chrono::microseconds(interval_us);
   }
}

VkResult shm_presenter::present_image(x11_image_data *image_data, uint32_t /*serial*/)
{
   check_window_events();
   handle_refresh_rate_change();

   if (m_fence_available && !m_first_frame)
   {
      wait_for_presentation_fence();
   }
   else if (!m_fence_available)
   {
      if (m_sync_pending)
      {
         ensure_sync_completion();
      }
   }
   m_first_frame = false;

   xcb_flush(m_connection);

   image_data->use_alt_buffer = !image_data->use_alt_buffer;
   xcb_shm_seg_t active_seg =
      image_data->use_alt_buffer && image_data->shm_seg_alt != XCB_NONE ? image_data->shm_seg_alt : image_data->shm_seg;
   void *active_addr = image_data->use_alt_buffer && image_data->shm_addr_alt != nullptr ? image_data->shm_addr_alt :
                                                                                           image_data->shm_addr;

   if (active_addr && image_data->shm_size > 0)
   {
      if (image_data->external_mem.is_host_visible())
      {
         void *mapped_memory = nullptr;
         if (image_data->external_mem.map_host_memory(&mapped_memory) == VK_SUCCESS && mapped_memory != nullptr)
         {
            const auto &vulkan_layout = image_data->external_mem.get_host_layout();
            size_t source_stride = vulkan_layout.rowPitch;
            size_t dest_stride = image_data->stride;
            size_t source_offset = vulkan_layout.offset;

            size_t bytes_per_pixel = dest_stride / image_data->width;
            size_t gpu_pixels_per_row = image_data->width;
            size_t display_pixels_per_row = image_data->width;

            char *src_base = (char *)mapped_memory + source_offset;

            if (gpu_pixels_per_row != display_pixels_per_row)
            {
               precompute_scaling_lut(gpu_pixels_per_row, display_pixels_per_row);
            }
            else
            {
               m_scaling_lut.clear();
            }

            char *dst_base = (char *)active_addr;

            if (bytes_per_pixel == 4)
            {
               uint32_t *src_pixels = (uint32_t *)src_base;
               uint32_t *dst_pixels = (uint32_t *)dst_base;
               uint32_t src_stride_pixels = source_stride / bytes_per_pixel;

               copy_pixels_optimized(src_pixels, dst_pixels, src_stride_pixels, display_pixels_per_row,
                                     image_data->height);
            }
            else
            {
               for (uint32_t row = 0; row < image_data->height; row++)
               {
                  char *src_row = src_base + (row * source_stride);
                  char *dst_row = dst_base + (row * dest_stride);
                  size_t copy_size = std::min(source_stride, dest_stride);
                  std::memcpy(dst_row, src_row, copy_size);
               }
            }
         }
         else
         {
            return VK_ERROR_UNKNOWN;
         }
      }
      else
      {
         WSI_LOG_ERROR("GPU memory not available for SHM presentation");
         return VK_ERROR_DEVICE_LOST;
      }
   }
   else
   {
      return VK_ERROR_UNKNOWN;
   }

   xcb_shm_put_image(m_connection, m_window, m_gc, image_data->width, image_data->height, 0, 0, image_data->width,
                     image_data->height, 0, 0, image_data->depth, XCB_IMAGE_FORMAT_Z_PIXMAP, 0, active_seg, 0);

   auto current_time = std::chrono::steady_clock::now();
   auto time_since_last = std::chrono::duration_cast<std::chrono::microseconds>(current_time - m_last_frame_time);

   if (m_last_frame_time.time_since_epoch().count() > 0 && time_since_last < m_frame_interval)
   {
      auto sleep_time = m_frame_interval - time_since_last;

      if (sleep_time > std::chrono::microseconds(500))
      {
         auto conservative_sleep = sleep_time - std::chrono::microseconds(200);
         std::this_thread::sleep_for(conservative_sleep);
      }

      auto target_time = m_last_frame_time + m_frame_interval;
      while (std::chrono::steady_clock::now() < target_time)
      {
         std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      current_time = std::chrono::steady_clock::now();
   }
   m_last_frame_time = current_time;

   if (m_fence_available)
   {
      trigger_presentation_fence();
   }
   else
   {
      start_async_sync();
   }

   xcb_flush(m_connection);

   return VK_SUCCESS;
}
void shm_presenter::destroy_image_resources(x11_image_data *image_data)
{
   if (image_data->shm_seg != XCB_NONE)
   {
      xcb_shm_detach(m_connection, image_data->shm_seg);
      image_data->shm_seg = XCB_NONE;
   }

   if (image_data->shm_seg_alt != XCB_NONE)
   {
      xcb_shm_detach(m_connection, image_data->shm_seg_alt);
      image_data->shm_seg_alt = XCB_NONE;
   }

   if (image_data->shm_addr && image_data->shm_addr != (void *)-1)
   {
      shmdt(image_data->shm_addr);
      image_data->shm_addr = nullptr;
   }

   if (image_data->shm_addr_alt && image_data->shm_addr_alt != (void *)-1)
   {
      shmdt(image_data->shm_addr_alt);
      image_data->shm_addr_alt = nullptr;
   }

   image_data->shm_id = -1;
   image_data->shm_id_alt = -1;
   image_data->shm_size = 0;
   image_data->use_alt_buffer = false;
}

bool shm_presenter::is_available(xcb_connection_t * /*connection*/, surface *wsi_surface)
{
   return wsi_surface->has_shm();
}

VkResult shm_presenter::create_graphics_context()
{
   m_gc = xcb_generate_id(m_connection);

   uint32_t values[] = { 0, 0 };

   uint32_t mask = GC_COLOR_MASK;

   xcb_create_gc(m_connection, m_gc, m_window, mask, values);

   xcb_flush(m_connection);

   return VK_SUCCESS;
}

} /* namespace x11 */
} /* namespace wsi */