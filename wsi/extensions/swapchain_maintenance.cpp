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
 * @brief Contains the implementation for VK_EXT_swapchain_maintenance1 extension.
 */

#include <layer/private_data.hpp>
#include <wsi/wsi_factory.hpp>
#include <wsi/surface_properties.hpp>

#include "swapchain_maintenance.hpp"
#include "present_id.hpp"

namespace wsi
{

wsi_ext_swapchain_maintenance1::wsi_ext_swapchain_maintenance1(const util::allocator &allocator)
   : m_present_modes(allocator)
{
}

VkResult wsi_ext_swapchain_maintenance1::handle_switching_presentation_mode(VkPresentModeKHR swapchain_present_mode)
{
   assert(m_present_modes.size() > 0);
   auto it = std::find_if(m_present_modes.begin(), m_present_modes.end(),
                          [swapchain_present_mode](VkPresentModeKHR p) { return p == swapchain_present_mode; });
   if (it == m_present_modes.end())
   {
      WSI_LOG_ERROR("unable to switch presentation mode");
      return VK_ERROR_SURFACE_LOST_KHR;
   }
   m_present_mode = swapchain_present_mode;
   return VK_SUCCESS;
}

VkResult wsi_ext_swapchain_maintenance1::handle_swapchain_present_modes_create_info(
   VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info, VkSurfaceKHR surface)
{
   const auto *swapchain_present_modes_create_info = util::find_extension<VkSwapchainPresentModesCreateInfoEXT>(
      VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT, swapchain_create_info->pNext);
   if (swapchain_present_modes_create_info != nullptr)
   {
      if (!m_present_modes.try_resize(swapchain_present_modes_create_info->presentModeCount))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      layer::device_private_data &device_data = layer::device_private_data::get(device);
      auto &instance = device_data.instance_data;
      wsi::surface_properties *props = wsi::get_surface_properties(instance, surface);
      assert(props != nullptr);
      for (uint32_t i = 0; i < swapchain_present_modes_create_info->presentModeCount; i++)
      {
         auto res =
            props->is_compatible_present_modes(m_present_mode, swapchain_present_modes_create_info->pPresentModes[i]);
         if (!res)
         {
            WSI_LOG_ERROR("present modes incompatible");
            return VK_ERROR_INITIALIZATION_FAILED;
         }
         m_present_modes[i] = swapchain_present_modes_create_info->pPresentModes[i];
      }
   }
   return VK_SUCCESS;
}

VkResult wsi_ext_swapchain_maintenance1::handle_scaling_create_info(
   VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info, const VkSurfaceKHR surface)
{

   auto present_scaling_create_info = util::find_extension<VkSwapchainPresentScalingCreateInfoEXT>(
      VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT, swapchain_create_info);
   if (present_scaling_create_info != nullptr)
   {
      auto &device_data = layer::device_private_data::get(device);
      auto &instance = device_data.instance_data;

      VkSurfacePresentScalingCapabilitiesEXT scaling_capabilities = {};
      wsi::surface_properties *props = wsi::get_surface_properties(instance, surface);
      assert(props != nullptr);
      props->get_surface_present_scaling_and_gravity(&scaling_capabilities);

      if (((present_scaling_create_info->scalingBehavior != 0) &&
           ((scaling_capabilities.supportedPresentScaling & present_scaling_create_info->scalingBehavior) == 0)) ||
          ((present_scaling_create_info->presentGravityX != 0) &&
           ((scaling_capabilities.supportedPresentGravityX & present_scaling_create_info->presentGravityX) == 0)) ||
          ((present_scaling_create_info->presentGravityY != 0) &&
           ((scaling_capabilities.supportedPresentGravityY & present_scaling_create_info->presentGravityY) == 0)))
      {
         return VK_ERROR_INITIALIZATION_FAILED;
      }
   }
   return VK_SUCCESS;
}

};