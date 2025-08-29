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
 * @file shm_presenter.hpp
 *
 * @brief MIT-SHM based X11 presenter implementation.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <mutex>
#include <xcb/sync.h>

namespace wsi
{
namespace x11
{

class surface;
struct x11_image_data;

class shm_presenter
{
public:
   ~shm_presenter();

   shm_presenter();

   VkResult init(xcb_connection_t *connection, xcb_window_t window, surface *wsi_surface);

   VkResult create_image_resources(x11_image_data *image_data, uint32_t width, uint32_t height, int depth);

   VkResult present_image(x11_image_data *image_data, uint32_t serial);

   void destroy_image_resources(x11_image_data *image_data);

   bool is_available(xcb_connection_t *connection, surface *wsi_surface);

private:
   xcb_connection_t *m_connection = nullptr;
   xcb_window_t m_window = 0;
   surface *m_wsi_surface = nullptr;
   xcb_gcontext_t m_gc = XCB_NONE;

   std::vector<uint32_t> m_scaling_lut;
   uint32_t m_last_gpu_width = 0;
   uint32_t m_last_display_width = 0;

   xcb_get_geometry_cookie_t m_pending_sync_cookie;
   bool m_sync_pending = false;

   xcb_sync_fence_t m_presentation_fence = XCB_NONE;
   bool m_fence_available = false;
   bool m_first_frame = true;

   std::unordered_map<int, uint8_t> m_depth_to_bpp_cache;

   std::chrono::steady_clock::time_point m_last_frame_time;
   std::chrono::microseconds m_frame_interval;
   double m_refresh_rate_hz;

   std::atomic<bool> m_thread_error_occurred{ false };
   std::mutex m_error_recovery_mutex;

   bool m_xrandr_events_available = false;
   int m_xrandr_event_base = 0;
   std::atomic<bool> m_refresh_rate_changed{ false };

   VkResult create_graphics_context();

   void precompute_scaling_lut(uint32_t gpu_width, uint32_t display_width);
   void copy_pixels_optimized(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                              uint32_t dst_width, uint32_t height);
   void copy_pixels_threaded(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                             uint32_t dst_width, uint32_t height);
   void copy_pixels_optimized_single_thread(const uint32_t *src_pixels, uint32_t *dst_pixels,
                                            uint32_t src_stride_pixels, uint32_t dst_width, uint32_t height);
#ifdef ENABLE_ARM_NEON
   void copy_pixels_simd(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                         uint32_t dst_width, uint32_t height);
#endif
   void copy_pixels_scalar(const uint32_t *src_pixels, uint32_t *dst_pixels, uint32_t src_stride_pixels,
                           uint32_t dst_width, uint32_t height);

   void start_async_sync();
   bool check_pending_sync();
   void ensure_sync_completion();

   bool init_fence_sync();
   void cleanup_fence_sync();
   void wait_for_presentation_fence();
   void trigger_presentation_fence();

   void cache_x11_formats();
   uint8_t get_bits_per_pixel_for_depth(int depth);

   bool is_aligned(const void *ptr, size_t alignment);
#ifdef ENABLE_ARM_NEON
   bool are_pointers_neon_aligned(const void *src, void *dst);
#endif
   void detect_refresh_rate();
   double get_window_refresh_rate();

   bool init_xrandr_events();
   void check_window_events();
   void handle_refresh_rate_change();
};

} /* namespace x11 */
} /* namespace wsi */