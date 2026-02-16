/*
 * Copyright (c) 2016-2017, 2019, 2021-2022, 2024-2026 Arm Limited.
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

#include <cassert>

#include <wsi/wsi_factory.hpp>
#include <util/helpers.hpp>

#include "private_data.hpp"
#include "surface_api.hpp"

/**
 * @brief Implements vkGetPhysicalDeviceSurfaceCapabilitiesKHR Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) VWL_API_POST
{
   auto &instance = layer::instance_private_data::get(physicalDevice);
   if (instance.should_layer_handle_surface(physicalDevice, surface))
   {
      wsi::surface_properties *props = wsi::get_surface_properties(instance, surface);
      assert(props != nullptr);
      return props->get_surface_capabilities(physicalDevice, pSurfaceCapabilities);
   }

   /* If the layer cannot handle this surface, then necessarily the surface must have been created by the ICDs (or a
    * layer below us.) So it is safe to assume that the ICDs (or layers below us) support VK_KHR_surface and therefore
    * it is safe to can call down. This holds for other entrypoints below.
    */
   return instance.disp.GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
}

/**
 * @brief Implements vkGetPhysicalDeviceSurfaceCapabilities2EXT Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceSurfaceCapabilities2EXT(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                     VkSurfaceCapabilities2EXT *pSurfaceCapabilities) VWL_API_POST
{
   /*
    * To adapt vulkan driver like mesa:panvk which still expose vkGetPhysicalDeviceSurfaceCapabilities2EXT
    * and VK_EXT_display_surface_counter. Vulkan WSI need to implement vkGetPhysicalDeviceSurfaceCapabilities2EXT
    * to handle the compatibility between Vulkan WSI and ICD.
    * Because mesa has different initialization strategy on min/maxImageCount with Vulkan WSI,
    * so if we haven't do like this, app will obtain different value between
    * vkGetPhysicalDeviceSurfaceCapabilities2EXT and vkGetPhysicalDeviceSurfaceCapabilities2KHR.
    */
   auto &instance = layer::instance_private_data::get(physicalDevice);
   if (instance.should_layer_handle_surface(physicalDevice, surface))
   {
      wsi::surface_properties *props = wsi::get_surface_properties(instance, surface);
      assert(props != nullptr);

      /*
       * Firstly, VkSurfaceCapabilities2EXT equal to { VkSurfaceCapabilitiesKHR, VkSurfaceCounterFlagsEXT }
       * So we set the common variable by common function as same as VkSurfaceCapabilitiesKHR,
       * then set supportedSurfaceCounters manually.
       *
       * Secondly, from the vulkan spec, VkSurfaceCapabilities2EXT->pNext must be NULL,
       * so we needn't to deal with the pNext Structure like vkGetPhysicalDeviceSurfaceCapabilities2KHR.
       */
      VkSurfaceCapabilitiesKHR khr_caps = {};
      VkResult res = props->get_surface_capabilities(physicalDevice, &khr_caps);
      if (res != VK_SUCCESS)
      {
         return res;
      }
      pSurfaceCapabilities->minImageCount = khr_caps.minImageCount;
      pSurfaceCapabilities->maxImageCount = khr_caps.maxImageCount;
      pSurfaceCapabilities->currentExtent = khr_caps.currentExtent;
      pSurfaceCapabilities->minImageExtent = khr_caps.minImageExtent;
      pSurfaceCapabilities->maxImageExtent = khr_caps.maxImageExtent;
      pSurfaceCapabilities->maxImageArrayLayers = khr_caps.maxImageArrayLayers;
      pSurfaceCapabilities->supportedTransforms = khr_caps.supportedTransforms;
      pSurfaceCapabilities->currentTransform = khr_caps.currentTransform;
      pSurfaceCapabilities->supportedCompositeAlpha = khr_caps.supportedCompositeAlpha;
      pSurfaceCapabilities->supportedUsageFlags = khr_caps.supportedUsageFlags;
      pSurfaceCapabilities->supportedSurfaceCounters = 0;

      return res;
   }

   return instance.disp.GetPhysicalDeviceSurfaceCapabilities2EXT(physicalDevice, surface, pSurfaceCapabilities);
}

/**
 * @brief Implements vkGetPhysicalDeviceSurfaceCapabilities2KHR Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice,
                                                     const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                     VkSurfaceCapabilities2KHR *pSurfaceCapabilities) VWL_API_POST
{
   auto &instance = layer::instance_private_data::get(physicalDevice);
   if (instance.should_layer_handle_surface(physicalDevice, pSurfaceInfo->surface))
   {
      wsi::surface_properties *props = wsi::get_surface_properties(instance, pSurfaceInfo->surface);
      assert(props != nullptr);

      auto *surf_caps_ext = util::find_extension<VkPresentTimingSurfaceCapabilitiesEXT>(
         VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT, pSurfaceCapabilities);
      if (surf_caps_ext != nullptr)
      {
         TRY_LOG_CALL(props->get_present_timing_surface_caps(physicalDevice, surf_caps_ext));
      }

      auto shared_present_surface_cap_struct = util::find_extension<VkSharedPresentSurfaceCapabilitiesKHR>(
         VK_STRUCTURE_TYPE_SHARED_PRESENT_SURFACE_CAPABILITIES_KHR, pSurfaceCapabilities);
      if (shared_present_surface_cap_struct != nullptr)
      {
         shared_present_surface_cap_struct->sharedPresentSupportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      }
      return props->get_surface_capabilities(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
   }

   return instance.disp.GetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
}

/**
 * @brief Implements vkGetPhysicalDeviceSurfaceFormatsKHR Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                               uint32_t *pSurfaceFormatCount,
                                               VkSurfaceFormatKHR *pSurfaceFormats) VWL_API_POST
{
   auto &instance = layer::instance_private_data::get(physicalDevice);
   if (instance.should_layer_handle_surface(physicalDevice, surface))
   {
      wsi::surface_properties *props = wsi::get_surface_properties(instance, surface);
      assert(props != nullptr);
      return props->get_surface_formats(physicalDevice, pSurfaceFormatCount, pSurfaceFormats);
   }

   return instance.disp.GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount,
                                                           pSurfaceFormats);
}

/**
 * @brief Implements vkGetPhysicalDeviceSurfaceFormats2KHR Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice,
                                                const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                uint32_t *pSurfaceFormatCount,
                                                VkSurfaceFormat2KHR *pSurfaceFormats) VWL_API_POST
{
   auto &instance = layer::instance_private_data::get(physicalDevice);
   if (instance.should_layer_handle_surface(physicalDevice, pSurfaceInfo->surface))
   {
      wsi::surface_properties *props = wsi::get_surface_properties(instance, pSurfaceInfo->surface);
      assert(props != nullptr);
      return props->get_surface_formats(physicalDevice, pSurfaceFormatCount, nullptr, pSurfaceFormats);
   }

   return instance.disp.GetPhysicalDeviceSurfaceFormats2KHR(physicalDevice, pSurfaceInfo, pSurfaceFormatCount,
                                                            pSurfaceFormats);
}

/**
 * @brief Implements vkGetPhysicalDeviceSurfacePresentModesKHR Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                    uint32_t *pPresentModeCount,
                                                    VkPresentModeKHR *pPresentModes) VWL_API_POST
{
   auto &instance = layer::instance_private_data::get(physicalDevice);
   if (instance.should_layer_handle_surface(physicalDevice, surface))
   {
      wsi::surface_properties *props = wsi::get_surface_properties(instance, surface);
      assert(props != nullptr);
      return props->get_surface_present_modes(physicalDevice, surface, pPresentModeCount, pPresentModes);
   }

   return instance.disp.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount,
                                                                pPresentModes);
}

/**
 * @brief Implements vkGetPhysicalDeviceSurfaceSupportKHR Vulkan entrypoint.
 */
VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                                               VkSurfaceKHR surface, VkBool32 *pSupported) VWL_API_POST
{
   auto &instance = layer::instance_private_data::get(physicalDevice);
   if (!instance.should_layer_handle_surface(physicalDevice, surface))
   {
      /* The surface must have been created by a layer below us. */
      return instance.disp.GetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
   }

   *pSupported = queueFamilyIndex == instance.get_best_queue_family(physicalDevice);
   return VK_SUCCESS;
}

VWL_VKAPI_CALL(void)
wsi_layer_vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                              const VkAllocationCallbacks *pAllocator) VWL_API_POST
{
   auto &instance_data = layer::instance_private_data::get(instance);

   instance_data.disp.DestroySurfaceKHR(instance, surface, pAllocator);

   instance_data.remove_surface(
      surface, util::allocator{ instance_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, pAllocator });
}
