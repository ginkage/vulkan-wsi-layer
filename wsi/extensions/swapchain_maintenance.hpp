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
 * @file swapchain_maintenance.cpp
 *
 * @brief Contains the base class declaration for the VK_EXT_swapchain_maintenance1 extension.
 */
#pragma once

#include <layer/private_data.hpp>
#include <util/custom_allocator.hpp>

#include "wsi_extension.hpp"

namespace wsi
{

/**
 * @brief VK_EXT_swapchain_maintenance1 extension class
 *
 * This class defines the VK_EXT_swapchain_maintenance1 extension
 * features.
 */
class wsi_ext_swapchain_maintenance1 : public wsi_ext
{
public:
   /**
    * @brief The name of the extension.
    */
   WSI_DEFINE_EXTENSION(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);

   /**
    * @brief Constructor for the wsi_ext_swapchain_maintenance1 class.
    */
   wsi_ext_swapchain_maintenance1(const util::allocator &allocator);

   /**
    * @brief Handle presentation mode switching.
    *
    * If VkSwapchainPresentModeInfoEXT is supplied as part of the pNext chain of VkPresentInfoKHR
    * then this function handles switching the swapchains(s)' presentation mode
    * to the one(s) requested in VkSwapchainPresentModeInfoEXT structure.
    *
    * @param swapchain_present_mode presentation mode to switch to.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult handle_switching_presentation_mode(VkPresentModeKHR swapchain_present_mode);

   /**
    * @brief If VkSwapchainPresentModesCreateInfoEXT is supplied as part of the pNext chain of VkSwapchainCreateInfoKHR
    * then this function gets the surface properties and checks whether the present modes are compatible and updates the object's present modes.
    *
    * @param device The device handler.
    * @param swapchain_create_info Pointer to the swapchain create info, used to extract VkSwapchainPresentModesCreateInfoEXT.
    * @param surface The surface handler.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult handle_swapchain_present_modes_create_info(VkDevice device,
                                                       const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                                       VkSurfaceKHR surface);

   /**
    * @brief If VkSwapchainPresentScalingCreateInfoEXT is supplied as part of the pNext chain of VkSwapchainCreateInfoKHR
    * then this function gets the surface properties and checks whether scaling can be done.
    *
    * @param device The device handler.
    * @param swapchain_create_info Pointer to the swapchain create info, used to extract VkSwapchainPresentScalingCreateInfoEXT.
    * @param surface The surface handler.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult handle_scaling_create_info(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                       const VkSurfaceKHR surface);

private:
   /**
    * @brief Possible presentation modes this swapchain is allowed to present with VkSwapchainPresentModesCreateInfoEXT
    */
   util::vector<VkPresentModeKHR> m_present_modes;

   /**
    * @brief Present mode currently being used for this swapchain
    */
   VkPresentModeKHR m_present_mode;
};

} /* namespace wsi */
