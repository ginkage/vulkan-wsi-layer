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

/**
 * @file swapchain.hpp
 *
 * @brief Contains the class definition for a headless swapchain.
 */

#pragma once

extern "C" {
#include <vulkan/vk_icd.h>
}

#include <wsi/swapchain_base.hpp>

namespace wsi
{
namespace headless
{
/**
 * @brief Headless swapchain class.
 *
 * This class is mostly empty, because all the swapchain stuff is handled by the swapchain class,
 * which we inherit. This class only provides a way to create an image and page-flip ops.
 */
class swapchain : public wsi::swapchain_base
{
public:
   explicit swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator);

   ~swapchain();

protected:
   /**
    * @brief Platform specific init
    */
   VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                          bool &use_presentation_thread) override;

   /**
    * @brief Initalize backend specific image factory.
    *
    * @param swapchain_create_info Swapchain create info.
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
    * @brief Get the image factory used for creating swapchain images.
    *
    * @return Swapchain image factory.
    */
   swapchain_image_factory &get_image_factory() override;

private:
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

   /**
    * @brief Image factory that is used to create swapchain images.
    */
   swapchain_image_factory m_image_factory;
};

} /* namespace headless */
} /* namespace wsi */
