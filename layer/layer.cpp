/*
 * Copyright (c) 2016-2026 Arm Limited.
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
#include <cstdio>
#include <cstring>
#include <array>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include "layer/calibrated_timestamps_api.hpp"
#include "present_timing_api.hpp"
#include "present_wait_api.hpp"
#include "private_data.hpp"
#include "surface_api.hpp"
#include "swapchain_api.hpp"
#include "swapchain_maintenance_api.hpp"
#include "util/extension_list.hpp"
#include "util/custom_allocator.hpp"
#include "wsi/wsi_factory.hpp"
#include "wsi/extensions/present_timing.hpp"
#include "util/log.hpp"
#include "util/macros.hpp"
#include "util/helpers.hpp"

#define VK_LAYER_API_VERSION VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION)

namespace layer
{
struct features
{
   struct writable_layer_feature
   {
      VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *swapchain_maintenance1 = nullptr;
      VkPhysicalDevicePresentWaitFeaturesKHR *present_wait = nullptr;
      VkPhysicalDevicePresentWait2FeaturesKHR *present_wait2 = nullptr;
      VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *swapchain_compression = nullptr;
      VkPhysicalDevicePresentIdFeaturesKHR *present_id = nullptr;
      VkPhysicalDevicePresentId2FeaturesKHR *present_id2 = nullptr;
      VkPhysicalDevicePresentTimingFeaturesEXT *present_timing = nullptr;
      VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT *fifo_latest_ready = nullptr;
#if VULKAN_WSI_LAYER_EXPERIMENTAL
      VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *multisampled_render_to_swapchain = nullptr;
#endif

      writable_layer_feature() = default;

      writable_layer_feature(void *p_next)
      {
         swapchain_maintenance1 = util::find_extension<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, p_next);
         present_wait = util::find_extension<VkPhysicalDevicePresentWaitFeaturesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, p_next);
         present_wait2 = util::find_extension<VkPhysicalDevicePresentWait2FeaturesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR, p_next);
         swapchain_compression = util::find_extension<VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT, p_next);
         present_id = util::find_extension<VkPhysicalDevicePresentIdFeaturesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, p_next);
         present_id2 = util::find_extension<VkPhysicalDevicePresentId2FeaturesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR, p_next);
         present_timing = util::find_extension<VkPhysicalDevicePresentTimingFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT, p_next);
         fifo_latest_ready = util::find_extension<VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT, p_next);
#if VULKAN_WSI_LAYER_EXPERIMENTAL
         multisampled_render_to_swapchain =
            util::find_extension<VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT>(
               VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT, p_next);
#endif

         /* We only need to initialize swapchain_maintenance1 and present_wait because these values are
          * used in the post_query after the ICD call to derive the final value.
          */
         if (swapchain_maintenance1 != nullptr)
         {
            swapchain_maintenance1->swapchainMaintenance1 = VK_FALSE;
         }

         if (present_wait != nullptr)
         {
            present_wait->presentWait = VK_FALSE;
         }
      }
   };

   struct storage
   {
      VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maintenance1 = {};
      VkPhysicalDevicePresentWaitFeaturesKHR present_wait = {};
      VkPhysicalDevicePresentWait2FeaturesKHR present_wait2 = {};
      VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT swapchain_compression = {};
      VkPhysicalDevicePresentIdFeaturesKHR present_id = {};
      VkPhysicalDevicePresentId2FeaturesKHR present_id2 = {};
      VkPhysicalDevicePresentTimingFeaturesEXT present_timing = {};
      VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT fifo_latest_ready = {};
#if VULKAN_WSI_LAYER_EXPERIMENTAL
      VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT multisampled_render_to_swapchain = {};
#endif

      storage() noexcept
      {
         swapchain_maintenance1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
         present_wait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
         present_wait2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR;
         swapchain_compression.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT;
         present_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
         present_id2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR;
         present_timing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT;
         fifo_latest_ready.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT;
#if VULKAN_WSI_LAYER_EXPERIMENTAL
         multisampled_render_to_swapchain.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT;
#endif
      }
   };

   writable_layer_feature writable_layer_features = {};

   features() = default;

   features(void *pNext)
      : writable_layer_features{ pNext }
   {
   }

   static VkBaseOutStructure *make_query_chain(const void *requested_features, storage &storage)
   {
      VkBaseOutStructure *supported_feature_chain = nullptr;
#define APPEND_IF_REQUESTED(ext, type, p_next, writable_struct, feature_chain)              \
   do                                                                                       \
   {                                                                                        \
      const auto *requested_feature = util::find_extension<ext>(type, p_next);              \
      if (requested_feature != nullptr)                                                     \
      {                                                                                     \
         append_requested_feature_query(feature_chain, requested_feature, writable_struct); \
      }                                                                                     \
   } while (false)

      APPEND_IF_REQUESTED(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, requested_features,
                          storage.swapchain_maintenance1, supported_feature_chain);
      APPEND_IF_REQUESTED(VkPhysicalDevicePresentWaitFeaturesKHR,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, requested_features,
                          storage.present_wait, supported_feature_chain);
      APPEND_IF_REQUESTED(VkPhysicalDevicePresentWait2FeaturesKHR,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR, requested_features,
                          storage.present_wait2, supported_feature_chain);
      APPEND_IF_REQUESTED(VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT,
                          requested_features, storage.swapchain_compression, supported_feature_chain);
      APPEND_IF_REQUESTED(VkPhysicalDevicePresentIdFeaturesKHR,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, requested_features,
                          storage.present_id, supported_feature_chain);
      APPEND_IF_REQUESTED(VkPhysicalDevicePresentId2FeaturesKHR,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR, requested_features,
                          storage.present_id2, supported_feature_chain);
      APPEND_IF_REQUESTED(VkPhysicalDevicePresentTimingFeaturesEXT,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT, requested_features,
                          storage.present_timing, supported_feature_chain);
      APPEND_IF_REQUESTED(VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT,
                          requested_features, storage.fifo_latest_ready, supported_feature_chain);
#if VULKAN_WSI_LAYER_EXPERIMENTAL
      APPEND_IF_REQUESTED(VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT,
                          requested_features, storage.multisampled_render_to_swapchain, supported_feature_chain);
#endif

#undef APPEND_IF_REQUESTED

      return supported_feature_chain;
   }

   void post_query(VkPhysicalDevice physical_device) noexcept
   {
      auto &instance = instance_private_data::get(physical_device);

      if (writable_layer_features.present_wait2 != nullptr)
      {
         writable_layer_features.present_wait2->presentWait2 = !instance.is_unsupported_surface_extension_enabled();
      }

      if (writable_layer_features.swapchain_compression != nullptr)
      {
         writable_layer_features.swapchain_compression->imageCompressionControlSwapchain =
            instance.has_image_compression_support(physical_device);
      }

      if (writable_layer_features.present_id != nullptr)
      {
         writable_layer_features.present_id->presentId = VK_TRUE;
      }

      if (writable_layer_features.present_id2 != nullptr)
      {
         writable_layer_features.present_id2->presentId2 = VK_TRUE;
      }

      if (writable_layer_features.swapchain_maintenance1 != nullptr)
      {
#if BUILD_WSI_DISPLAY
         /* The display backend does not implement swapchain maintenance1, so report it unsupported for
          * instances that enabled VK_KHR_display - matching the extension filter in
          * wsi_layer_vkEnumerateDeviceExtensionProperties. */
         const bool layer_handles_display = instance.is_instance_extension_enabled(VK_KHR_DISPLAY_EXTENSION_NAME);
#else
         /* Without the display backend the layer never handles a display surface, and that same filter
          * is compiled out, so suppressing the feature here would only leave the layer advertising an
          * extension whose feature it refuses - which fails vkCreateDevice for X11/Wayland apps that
          * happen to enable VK_KHR_display (the ICD may offer it). */
         const bool layer_handles_display = false;
#endif
         if (layer_handles_display)
         {
            writable_layer_features.swapchain_maintenance1->swapchainMaintenance1 = VK_FALSE;
         }
         else if (!writable_layer_features.swapchain_maintenance1->swapchainMaintenance1)
         {
            writable_layer_features.swapchain_maintenance1->swapchainMaintenance1 =
               instance.get_maintainance1_support();
         }
      }

      if (writable_layer_features.present_wait != nullptr)
      {
         writable_layer_features.present_wait->presentWait =
            writable_layer_features.present_wait->presentWait || !instance.is_unsupported_surface_extension_enabled();
      }

      if (writable_layer_features.present_timing != nullptr)
      {
         bool support;
         if (wsi::wsi_ext_present_timing::physical_device_has_supported_queue_family(physical_device, support) !=
             VK_SUCCESS)
         {
            WSI_LOG_ERROR("Failed to query physical device for present timing support");
            support = false;
         }

         writable_layer_features.present_timing->presentTiming = support ? VK_TRUE : VK_FALSE;
         writable_layer_features.present_timing->presentAtAbsoluteTime = VK_TRUE;
         writable_layer_features.present_timing->presentAtRelativeTime = VK_TRUE;
      }

      if (writable_layer_features.fifo_latest_ready != nullptr)
      {
         writable_layer_features.fifo_latest_ready->presentModeFifoLatestReady = VK_TRUE;
      }
   }

   bool compare_all(const void *requested_features) const noexcept
   {
      return compare(writable_layer_features.swapchain_maintenance1,
                     util::find_extension<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT>(
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, requested_features)) &&
             compare(writable_layer_features.present_wait,
                     util::find_extension<VkPhysicalDevicePresentWaitFeaturesKHR>(
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, requested_features)) &&
             compare(writable_layer_features.present_wait2,
                     util::find_extension<VkPhysicalDevicePresentWait2FeaturesKHR>(
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR, requested_features)) &&
             compare(writable_layer_features.swapchain_compression,
                     util::find_extension<VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT>(
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT,
                        requested_features)) &&
             compare(writable_layer_features.present_id,
                     util::find_extension<VkPhysicalDevicePresentIdFeaturesKHR>(
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, requested_features)) &&
             compare(writable_layer_features.present_id2,
                     util::find_extension<VkPhysicalDevicePresentId2FeaturesKHR>(
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR, requested_features)) &&
             compare(writable_layer_features.present_timing,
                     util::find_extension<VkPhysicalDevicePresentTimingFeaturesEXT>(
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT, requested_features)) &&
             compare(
                writable_layer_features.fifo_latest_ready,
                util::find_extension<VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT>(
                   VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT, requested_features))
#if VULKAN_WSI_LAYER_EXPERIMENTAL
             && compare(writable_layer_features.multisampled_render_to_swapchain,
                        util::find_extension<VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT>(
                           VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT,
                           requested_features))
#endif
         ;
   }

private:
   template <typename T>
   static void append_requested_feature_query(VkBaseOutStructure *&supported_feature_chain, const T *requested_features,
                                              T &supported_features) noexcept
   {
      if (requested_features == nullptr)
      {
         return;
      }

      auto *supported_feature_base = reinterpret_cast<VkBaseOutStructure *>(&supported_features);
      supported_feature_base->pNext = supported_feature_chain;
      supported_feature_chain = supported_feature_base;
   }

   static bool compare(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *supported_features,
                       const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->swapchainMaintenance1 == VK_FALSE ||
             supported_features->swapchainMaintenance1 == VK_TRUE;
   }

   static bool compare(VkPhysicalDevicePresentWaitFeaturesKHR *supported_features,
                       const VkPhysicalDevicePresentWaitFeaturesKHR *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->presentWait == VK_FALSE ||
             supported_features->presentWait == VK_TRUE;
   }

   static bool compare(VkPhysicalDevicePresentWait2FeaturesKHR *supported_features,
                       const VkPhysicalDevicePresentWait2FeaturesKHR *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->presentWait2 == VK_FALSE ||
             supported_features->presentWait2 == VK_TRUE;
   }

   static bool compare(VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *supported_features,
                       const VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->imageCompressionControlSwapchain == VK_FALSE ||
             supported_features->imageCompressionControlSwapchain == VK_TRUE;
   }

   static bool compare(VkPhysicalDevicePresentIdFeaturesKHR *supported_features,
                       const VkPhysicalDevicePresentIdFeaturesKHR *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->presentId == VK_FALSE ||
             supported_features->presentId == VK_TRUE;
   }

   static bool compare(VkPhysicalDevicePresentId2FeaturesKHR *supported_features,
                       const VkPhysicalDevicePresentId2FeaturesKHR *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->presentId2 == VK_FALSE ||
             supported_features->presentId2 == VK_TRUE;
   }

   static bool compare(VkPhysicalDevicePresentTimingFeaturesEXT *supported_features,
                       const VkPhysicalDevicePresentTimingFeaturesEXT *requested_features) noexcept
   {
      return (requested_features == nullptr || requested_features->presentTiming == VK_FALSE ||
              supported_features->presentTiming == VK_TRUE) &&
             (requested_features == nullptr || requested_features->presentAtAbsoluteTime == VK_FALSE ||
              supported_features->presentAtAbsoluteTime == VK_TRUE) &&
             (requested_features == nullptr || requested_features->presentAtRelativeTime == VK_FALSE ||
              supported_features->presentAtRelativeTime == VK_TRUE);
   }

   static bool compare(VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT *supported_features,
                       const VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->presentModeFifoLatestReady == VK_FALSE ||
             supported_features->presentModeFifoLatestReady == VK_TRUE;
   }

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   static bool compare(VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *supported_features,
                       const VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT *requested_features) noexcept
   {
      return requested_features == nullptr || requested_features->multisampledRenderToSwapchain == VK_FALSE ||
             supported_features->multisampledRenderToSwapchain == VK_TRUE;
   }
#endif
};

void populate_supported_layer_features(VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures2 &supported_features)
{
   auto &instance = layer::instance_private_data::get(physical_device);
   auto supported_layer_features = features{ supported_features.pNext };

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   auto *multisampled_render_to_swapchain =
      util::find_extension<VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT, supported_features.pNext);
   auto *multisampled_render_to_single_sampled =
      util::find_extension<VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT,
         supported_features.pNext);
   VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT multisampled_render_to_single_sampled_storage = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT,
      supported_features.pNext,
      VK_FALSE,
   };
   const void *original_feature_chain = supported_features.pNext;
   if (multisampled_render_to_swapchain != nullptr && multisampled_render_to_single_sampled == nullptr)
   {
      multisampled_render_to_single_sampled = &multisampled_render_to_single_sampled_storage;
      supported_features.pNext = multisampled_render_to_single_sampled;
   }
#endif

   auto get_physical_device_features2 =
      instance.disp.get_fn<PFN_vkGetPhysicalDeviceFeatures2KHR>("vkGetPhysicalDeviceFeatures2KHR");
   if (get_physical_device_features2.has_value())
   {
      (*get_physical_device_features2)(physical_device, &supported_features);
   }

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   if (multisampled_render_to_swapchain != nullptr)
   {
      multisampled_render_to_swapchain->multisampledRenderToSwapchain =
         multisampled_render_to_single_sampled->multisampledRenderToSingleSampled;
      supported_features.pNext = const_cast<void *>(original_feature_chain);
   }
#endif

   supported_layer_features.post_query(physical_device);
}

VkResult validate_requested_layer_features(VkPhysicalDevice physical_device, const VkDeviceCreateInfo *pCreateInfo)
{
   features::storage supported_feature_storage;
   auto *supported_feature_chain = features::make_query_chain(pCreateInfo->pNext, supported_feature_storage);
   if (supported_feature_chain == nullptr)
   {
      return VK_SUCCESS;
   }

   features supported_features{ supported_feature_chain };
   VkPhysicalDeviceFeatures2 supported_device_features = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      supported_feature_chain,
      {},
   };
   populate_supported_layer_features(physical_device, supported_device_features);
   if (!supported_features.compare_all(pCreateInfo->pNext))
   {
      return VK_ERROR_FEATURE_NOT_PRESENT;
   }

   return VK_SUCCESS;
}

static bool is_swapchain_maintenance1(const VkExtensionProperties &property)
{
   return strcmp(property.extensionName, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0 ||
          strcmp(property.extensionName, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0;
}

VKAPI_ATTR VkLayerInstanceCreateInfo *get_chain_info(const VkInstanceCreateInfo *pCreateInfo, VkLayerFunction func)
{
   auto *chain_info = reinterpret_cast<const VkLayerInstanceCreateInfo *>(pCreateInfo->pNext);
   while (chain_info &&
          !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain_info->function == func))
   {
      chain_info = reinterpret_cast<const VkLayerInstanceCreateInfo *>(chain_info->pNext);
   }

   return const_cast<VkLayerInstanceCreateInfo *>(chain_info);
}

VKAPI_ATTR VkLayerDeviceCreateInfo *get_chain_info(const VkDeviceCreateInfo *pCreateInfo, VkLayerFunction func)
{
   auto *chain_info = reinterpret_cast<const VkLayerDeviceCreateInfo *>(pCreateInfo->pNext);
   while (chain_info &&
          !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain_info->function == func))
   {
      chain_info = reinterpret_cast<const VkLayerDeviceCreateInfo *>(chain_info->pNext);
   }

   return const_cast<VkLayerDeviceCreateInfo *>(chain_info);
}

template <typename T>
static T get_instance_proc_addr(PFN_vkGetInstanceProcAddr fp_get_instance_proc_addr, const char *name,
                                VkInstance instance = VK_NULL_HANDLE)
{
   T func = reinterpret_cast<T>(fp_get_instance_proc_addr(instance, name));
   if (func == nullptr)
   {
      WSI_LOG_WARNING("Failed to get address of %s", name);
   }

   return func;
}

template <typename T>
static T get_device_proc_addr(PFN_vkGetDeviceProcAddr fp_get_device_proc_addr, const char *name,
                              VkDevice device = VK_NULL_HANDLE)
{
   T func = reinterpret_cast<T>(fp_get_device_proc_addr(device, name));
   if (func == nullptr)
   {
      WSI_LOG_WARNING("Failed to get address of %s", name);
   }

   return func;
}

/* This is where the layer is initialised and the instance dispatch table is constructed. */
VKAPI_ATTR VkResult create_instance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                    VkInstance *pInstance)
{
   VkLayerInstanceCreateInfo *layer_link_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   VkLayerInstanceCreateInfo *loader_data_callback = get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
   if (nullptr == layer_link_info || nullptr == layer_link_info->u.pLayerInfo || nullptr == loader_data_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer in layer initialization structures during vkCreateInstance");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = layer_link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkSetInstanceLoaderData loader_callback = loader_data_callback->u.pfnSetInstanceLoaderData;
   if (nullptr == fpGetInstanceProcAddr || nullptr == loader_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer for loader callback functions during vkCreateInstance");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto fpCreateInstance = get_instance_proc_addr<PFN_vkCreateInstance>(fpGetInstanceProcAddr, "vkCreateInstance");
   if (nullptr == fpCreateInstance)
   {
      WSI_LOG_ERROR("Unexpected NULL return value from pfnNextGetInstanceProcAddr");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* For instances handled by the layer, we need to enable extra extensions, therefore take a copy of pCreateInfo. */
   VkInstanceCreateInfo modified_info = *pCreateInfo;

   /* Create a util::vector in case we need to modify the modified_info.ppEnabledExtensionNames list.
    * This object and the extension_list object need to be in the global scope so they can be alive by the time
    * vkCreateInstance is called.
    */
   util::allocator allocator{ VK_SYSTEM_ALLOCATION_SCOPE_COMMAND, pAllocator };
   util::vector<const char *> modified_enabled_extensions{ allocator };
   util::extension_list extensions{ allocator };

   /* Find all the platforms that the layer can handle based on pCreateInfo->ppEnabledExtensionNames. */
   auto layer_platforms_to_enable = wsi::find_enabled_layer_platforms(pCreateInfo);

   /* Create a list of extensions to enable, including the provided extensions and those required by the layer. */
   TRY_LOG_CALL(extensions.add(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount));

   uint32_t api_version =
      pCreateInfo->pApplicationInfo != nullptr ? pCreateInfo->pApplicationInfo->apiVersion : VK_API_VERSION_1_3;

   if (!layer_platforms_to_enable.empty())
   {
      if (!extensions.contains(VK_KHR_SURFACE_EXTENSION_NAME))
      {
         return VK_ERROR_EXTENSION_NOT_PRESENT;
      }
      TRY_LOG_CALL(wsi::add_instance_extensions_required_by_layer(layer_platforms_to_enable, extensions, api_version));
   }

   TRY_LOG_CALL(extensions.get_extension_strings(modified_enabled_extensions));
   modified_info.ppEnabledExtensionNames = modified_enabled_extensions.data();
   modified_info.enabledExtensionCount = static_cast<uint32_t>(modified_enabled_extensions.size());

   /* Advance the link info for the next element on the chain. */
   layer_link_info->u.pLayerInfo = layer_link_info->u.pLayerInfo->pNext;

   /* Now call create instance on the chain further down the list.
    * Note that we do not remove the extensions that the layer supports from modified_info.ppEnabledExtensionNames.
    * Layers have to abide the rule that vkCreateInstance must not generate an error for unrecognized extension names.
    * Also, the loader filters the extension list to ensure that ICDs do not see extensions that they do not support.
    */
   TRY_LOG(fpCreateInstance(&modified_info, pAllocator, pInstance), "Failed to create the instance");
   /* Note: If the call to vkCreateInstance succeeded, the loader will do the clean-up for us
    * after this function returns with an error code. We can't call vkDestroyInstance
    * ourselves as this will cause double-free from the loader attempting to clean up after us.
    * Any failing calls below this point should NOT call vkDestroyInstance and rather just
    * return the error code. */

   /* Following the spec: use the callbacks provided to vkCreateInstance() if not nullptr,
    * otherwise use the default callbacks.
    */
   util::allocator instance_allocator{ VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE, pAllocator };
   std::optional<instance_dispatch_table> table = instance_dispatch_table::create(instance_allocator);
   if (!table.has_value())
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY_LOG_CALL(table->populate(*pInstance, fpGetInstanceProcAddr, api_version));
   table->set_user_enabled_extensions(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);

   TRY_LOG_CALL(instance_private_data::associate(*pInstance, std::move(*table), loader_callback,
                                                 layer_platforms_to_enable, api_version, instance_allocator));

   /*
    * Store the enabled instance extensions in order to return nullptr in
    * vkGetInstanceProcAddr for functions of disabled extensions.
    */
   VkResult result =
      instance_private_data::get(*pInstance)
         .set_instance_enabled_extensions(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);
   if (result != VK_SUCCESS)
   {
      instance_private_data::disassociate(*pInstance);
      return result;
   }

   return VK_SUCCESS;
}

/* FurMark 2.10 requests extendedDynamicState3PolygonMode without checking for it, and crashes when
 * vkCreateDevice then fails on ICDs that lack it (Mali). Returns the structure it cleared the request in,
 * so the caller can restore the application's value. */
static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *clear_unsupported_polygon_mode(
   instance_private_data &inst_data, VkPhysicalDevice physical_device, const void *p_next)
{
   const auto *requested = util::find_extension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, p_next);
   if (requested == nullptr || requested->extendedDynamicState3PolygonMode == VK_FALSE)
   {
      return nullptr;
   }

   VkPhysicalDeviceExtendedDynamicState3FeaturesEXT supported = {};
   supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
   VkPhysicalDeviceFeatures2KHR features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR, &supported, {} };
   inst_data.disp.GetPhysicalDeviceFeatures2KHR(physical_device, &features);
   if (supported.extendedDynamicState3PolygonMode != VK_FALSE)
   {
      return nullptr;
   }

   WSI_LOG_WARNING("Ignoring the unsupported extendedDynamicState3PolygonMode feature requested by the application.");
   auto *writable = const_cast<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *>(requested);
   writable->extendedDynamicState3PolygonMode = VK_FALSE;
   return writable;
}

/* The compositor, and Xwayland's glamor when it copies a presented frame, render on the same GPU as the
 * application. A GPU-bound application at the default queue priority keeps that work waiting behind its
 * in-flight frames, so each presented frame reaches the screen after a varying part of the next one has
 * rendered - on Mali, frame pacing jitter of up to a whole application frame. Give the queues of presenting
 * applications LOW global priority, so the presentation work runs first, unless the application chose a
 * priority itself or WSI_LOW_PRIORITY_QUEUES=0. Lowering the priority below the default never requires
 * permission. The queue create infos are rewritten into @p queue_infos and @p priorities, which must outlive
 * the call down. */
static VkResult lower_queue_priorities(VkPhysicalDevice physical_device, util::extension_list &enabled_extensions,
                                       VkDeviceCreateInfo &create_info,
                                       util::vector<VkDeviceQueueCreateInfo> &queue_infos,
                                       util::vector<VkDeviceQueueGlobalPriorityCreateInfoKHR> &priorities)
{
   const char *env = std::getenv("WSI_LOW_PRIORITY_QUEUES");
   if ((env != nullptr && std::strcmp(env, "0") == 0) || !enabled_extensions.contains(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
   {
      return VK_SUCCESS;
   }

   /* The point is for display servers to run first, so leave them alone when they render through Vulkan (e.g. a
    * compositor or Xwayland on zink) - at equal priority the GPU time-slices between them and a busy client, and
    * the screen stalls for a scheduling period at a time. A compositor on KMS has neither WAYLAND_DISPLAY nor
    * DISPLAY to connect to, and a compositor hands Xwayland (and its own helper clients) a WAYLAND_SOCKET. */
   const bool is_client = (std::getenv("WAYLAND_DISPLAY") != nullptr || std::getenv("DISPLAY") != nullptr) &&
                          std::getenv("WAYLAND_SOCKET") == nullptr;
   if (!is_client)
   {
      return VK_SUCCESS;
   }

   util::extension_list available_extensions{ enabled_extensions.get_allocator() };
   TRY_LOG_CALL(wsi::get_available_device_extensions(physical_device, available_extensions));
   const char *priority_extension = nullptr;
   if (available_extensions.contains(VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME))
   {
      priority_extension = VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME;
   }
   else if (available_extensions.contains(VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME))
   {
      priority_extension = VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME;
   }
   else
   {
      return VK_SUCCESS;
   }

   /* With the globalPriorityQuery feature enabled, a queue's priority must be one its family lists, so only
    * lower the queues of families that list LOW. Without the query extensions the application cannot enable
    * that feature, and there is nothing to check. */
   util::allocator allocator{ enabled_extensions.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND };
   util::vector<VkQueueFamilyGlobalPriorityPropertiesKHR> family_priorities{ allocator };
   const bool can_query = available_extensions.contains(VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME) ||
                          available_extensions.contains(VK_EXT_GLOBAL_PRIORITY_QUERY_EXTENSION_NAME);
   if (can_query)
   {
      auto &instance_data = instance_private_data::get(physical_device);
      uint32_t family_count = 0;
      instance_data.disp.GetPhysicalDeviceQueueFamilyProperties2KHR(physical_device, &family_count, nullptr);
      util::vector<VkQueueFamilyProperties2KHR> family_properties{ allocator };
      if (!family_properties.try_resize(family_count) || !family_priorities.try_resize(family_count))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      for (uint32_t family = 0; family < family_count; family++)
      {
         family_priorities[family] = {};
         family_priorities[family].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR;
         family_properties[family] = {};
         family_properties[family].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2_KHR;
         family_properties[family].pNext = &family_priorities[family];
      }
      instance_data.disp.GetPhysicalDeviceQueueFamilyProperties2KHR(physical_device, &family_count,
                                                                    family_properties.data());
   }
   auto family_lists_low = [&](uint32_t family) {
      if (!can_query)
      {
         return true;
      }
      if (family >= family_priorities.size())
      {
         return false;
      }
      for (uint32_t p = 0; p < family_priorities[family].priorityCount; p++)
      {
         if (family_priorities[family].priorities[p] == VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR)
         {
            return true;
         }
      }
      return false;
   };

   if (!queue_infos.try_resize(create_info.queueCreateInfoCount) ||
       !priorities.try_resize(create_info.queueCreateInfoCount))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   bool lowered = false;
   for (uint32_t i = 0; i < create_info.queueCreateInfoCount; i++)
   {
      queue_infos[i] = create_info.pQueueCreateInfos[i];
      if (util::find_extension<VkDeviceQueueGlobalPriorityCreateInfoKHR>(
             VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR, queue_infos[i].pNext) != nullptr ||
          !family_lists_low(queue_infos[i].queueFamilyIndex))
      {
         continue;
      }

      priorities[i] = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR, queue_infos[i].pNext,
                        VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR };
      queue_infos[i].pNext = &priorities[i];
      lowered = true;
   }

   if (lowered)
   {
      create_info.pQueueCreateInfos = queue_infos.data();
      TRY_LOG_CALL(enabled_extensions.add(priority_extension));
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult create_device(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VkLayerDeviceCreateInfo *layer_link_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   VkLayerDeviceCreateInfo *loader_data_callback = get_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
   if (nullptr == layer_link_info || nullptr == layer_link_info->u.pLayerInfo || nullptr == loader_data_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer in layer initialization structures during vkCreateDevice");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Retrieve the vkGetDeviceProcAddr and the vkCreateDevice function pointers for the next layer in the chain. */
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = layer_link_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = layer_link_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
   PFN_vkSetDeviceLoaderData loader_callback = loader_data_callback->u.pfnSetDeviceLoaderData;
   if (nullptr == fpGetInstanceProcAddr || nullptr == fpGetDeviceProcAddr || nullptr == loader_callback)
   {
      WSI_LOG_ERROR("Unexpected NULL pointer for loader callback functions during vkCreateDevice");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto fpCreateDevice = get_instance_proc_addr<PFN_vkCreateDevice>(fpGetInstanceProcAddr, "vkCreateDevice");
   if (nullptr == fpCreateDevice)
   {
      WSI_LOG_ERROR("Unexpected NULL return value from pfnNextGetInstanceProcAddr");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Advance the link info for the next element on the chain. */
   layer_link_info->u.pLayerInfo = layer_link_info->u.pLayerInfo->pNext;

   /* Enable extra extensions if needed by the layer, similarly to what done in vkCreateInstance. */
   VkDeviceCreateInfo modified_info = *pCreateInfo;

   auto &inst_data = instance_private_data::get(physicalDevice);
   util::allocator allocator{ inst_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND, pAllocator };
   util::vector<const char *> modified_enabled_extensions{ allocator };
   util::extension_list enabled_extensions{ allocator };

   TRY(validate_requested_layer_features(physicalDevice, pCreateInfo));

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT multisampled_render_to_single_sampled = {};
   VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *multisampled_render_to_single_sampled_override =
      nullptr;
   VkBool32 original_multisampled_render_to_single_sampled = VK_FALSE;
   const auto *multisampled_render_to_swapchain =
      util::find_extension<VkPhysicalDeviceMultisampledRenderToSwapchainFeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SWAPCHAIN_FEATURES_EXT, pCreateInfo->pNext);
   if (multisampled_render_to_swapchain != nullptr &&
       multisampled_render_to_swapchain->multisampledRenderToSwapchain == VK_TRUE)
   {
      const auto *requested_base_feature =
         util::find_extension<VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT, pCreateInfo->pNext);
      if (requested_base_feature != nullptr)
      {
         multisampled_render_to_single_sampled_override =
            const_cast<VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT *>(requested_base_feature);
         original_multisampled_render_to_single_sampled = requested_base_feature->multisampledRenderToSingleSampled;
      }
      else
      {
         multisampled_render_to_single_sampled.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT;
         multisampled_render_to_single_sampled.pNext = const_cast<void *>(modified_info.pNext);
         multisampled_render_to_single_sampled.multisampledRenderToSingleSampled = VK_TRUE;
         modified_info.pNext = &multisampled_render_to_single_sampled;
      }
   }
#endif

   VkPhysicalDeviceMaintenance9FeaturesKHR maintenance9_features = {};
   util::vector<VkDeviceQueueCreateInfo> low_priority_queue_infos{ allocator };
   util::vector<VkDeviceQueueGlobalPriorityCreateInfoKHR> low_priorities{ allocator };
   const util::wsi_platform_set &enabled_platforms = inst_data.get_enabled_platforms();
   if (!enabled_platforms.empty())
   {
      TRY_LOG_CALL(enabled_extensions.add(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount));
      TRY_LOG_CALL(wsi::add_device_extensions_required_by_layer(physicalDevice, enabled_platforms, enabled_extensions,
                                                                inst_data.api_version));
      auto present_timing_supported = wsi::present_timing_dependencies_supported(physicalDevice);
      if (std::holds_alternative<VkResult>(present_timing_supported))
      {
         return std::get<VkResult>(present_timing_supported);
      }
      if (std::get<bool>(present_timing_supported))
      {
         TRY_LOG_CALL(enabled_extensions.add(VK_KHR_MAINTENANCE_9_EXTENSION_NAME));

         const auto *device_maintenance9_features = util::find_extension<VkPhysicalDeviceMaintenance9FeaturesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR, pCreateInfo->pNext);
         if (device_maintenance9_features)
         {
            if (device_maintenance9_features->maintenance9 == VK_FALSE)
            {
               /* We are taking the same risk with the frame boundary features below. */
               auto *maintenance9_features_non_const =
                  const_cast<VkPhysicalDeviceMaintenance9FeaturesKHR *>(device_maintenance9_features);
               maintenance9_features_non_const->maintenance9 = VK_TRUE;
            }
         }
         else
         {
            maintenance9_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR;
            maintenance9_features.pNext = const_cast<void *>(modified_info.pNext);
            maintenance9_features.maintenance9 = VK_TRUE;

            modified_info.pNext = &maintenance9_features;
         }
      }

      TRY_LOG_CALL(lower_queue_priorities(physicalDevice, enabled_extensions, modified_info, low_priority_queue_infos,
                                          low_priorities));

      TRY_LOG_CALL(enabled_extensions.get_extension_strings(modified_enabled_extensions));

      modified_info.ppEnabledExtensionNames = modified_enabled_extensions.data();
      modified_info.enabledExtensionCount = static_cast<uint32_t>(modified_enabled_extensions.size());
   }

   bool should_layer_handle_frame_boundary_events = false;
   VkPhysicalDeviceFrameBoundaryFeaturesEXT frame_boundary;

   if (ENABLE_INSTRUMENTATION)
   {
      if (enabled_extensions.contains(VK_EXT_FRAME_BOUNDARY_EXTENSION_NAME))
      {
         if (inst_data.has_frame_boundary_support(physicalDevice))
         {
            const auto *application_frame_boundary_features =
               util::find_extension<VkPhysicalDeviceFrameBoundaryFeaturesEXT>(
                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT, pCreateInfo->pNext);

            if (application_frame_boundary_features)
            {
               if (application_frame_boundary_features->frameBoundary == VK_FALSE)
               {
                  /* The original features cannot be modified as they are marked as constant.
                   * Additionally, it is not possible to unlink this extension from the pNext
                   * chain as all other passed structures are also marked as const. We'll take
                   * the risk to modify the original structure as there is no trivial way to
                   * re-enable frame boundary feature or swap out the original structure. */
                  auto *frame_boundary_features_non_const =
                     const_cast<VkPhysicalDeviceFrameBoundaryFeaturesEXT *>(application_frame_boundary_features);
                  frame_boundary_features_non_const->frameBoundary = VK_TRUE;
               }
            }
            else
            {
               frame_boundary.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT;
               frame_boundary.pNext = const_cast<void *>(modified_info.pNext);
               frame_boundary.frameBoundary = VK_TRUE;

               modified_info.pNext = &frame_boundary;
            }

            should_layer_handle_frame_boundary_events = true;
         }
      }
   }

   /* Now call create device on the chain further down the list. */
#if VULKAN_WSI_LAYER_EXPERIMENTAL
   if (multisampled_render_to_single_sampled_override != nullptr)
   {
      multisampled_render_to_single_sampled_override->multisampledRenderToSingleSampled = VK_TRUE;
   }
#endif

   auto *cleared_polygon_mode = clear_unsupported_polygon_mode(inst_data, physicalDevice, modified_info.pNext);

   const VkResult create_device_result = fpCreateDevice(physicalDevice, &modified_info, pAllocator, pDevice);

   if (cleared_polygon_mode != nullptr)
   {
      cleared_polygon_mode->extendedDynamicState3PolygonMode = VK_TRUE;
   }

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   if (multisampled_render_to_single_sampled_override != nullptr)
   {
      multisampled_render_to_single_sampled_override->multisampledRenderToSingleSampled =
         original_multisampled_render_to_single_sampled;
   }
#endif

   TRY_LOG(create_device_result, "Failed to create the device");

   auto fn_destroy_device = get_device_proc_addr<PFN_vkDestroyDevice>(fpGetDeviceProcAddr, "vkDestroyDevice", *pDevice);
   /* This should never be nullptr */
   assert(fn_destroy_device != nullptr);

   /* Following the spec: use the callbacks provided to vkCreateDevice() if not nullptr, otherwise use the callbacks
    * provided to the instance (if no allocator callbacks was provided to the instance, it will use default ones).
    */
   util::allocator device_allocator{ inst_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, pAllocator };
   std::optional<device_dispatch_table> table = device_dispatch_table::create(device_allocator);
   if (!table.has_value())
   {
      fn_destroy_device(*pDevice, pAllocator);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   VkResult result = table->populate(*pDevice, fpGetDeviceProcAddr, inst_data.api_version);
   if (result != VK_SUCCESS)
   {
      fn_destroy_device(*pDevice, pAllocator);
      return result;
   }

   table->set_user_enabled_extensions(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);

   result = device_private_data::associate(*pDevice, inst_data, physicalDevice, std::move(*table), loader_callback,
                                           device_allocator);
   if (result != VK_SUCCESS)
   {
      fn_destroy_device(*pDevice, pAllocator);
      return result;
   }

   /*
    * Store the enabled device extensions in order to return nullptr in
    * vkGetDeviceProcAddr for functions of disabled extensions.
    */
   auto &device_data = layer::device_private_data::get(*pDevice);
   device_data.set_layer_frame_boundary_handling_enabled(should_layer_handle_frame_boundary_events);

   result = device_data.set_device_enabled_extensions(pCreateInfo->ppEnabledExtensionNames,
                                                      pCreateInfo->enabledExtensionCount);
   if (result != VK_SUCCESS)
   {
      layer::device_private_data::disassociate(*pDevice);
      fn_destroy_device(*pDevice, pAllocator);
      return result;
   }

   const auto *swapchain_compression_feature =
      util::find_extension<VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT, pCreateInfo->pNext);
   if (swapchain_compression_feature != nullptr)
   {
      device_data.set_swapchain_compression_control_enabled(
         swapchain_compression_feature->imageCompressionControlSwapchain);
   }

   const auto present_id_features = util::find_extension<VkPhysicalDevicePresentIdFeaturesKHR>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, pCreateInfo->pNext);
   if (present_id_features != nullptr)
   {
      device_data.set_present_id_feature_enabled(present_id_features->presentId);
   }

   const auto present_id2_features = util::find_extension<VkPhysicalDevicePresentId2FeaturesKHR>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR, pCreateInfo->pNext);
   if (present_id2_features != nullptr)
   {
      device_data.set_present_id2_feature_enabled(present_id2_features->presentId2);
   }

   const auto present_mode_fifo_latest_ready_features =
      util::find_extension<VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT>(
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT, pCreateInfo->pNext);
   if (present_mode_fifo_latest_ready_features != nullptr)
   {
      device_data.set_present_mode_fifo_latest_ready_enabled(
         present_mode_fifo_latest_ready_features->presentModeFifoLatestReady);
   }

   const auto *swapchain_maintenance1_features = util::find_extension<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, pCreateInfo->pNext);
   if (swapchain_maintenance1_features != nullptr)
   {
      device_data.set_swapchain_maintenance1_enabled(swapchain_maintenance1_features->swapchainMaintenance1);
   }

   auto *present_wait_features = util::find_extension<VkPhysicalDevicePresentWaitFeaturesKHR>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, pCreateInfo->pNext);
   if (present_wait_features != nullptr)
   {
      device_data.set_present_wait_enabled(present_wait_features->presentWait);
   }

   auto *present_wait2_features = util::find_extension<VkPhysicalDevicePresentWait2FeaturesKHR>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR, pCreateInfo->pNext);
   if (present_wait2_features != nullptr)
   {
      device_data.set_present_wait2_enabled(present_wait2_features->presentWait2);
   }

   return VK_SUCCESS;
}

} /* namespace layer */

VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetDeviceProcAddr(VkDevice device, const char *funcName) VWL_API_POST;

VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetInstanceProcAddr(VkInstance instance, const char *funcName) VWL_API_POST;

/* Clean up the dispatch table for this instance. */
VWL_VKAPI_CALL(void)
wsi_layer_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) VWL_API_POST
{
   if (instance == VK_NULL_HANDLE)
   {
      return;
   }

   auto fn_destroy_instance =
      layer::instance_private_data::get(instance).disp.get_fn<PFN_vkDestroyInstance>("vkDestroyInstance");

   /* Call disassociate() before doing vkDestroyInstance as an instance may be created by a different thread
    * just after we call vkDestroyInstance() and it could get the same address if we are unlucky.
    */
   layer::instance_private_data::disassociate(instance);

   assert(fn_destroy_instance.has_value());
   (*fn_destroy_instance)(instance, pAllocator);
}

VWL_VKAPI_CALL(void)
wsi_layer_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) VWL_API_POST
{
   if (device == VK_NULL_HANDLE)
   {
      return;
   }

   auto fn_destroy_device = layer::device_private_data::get(device).disp.get_fn<PFN_vkDestroyDevice>("vkDestroyDevice");

   /* Call disassociate() before doing vkDestroyDevice as a device may be created by a different thread
    * just after we call vkDestroyDevice().
    */
   layer::device_private_data::disassociate(device);

   assert(fn_destroy_device.has_value());
   (*fn_destroy_device)(device, pAllocator);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                           VkInstance *pInstance) VWL_API_POST
{
   return layer::create_instance(pCreateInfo, pAllocator, pInstance);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) VWL_API_POST
{
   return layer::create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

VWL_VKAPI_CALL(VkResult)
VWL_VKAPI_EXPORT wsi_layer_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                                                uint32_t *pPropertyCount,
                                                                VkExtensionProperties *pProperties) VWL_API_POST
{
   assert(physicalDevice);
   assert(pPropertyCount);

   auto &instance = layer::instance_private_data::get(physicalDevice);
#if BUILD_WSI_DISPLAY
   const bool filter =
      instance.is_instance_extension_enabled(VK_KHR_DISPLAY_EXTENSION_NAME) &&
      (pLayerName == nullptr || pLayerName[0] == '\0' || strcmp(pLayerName, "VK_LAYER_window_system_integration") == 0);
#else
   const bool filter = false;
#endif

   if (!filter)
   {
      return instance.disp.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
   }

   util::allocator allocator{ instance.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND };
   util::vector<VkExtensionProperties> properties{ allocator };
   uint32_t property_count = 0;

   VkResult result =
      instance.disp.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, &property_count, nullptr);
   if (result != VK_SUCCESS)
   {
      return result;
   }

   if (property_count > 0)
   {
      if (!properties.try_resize(property_count))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      result = instance.disp.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, &property_count,
                                                                properties.data());
      if (result != VK_SUCCESS)
      {
         return result;
      }
   }

   uint32_t filtered_count = 0;
   for (uint32_t i = 0; i < property_count; ++i)
   {
      if (!layer::is_swapchain_maintenance1(properties[i]))
      {
         ++filtered_count;
      }
   }

   if (pProperties == nullptr)
   {
      *pPropertyCount = filtered_count;
      return VK_SUCCESS;
   }

   const uint32_t capacity = *pPropertyCount;
   uint32_t copied_count = 0;
   for (uint32_t i = 0; i < property_count && copied_count < capacity; ++i)
   {
      if (!layer::is_swapchain_maintenance1(properties[i]))
      {
         pProperties[copied_count++] = properties[i];
      }
   }

   *pPropertyCount = copied_count;
   return copied_count < filtered_count ? VK_INCOMPLETE : VK_SUCCESS;
}

VWL_VKAPI_CALL(VkResult)
VWL_VKAPI_EXPORT wsi_layer_vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct)
   VWL_API_POST
{
   assert(pVersionStruct);
   assert(pVersionStruct->sType == LAYER_NEGOTIATE_INTERFACE_STRUCT);

   /* 2 is the minimum interface version which would utilize this function. */
   assert(pVersionStruct->loaderLayerInterfaceVersion >= 2);

   /* Set our requested interface version. Set to 2 for now to separate us from newer versions. */
   pVersionStruct->loaderLayerInterfaceVersion = 2;

   /* Fill in struct values. */
   pVersionStruct->pfnGetInstanceProcAddr = &wsi_layer_vkGetInstanceProcAddr;
   pVersionStruct->pfnGetDeviceProcAddr = &wsi_layer_vkGetDeviceProcAddr;
   pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;

   return VK_SUCCESS;
}

VWL_VKAPI_CALL(void)
wsi_layer_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physical_device,
                                       VkPhysicalDeviceFeatures2 *pFeatures) VWL_API_POST
{
   layer::populate_supported_layer_features(physical_device, *pFeatures);
}

#define GET_PROC_ADDR(func)      \
   if (!strcmp(funcName, #func)) \
      return (PFN_vkVoidFunction)&wsi_layer_##func;

/**
 * @brief Trampoline for **vkGetDeviceProcAddr** inside the WSI layer.
 *
 * Workflow:
 * 1. Retrieve the device’s private state (enabled extensions and downstream dispatch table).
 * 2. If the function is one that this layer intercepts, return the layer’s handler.
 * 3. Otherwise, forward to the downstream dispatch table (next layer or ICD), or return nullptr if unavailable.
 *
 * This layer never exposes entrypoints for disabled extensions, preserving the Vulkan dispatch-chain contract.
 *
 * @param device   The VkDevice being queried.
 * @param funcName Name of the device-level command to resolve.
 * @return Pointer to the layer’s implementation, the next-layer/ICD function, or nullptr.
 */
VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetDeviceProcAddr(VkDevice device, const char *funcName) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   const uint64_t api_version = device_data.instance_data.api_version;
   const bool core_1_1 = api_version >= VK_API_VERSION_1_1;
   if (device_data.is_device_extension_enabled(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkCreateSwapchainKHR);
      GET_PROC_ADDR(vkDestroySwapchainKHR);
      GET_PROC_ADDR(vkGetSwapchainImagesKHR);
      GET_PROC_ADDR(vkAcquireNextImageKHR);
      GET_PROC_ADDR(vkQueuePresentKHR);
      if (device_data.is_device_extension_enabled(VK_KHR_DEVICE_GROUP_EXTENSION_NAME) || core_1_1)
      {
         GET_PROC_ADDR(vkAcquireNextImage2KHR);
      }
      if (core_1_1)
      {
         GET_PROC_ADDR(vkGetDeviceGroupSurfacePresentModesKHR);
         GET_PROC_ADDR(vkGetDeviceGroupPresentCapabilitiesKHR);
      }
   }

   if (device_data.is_device_extension_enabled(VK_KHR_DEVICE_GROUP_EXTENSION_NAME) &&
       device_data.is_device_extension_enabled(VK_KHR_SURFACE_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkGetDeviceGroupSurfacePresentModesKHR);
      GET_PROC_ADDR(vkGetDeviceGroupPresentCapabilitiesKHR);
   }

   if (device_data.is_device_extension_enabled(VK_KHR_SHARED_PRESENTABLE_IMAGE_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkGetSwapchainStatusKHR);
   }

   if (device_data.is_device_extension_enabled(VK_EXT_PRESENT_TIMING_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkSetSwapchainPresentTimingQueueSizeEXT);
      GET_PROC_ADDR(vkGetSwapchainTimingPropertiesEXT);
      GET_PROC_ADDR(vkGetSwapchainTimeDomainPropertiesEXT);
      GET_PROC_ADDR(vkGetPastPresentationTimingEXT);
      GET_PROC_ADDR(vkGetCalibratedTimestampsKHR);
      if (device_data.is_device_extension_enabled(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
      {
         GET_PROC_ADDR(vkGetCalibratedTimestampsEXT);
      }
   }

   GET_PROC_ADDR(vkDestroyDevice);
   GET_PROC_ADDR(vkCreateImage);

   if (device_data.is_device_extension_enabled(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME))
   {
      if (!strcmp(funcName, "vkBindImageMemory2KHR"))
      {
         return (PFN_vkVoidFunction)&wsi_layer_vkBindImageMemory2;
      }
   }
   if (core_1_1)
   {
      GET_PROC_ADDR(vkBindImageMemory2);
   }

   /* VK_EXT_swapchain_maintenance1 */
   if (device_data.is_device_extension_enabled(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkReleaseSwapchainImagesEXT);
   }

   /* VK_KHR_swapchain_maintenance1 */
   if (device_data.is_device_extension_enabled(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))
   {
      if (!strcmp(funcName, "vkReleaseSwapchainImagesKHR"))
      {
         return (PFN_vkVoidFunction)&wsi_layer_vkReleaseSwapchainImagesEXT;
      }
   }

   /* VK_KHR_present_wait */
   if (device_data.is_device_extension_enabled(VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkWaitForPresentKHR);
   }

   /* VK_KHR_present_wait2 */
   if (device_data.is_device_extension_enabled(VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME))
   {
      GET_PROC_ADDR(vkWaitForPresent2KHR);
   }

   return device_data.disp.get_user_enabled_entrypoint(device, funcName);
}

/**
 * @brief Trampoline for **vkGetInstanceProcAddr** inside the WSI layer.
 *
 * Workflow:
 * 1. Publish loader-critical symbols (i.e. `vkGetDeviceProcAddr`, `vkGetInstanceProcAddr`).
 * 2. Retrieve the instance’s private state (API version and enabled extensions)
 *    and intercept layer-handled commands.
 * 3. Forward all other commands to the downstream instance dispatch table,
 *    or return nullptr if unavailable.
 *
 * This layer only exposes core commands and enabled-extension entrypoints,
 * preserving the Vulkan dispatch-chain contract.
 *
 * @param instance The VkInstance being queried (may be VK_NULL_HANDLE).
 * @param funcName Name of the instance-level command to resolve.
 * @return Pointer to this layer’s handler, the next-layer/ICD function, or nullptr.
 */
VWL_VKAPI_CALL(PFN_vkVoidFunction)
wsi_layer_vkGetInstanceProcAddr(VkInstance instance, const char *funcName) VWL_API_POST
{
   GET_PROC_ADDR(vkGetDeviceProcAddr);
   GET_PROC_ADDR(vkGetInstanceProcAddr);
   GET_PROC_ADDR(vkCreateInstance);
   GET_PROC_ADDR(vkDestroyInstance);
   GET_PROC_ADDR(vkCreateDevice);

   if (instance == VK_NULL_HANDLE)
   {
      return nullptr;
   }

   auto &instance_data = layer::instance_private_data::get(instance);
   GET_PROC_ADDR(vkEnumerateDeviceExtensionProperties);

   const bool core_1_1 = instance_data.api_version >= VK_API_VERSION_1_1;
   if ((instance_data.is_instance_extension_enabled(VK_KHR_DEVICE_GROUP_EXTENSION_NAME) &&
        instance_data.is_instance_extension_enabled(VK_KHR_SURFACE_EXTENSION_NAME)) ||
       core_1_1)
   {
      GET_PROC_ADDR(vkGetPhysicalDevicePresentRectanglesKHR);
   }

   if (instance_data.is_instance_extension_enabled(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
   {
      if (!strcmp(funcName, "vkGetPhysicalDeviceFeatures2KHR"))
      {
         return (PFN_vkVoidFunction)&wsi_layer_vkGetPhysicalDeviceFeatures2;
      }
   }
   if (core_1_1)
   {
      GET_PROC_ADDR(vkGetPhysicalDeviceFeatures2);
   }

   if (instance_data.is_instance_extension_enabled(VK_KHR_SURFACE_EXTENSION_NAME))
   {
      PFN_vkVoidFunction wsi_func = wsi::get_proc_addr(funcName, instance_data);
      if (wsi_func)
      {
         return wsi_func;
      }

      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceSupportKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceFormatsKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceSurfacePresentModesKHR);
      GET_PROC_ADDR(vkDestroySurfaceKHR);

      if (instance_data.is_instance_extension_enabled(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
      {
         GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
         GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceFormats2KHR);
      }

      GET_PROC_ADDR(vkGetPhysicalDeviceCalibrateableTimeDomainsKHR);
      GET_PROC_ADDR(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT);

      if (instance_data.is_instance_extension_enabled(VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME))
      {
         GET_PROC_ADDR(vkGetPhysicalDeviceSurfaceCapabilities2EXT);
      }
   }

   return instance_data.disp.get_user_enabled_entrypoint(instance, funcName);
}
