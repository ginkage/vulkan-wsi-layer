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
#include "util/wsialloc/wsialloc.h"
#include "wsi/external_memory.hpp"
#include "shm_presenter.hpp"

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

struct x11_image_data
{
   x11_image_data(const VkDevice &device, const util::allocator &allocator)
      : external_mem(device, allocator)
   {
   }

   external_memory external_mem;
   xcb_pixmap_t pixmap = XCB_PIXMAP_NONE;
   std::vector<pending_completion> pending_completions;

   fence_sync present_fence;

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

struct image_creation_parameters
{
   wsialloc_format m_allocated_format;
   util::vector<VkSubresourceLayout> m_image_layout;
   VkExternalMemoryImageCreateInfoKHR m_external_info;
   VkImageDrmFormatModifierExplicitCreateInfoEXT m_drm_mod_info;

   image_creation_parameters(wsialloc_format allocated_format, util::allocator allocator,
                             VkExternalMemoryImageCreateInfoKHR external_info,
                             VkImageDrmFormatModifierExplicitCreateInfoEXT drm_mod_info)
      : m_allocated_format(allocated_format)
      , m_image_layout(allocator)
      , m_external_info(external_info)
      , m_drm_mod_info(drm_mod_info)
   {
   }
};

/**
 * @brief x11 swapchain class.
 *
 * This class is mostly empty, because all the swapchain stuff is handled by the swapchain class,
 * which we inherit. This class only provides a way to create an image and page-flip ops.
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
    * @brief Allocates and binds a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    * @param image             Handle to the image.
    *
    * @return Returns VK_SUCCESS on success, otherwise an appropriate error code.
    */
   VkResult allocate_and_bind_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image) override;

   /**
    * @brief Creates a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    * @param image             Handle to the image.
    *
    * @return If image creation is successful returns VK_SUCCESS, otherwise
    * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_INITIALIZATION_FAILED
    * depending on the error that occurred.
    */
   VkResult create_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image) override;

   /**
    * @brief Method to present and image
    *
    * It sends the next image for presentation to the presentation engine.
    *
    * @param pending_present Information on the pending present request.
    */
   void present_image(const pending_present_request &pending_present) override;

   /**
    * @brief Method to release a swapchain image
    *
    * @param image Handle to the image about to be released.
    */
   void destroy_image(wsi::swapchain_image &image) override;

   /**
    * @brief Sets the present payload for a swapchain image.
    *
    * @param[in] image       The swapchain image for which to set a present payload.
    * @param     queue       A Vulkan queue that can be used for any Vulkan commands needed.
    * @param[in] sem_payload Array of Vulkan semaphores that constitute the payload.
    * @param[in] submission_pnext Chain of pointers to attach to the payload submission.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult image_set_present_payload(swapchain_image &image, VkQueue queue, const queue_submit_semaphores &semaphores,
                                      const void *submission_pnext) override;

   VkResult image_wait_present(swapchain_image &image, uint64_t timeout) override;

   /**
    * @brief Bind image to a swapchain
    *
    * @param device              is the logical device that owns the images and memory.
    * @param bind_image_mem_info details the image we want to bind.
    * @param bind_sc_info        describes the swapchain memory to bind to.
    *
    * @return VK_SUCCESS on success, otherwise on failure VK_ERROR_OUT_OF_HOST_MEMORY or VK_ERROR_OUT_OF_DEVICE_MEMORY
    * can be returned.
    */
   VkResult bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                 const VkBindImageMemorySwapchainInfoKHR *bind_sc_info) override;

   /**
    * @brief Method to check if there are any free images
    *
    * @return true if any images are free, otherwise false.
    */
   bool free_image_found();

   /**
    * @brief Hook for any actions to free up a buffer for acquire
    *
    * @param[in,out] timeout time to wait, in nanoseconds. 0 doesn't block,
    *                        UINT64_MAX waits indefinitely. The timeout should
    *                        be updated if a sleep is required - this can
    *                        be set to 0 if the semaphore is now not expected
    *                        block.
    */
   VkResult get_free_buffer(uint64_t *timeout) override;

   /**
    * @brief Add required swapchain extensions.
    *
    * @param device               The Vulkan device.
    * @param swapchain_create_info Swapchain create info.
    *
    * @return VK_SUCCESS on success, other result codes on failure.
    */
   VkResult add_required_extensions(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info) override;

private:
   VkResult allocate_image(VkImageCreateInfo &image_create_info, x11_image_data *image_data);
   VkResult allocate_wsialloc(VkImageCreateInfo &image_create_info, x11_image_data *image_data,
                              util::vector<wsialloc_format> &importable_formats, wsialloc_format *allocated_format,
                              bool avoid_allocation);

   xcb_connection_t *m_connection;
   xcb_window_t m_window;

   /** Raw pointer to the WSI Surface that this swapchain was created from. The Vulkan specification ensures that the
    * surface is valid until swapchain is destroyed. */
   surface *m_wsi_surface;

   /**
    * @brief Handle to the WSI allocator.
    */
   wsialloc_allocator *m_wsi_allocator;

   /**
    * @brief Presentation strategy for this swapchain.
    */
   std::unique_ptr<shm_presenter> m_shm_presenter;

   /**
    * @brief Image creation parameters used for all swapchain images.
    */
   struct image_creation_parameters m_image_creation_parameters;

   /**
    * @brief Finds what formats are compatible with the requested swapchain image Vulkan Device and Wayland surface.
    *
    * @param      info               The Swapchain image creation info.
    * @param[out] importable_formats A list of formats that can be imported to the Vulkan Device.
    * @param[out] exportable_formats A list of formats that can be exported from the Vulkan Device.
    *
    * @return VK_SUCCESS or VK_ERROR_OUT_OF_HOST_MEMORY
    */
   VkResult get_surface_compatible_formats(const VkImageCreateInfo &info,
                                           util::vector<wsialloc_format> &importable_formats,
                                           util::vector<uint64_t> &exportable_modifers,
                                           util::vector<VkDrmFormatModifierPropertiesEXT> &drm_format_props);

   uint64_t m_send_sbc;
   uint64_t m_target_msc;

   VkPhysicalDeviceMemoryProperties2 m_memory_props;

   void present_event_thread();
   bool m_present_event_thread_run;
   std::thread m_present_event_thread;
   std::mutex m_thread_status_lock;
   std::condition_variable m_thread_status_cond;
   util::ring_buffer<xcb_pixmap_t, 6> m_free_buffer_pool;
};

} /* namespace x11 */
} /* namespace wsi */
