/*
 * Copyright (c) 2017-2019, 2021-2022 Arm Limited.
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
 * @file swapchain.hpp
 *
 * @brief Contains the class definition for a x11 swapchain.
 */

#pragma once

#include "wsi/swapchain_base.hpp"

extern "C" {
#include <vulkan/vk_icd.h>
#include <vulkan/vulkan.h>
}

#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 0
#endif

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <optional>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xproto.h>

#include "surface.hpp"
#include "wsi/external_memory.hpp"
#include "wsi/swapchain_image_factory.hpp"
#include "wsi/vulkan_image_handle_creator.hpp"
#include "wsi/image_backing_memory_device.hpp"
#include "wsi/image_backing_memory_external.hpp"
#include "wsi/wsi_alloc_utils.hpp"
#include "wsi/extensions/image_create_info_extension.hpp"
#include "shm_presenter.hpp"
#include "dri3_presenter.hpp"
#include "x11_presenter.hpp"

namespace wsi
{
namespace x11
{

struct pending_completion
{
   uint32_t serial;
   uint64_t present_id;
   std::optional<std::chrono::steady_clock::time_point> timestamp;
};

/**
 * @brief Backend-specific data attached to each swapchain image.
 *
 * Holds the host-visible image memory (via @ref external_memory) and the X11 shared-memory resources
 * used to present the image.
 */
struct x11_image_data : public swapchain_image_data
{
   x11_image_data(const VkDevice &dev, const util::allocator &allocator)
      : external_mem(dev, allocator)
      , device(dev)
   {
   }

   external_memory external_mem;
   xcb_pixmap_t pixmap = XCB_PIXMAP_NONE;
   std::vector<pending_completion> pending_completions;

   xcb_shm_seg_t shm_seg = XCB_NONE;
   int shm_id = -1;
   void *shm_addr = nullptr;
   size_t shm_size = 0;

   xcb_shm_seg_t shm_seg_alt = XCB_NONE;
   int shm_id_alt = -1;
   void *shm_addr_alt = nullptr;
   bool use_alt_buffer = false;

   uint32_t width = 0;
   uint32_t height = 0;
   uint32_t stride = 0;
   int depth = 0;

   void *cpu_buffer = nullptr;
   size_t cpu_buffer_size = 0;

   VkDevice device = VK_NULL_HANDLE;
   layer::device_private_data *device_data = nullptr;
};

/**
 * @brief Image create-info extension that forces LINEAR tiling.
 *
 * The X11 SHM backend allocates host-visible memory and reads the rendered image on the CPU, so the
 * swapchain images must be linearly tiled.
 */
class linear_tiling_extension : public image_create_info_extension
{
public:
   VkResult extend_image_create_info(VkImageCreateInfo *image_create_info) override
   {
      image_create_info->tiling = VK_IMAGE_TILING_LINEAR;
      return VK_SUCCESS;
   }
};

/**
 * @brief x11 swapchain class.
 *
 * Presents GPU-rendered images by copying them into an X11 shared-memory segment and blitting them
 * with MIT-SHM (see @ref shm_presenter). The image memory is host-visible so the CPU can read it.
 */
class swapchain : public wsi::swapchain_base
{
public:
   explicit swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator,
                      surface &wsi_surface);

   ~swapchain();

protected:
   /**
    * @brief Platform specific init
    */
   VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                          bool &use_presentation_thread) override;

   /**
    * @brief Allocates and binds host-visible memory to a swapchain image and sets up its SHM resources.
    */
   VkResult allocate_and_bind_swapchain_image(swapchain_image &image) override;

   /**
    * @brief Returns the factory that creates this swapchain's (linear, host-visible) images.
    */
   swapchain_image_factory &get_image_factory() override;

   /**
    * @brief Method to present an image.
    */
   void present_image(const pending_present_request &pending_present) override;

   /**
    * @brief Hook for any actions to free up a buffer for acquire.
    */
   VkResult get_free_buffer(uint64_t *timeout) override;

   /**
    * @brief Add required swapchain extensions.
    */
   VkResult add_required_extensions(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info) override;

private:
   /**
    * @brief Configure the image factory (linear handle creator + host-visible binding).
    */
   VkResult init_image_factory(const VkSwapchainCreateInfoKHR &swapchain_create_info);

   /**
    * @brief Method to check if there are any free images.
    */
   bool free_image_found();

   void present_event_thread();

   xcb_connection_t *m_connection;
   xcb_window_t m_window;

   /** Raw pointer to the WSI Surface that this swapchain was created from. The Vulkan specification ensures that the
    * surface is valid until swapchain is destroyed. */
   surface *m_wsi_surface;

   /**
    * @brief Presentation strategy for this swapchain (DRI3 zero-copy, or SHM fallback).
    */
   std::unique_ptr<x11_presenter> m_presenter;

   /** @brief True when @ref m_presenter is the DRI3 strategy (dma-buf images); false for SHM. */
   bool m_use_dri3 = false;

   /** @brief wsialloc allocator backing the DRI3 dma-buf images (null in SHM mode). */
   util::unique_ptr<swapchain_wsialloc_allocator> m_wsi_allocator;

   /** @brief DRI3 Present special-event queue (owned by m_presenter); null in SHM mode. */
   xcb_special_event_t *m_present_special_event = nullptr;

   /**
    * @brief Factory producing the swapchain's (linear, host-visible) images.
    */
   swapchain_image_factory m_image_factory;

   uint64_t m_send_sbc;
   uint64_t m_target_msc;

   /** @brief Most recent vsync count from PresentCompleteNotify, for FIFO target_msc pacing. */
   uint64_t m_last_present_msc;

   bool m_present_event_thread_run;
   std::thread m_present_event_thread;
   std::mutex m_thread_status_lock;
   std::condition_variable m_thread_status_cond;
   util::ring_buffer<xcb_pixmap_t, 16> m_free_buffer_pool;
};

} /* namespace x11 */
} /* namespace wsi */
