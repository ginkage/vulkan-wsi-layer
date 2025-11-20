/*
 * Copyright (c) 2017-2025 Arm Limited.
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

#pragma once

#include "wsi/swapchain_base.hpp"
#include "wl_helpers.hpp"

extern "C" {
#include <vulkan/vk_icd.h>
}

#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 0
#endif
#include <wayland-client.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>
#include "surface.hpp"
#include "util/wsialloc/wsialloc.h"
#include "util/custom_allocator.hpp"
#include "wl_object_owner.hpp"

#include <wsi/wsi_alloc_utils.hpp>

#include <wsi/image_backing_memory_external.hpp>
#include <wsi/external_memory.hpp>

namespace wsi
{
namespace wayland
{

class swapchain : public wsi::swapchain_base
{
public:
   explicit swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *allocator,
                      surface &wsi_surface);

   ~swapchain();

   /* TODO: make the buffer destructor a friend? so this can be protected */
   void release_buffer(struct wl_buffer *wl_buffer);

protected:
   /**
    * @brief Platform specific initialization
    *
    * @param      device                  VkDevice object.
    * @param      swapchain_create_info   Pointer to the swapchain create info struct.
    * @param[out] use_presentation_thread Flag indicating if image presentation
    *                                     must happen in a separate thread.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                          bool &use_presentation_thread) override;

   /**
    * @brief Initalize backend specific image factory.
    *
    * @param swapchain_create_info Swapchain create info.
    * @param image_factory Image factory to initalize.
    * @return Vulkan result code.
    */
   VkResult init_image_factory(const VkSwapchainCreateInfoKHR &swapchain_create_info);

   /**
    * @brief Allocates and binds a new swapchain image.
    *
    * @param swapchain_image Swapchain image.
    *
    * @return Returns VK_SUCCESS on success, otherwise an appropriate error code.
    */
   VkResult allocate_and_bind_swapchain_image(swapchain_image &image) override;

   /**
    * @brief Method to present and image
    *
    * It sends the next image for presentation to the presentation engine.
    *
    * @param pending_present Information on the pending present request.
    */
   void present_image(const pending_present_request &pending_present) override;

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
    * @brief Get the image factory used for creating swapchain images.
    *
    * @return Swapchain image factory.
    */
   swapchain_image_factory &get_image_factory() override;

private:
   /**
    * @brief Create a Wayland buffer image for the specified @p image
    *
    * @param image_external_memory Image external memory
    * @return Wayland buffer object or nullptr if there was a failure.
    */
   wayland_owner<wl_buffer> create_wl_buffer(image_backing_memory_external &image_external_memory);

   /**
    * @brief Adds required extensions to the extension list of the swapchain
    *
    * @param device Vulkan device
    * @param swapchain_create_info Swapchain create info
    * @return VK_SUCCESS on success, other result codes on failure
    */
   VkResult add_required_extensions(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info) override;

   /**
    * @brief Create the image creator with required extensions.
    *
    * @param swapchain_create_info VkSwapchainCreateInfoKHR passed by the application.
    * @return If error occurred, returns VkResult, vulkan_image_handle_creator handle otherwise.
    */
   std::variant<VkResult, util::unique_ptr<vulkan_image_handle_creator>> create_image_creator(
      const VkSwapchainCreateInfoKHR &swapchain_create_info);

   struct wl_display *m_display;
   struct wl_surface *m_surface;
   /** Raw pointer to the WSI Surface that this swapchain was created from. The Vulkan specification ensures that the
    * surface is valid until swapchain is destroyed. */
   surface *m_wsi_surface;

   /* The queue on which we dispatch buffer related events, mostly buffer_release */
   struct wl_event_queue *m_buffer_queue;

   /**
    * @brief WSIAllocator instance.
    */
   util::unique_ptr<swapchain_wsialloc_allocator> m_wsi_allocator;

   /**
    * @brief Image factory that is used to create swapchain images.
    */
   swapchain_image_factory m_image_factory;
};

} // namespace wayland
} // namespace wsi
