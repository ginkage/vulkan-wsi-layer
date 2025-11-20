/*
 * Copyright (c) 2024-2025 Arm Limited.
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
 * @brief Contains the class definition for a display swapchain.
 */

#pragma once

extern "C" {
#include <vulkan/vk_icd.h>
}

#include "drm_display.hpp"
#include "surface.hpp"
#include <util/wsialloc/wsialloc.h>
#include <wsi/external_memory.hpp>

#include <wsi/image_backing_memory_external.hpp>
#include <wsi/wsi_alloc_utils.hpp>

namespace wsi
{

namespace display
{

/**
 * @brief Display swapchain class.
 */
class swapchain : public wsi::swapchain_base
{
public:
   swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator, surface &wsi_surface);

   virtual ~swapchain();

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

protected:
   /**
    * @brief Get the image factory used for creating swapchain images.
    *
    * @return Swapchain image factory.
    */
   swapchain_image_factory &get_image_factory() override;

private:
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
   virtual VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
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
    * @brief Create a framebuffer for display
    *
    * @param image_external_memory Image external memory
    * @param out_fb_id The framebuffer ID will be written here
    * @return Vulkan result code
    */
   VkResult create_framebuffer(image_backing_memory_external &image_external_memory, uint32_t &out_fb_id);

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

   drm_display_mode *m_display_mode;

   /**
    * @brief WSIAllocator instance.
    */
   util::unique_ptr<swapchain_wsialloc_allocator> m_wsi_allocator;

   /**
    * @brief Image factory that is used to create swapchain images.
    */
   swapchain_image_factory m_image_factory;
};
} /* namespace display */

} /* namespace wsi*/
