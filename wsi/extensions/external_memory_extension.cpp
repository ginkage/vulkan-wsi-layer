/*
 * Copyright (c) 2025-2026 Arm Limited.
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
 * @file external_memory_extension.cpp
 */
#include <cassert>

#include <util/helpers.hpp>
#include <util/format_modifiers.hpp>

#include <layer/private_data.hpp>

#include "external_memory_extension.hpp"

namespace wsi
{

/**
 * @brief Finds what formats are compatible with the requested swapchain image Vulkan Device and backend surface.
 *
 * @param[in]  info               The Swapchain image creation info.
 * @param[in]  physical_device    Vulkan physical device.
 * @param[in]  compression        Optional compression control parameters for the swapchain image.
 * @param[in]  surface_formats    Supported surface formats from the presentation engine.
 * @param[out] importable_formats A list of formats that can be imported to the Vulkan Device.
 * @param[out] exportable_formats A list of formats that can be exported from the Vulkan Device.
 * @param[out] drm_format_props   A list of all device supported VkDrmFormatModifierPropertiesEXT.
 *
 * @return VK_SUCCESS for success, otherwise other error code.
 */
VkResult get_surface_compatible_formats(const VkImageCreateInfo &image_create_info, VkPhysicalDevice physical_device,
                                        std::optional<image_create_compression_control> &compression,
                                        const util::vector<util::drm::drm_format_pair> &surface_formats,
                                        util::vector<wsialloc_format> &importable_formats,
                                        util::vector<uint64_t> &exportable_modifers,
                                        util::vector<VkDrmFormatModifierPropertiesEXT> &drm_format_props)
{
   TRY_LOG(util::get_drm_format_properties(physical_device, image_create_info.format, drm_format_props),
           "Failed to get format properties");

   for (const auto &prop : drm_format_props)
   {
      bool is_supported = true;
      util::drm::drm_format_pair drm_format{ util::drm::vk_to_drm_format(image_create_info.format),
                                             prop.drmFormatModifier };

      for (const auto &format : surface_formats)
      {
         is_supported = false;
         if (format.fourcc == drm_format.fourcc && format.modifier == drm_format.modifier)
         {
            is_supported = true;
            break;
         }
      }

      if (!is_supported)
      {
         continue;
      }

      VkExternalImageFormatPropertiesKHR external_props = {};
      external_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES_KHR;

      VkImageFormatProperties2KHR format_props = {};
      format_props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2_KHR;
      format_props.pNext = &external_props;

      VkResult result = VK_SUCCESS;
      {
         VkPhysicalDeviceExternalImageFormatInfoKHR external_info = {};
         external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO_KHR;
         external_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

         VkPhysicalDeviceImageDrmFormatModifierInfoEXT drm_mod_info = {};
         drm_mod_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
         drm_mod_info.pNext = &external_info;
         drm_mod_info.drmFormatModifier = prop.drmFormatModifier;
         drm_mod_info.sharingMode = image_create_info.sharingMode;
         drm_mod_info.queueFamilyIndexCount = image_create_info.queueFamilyIndexCount;
         drm_mod_info.pQueueFamilyIndices = image_create_info.pQueueFamilyIndices;

         VkPhysicalDeviceImageFormatInfo2KHR image_info = {};
         image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR;
         image_info.pNext = &drm_mod_info;
         image_info.format = image_create_info.format;
         image_info.type = image_create_info.imageType;
         image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         image_info.usage = image_create_info.usage;
         image_info.flags = image_create_info.flags;

         /* Attach view format list (if any) to the image_info chain, as required by the spec. */
         const auto *image_format_list = util::find_extension<VkImageFormatListCreateInfo>(
            VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, image_create_info.pNext);
         VkImageFormatListCreateInfo image_format_list_copy = {};
         if (image_format_list)
         {
            image_format_list_copy = util::shallow_copy_extension(image_format_list);
            image_format_list_copy.pNext = image_info.pNext;
            image_info.pNext = &image_format_list_copy;
         }

         VkImageCompressionControlEXT compression_control = {};
         if (compression.has_value())
         {
            compression_control = compression->get_compression_control_properties();
            compression_control.pNext = image_info.pNext;
            image_info.pNext = &compression_control;
         }

         auto &instance_data = layer::instance_private_data::get(physical_device);
         result =
            instance_data.disp.GetPhysicalDeviceImageFormatProperties2KHR(physical_device, &image_info, &format_props);
      }
      if (result != VK_SUCCESS)
      {
         continue;
      }
      if (format_props.imageFormatProperties.maxExtent.width < image_create_info.extent.width ||
          format_props.imageFormatProperties.maxExtent.height < image_create_info.extent.height ||
          format_props.imageFormatProperties.maxExtent.depth < image_create_info.extent.depth)
      {
         continue;
      }
      if (format_props.imageFormatProperties.maxMipLevels < image_create_info.mipLevels ||
          format_props.imageFormatProperties.maxArrayLayers < image_create_info.arrayLayers)
      {
         continue;
      }
      if ((format_props.imageFormatProperties.sampleCounts & image_create_info.samples) != image_create_info.samples)
      {
         continue;
      }

      if (external_props.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT_KHR)
      {
         if (!exportable_modifers.try_push_back(drm_format.modifier))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }

      if (external_props.externalMemoryProperties.externalMemoryFeatures &
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT_KHR)
      {
         uint64_t flags =
            (prop.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_DISJOINT_BIT) ? 0 : WSIALLOC_FORMAT_NON_DISJOINT;
         wsialloc_format import_format{ drm_format.fourcc, drm_format.modifier, flags };
         if (!importable_formats.try_push_back(import_format))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }
   }

   return VK_SUCCESS;
}

/**
 * @brief Queries WSIAlloc for preferred format for allocation.
 *
 * @param image_create_info The Swapchain image creation info.
 * @param compression Optional compression control parameters for the swapchain image.
 * @param wsi_allocator WSIAlloc allocator.
 * @param in_importable_formats A list of formats that can be imported to the Vulkan Device.
 * @param out_plane_layouts Plane layouts description for each plane.
 * @return VkResult in case of failure, wsialloc_allocate_result otherwise.
 */
std::variant<VkResult, wsialloc_allocate_result> query_wsialloc_preferred_format(
   const VkImageCreateInfo &image_create_info, std::optional<image_create_compression_control> &compression,
   swapchain_wsialloc_allocator &wsi_allocator, util::vector<wsialloc_format> &in_importable_formats,
   util::vector<VkSubresourceLayout> &out_plane_layouts)
{
   bool use_fixed_rate_compression = false;
   if (compression)
   {
      if (compression->get_bitmask_for_image_compression_flags() & VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT)
      {
         use_fixed_rate_compression = true;
      }
   }

   allocation_params params = { (image_create_info.flags & VK_IMAGE_CREATE_PROTECTED_BIT) != 0,
                                image_create_info.extent,
                                in_importable_formats.data(),
                                in_importable_formats.size(),
                                use_fixed_rate_compression,
                                true };
   wsialloc_allocate_result alloc_result = { {}, { 0 }, { 0 }, { -1 }, false };
   TRY(wsi_allocator.allocate(params, &alloc_result));

   const uint32_t format_planes = util::drm::drm_fourcc_format_get_num_planes(alloc_result.format.fourcc);
   if (!out_plane_layouts.try_resize(format_planes))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t plane = 0; plane < format_planes; ++plane)
   {
      assert(alloc_result.average_row_strides[plane] >= 0);
      out_plane_layouts[plane].offset = alloc_result.offsets[plane];
      out_plane_layouts[plane].rowPitch = static_cast<uint32_t>(alloc_result.average_row_strides[plane]);
   }

   return alloc_result;
}

std::variant<VkResult, util::unique_ptr<swapchain_image_create_external_memory>> swapchain_image_create_external_memory::
   create(const VkImageCreateInfo &image_create_info, std::optional<image_create_compression_control> compression,
          swapchain_wsialloc_allocator &wsi_allocator, const util::vector<util::drm::drm_format_pair> &surface_formats,
          VkPhysicalDevice physical_device, util::allocator allocator)
{
   util::vector<wsialloc_format> importable_formats(util::allocator(allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
   util::vector<uint64_t> exportable_modifiers(util::allocator(allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));

   /* Query supported modifiers. */
   util::vector<VkDrmFormatModifierPropertiesEXT> drm_format_props(
      util::allocator(allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));

   TRY_LOG_CALL(get_surface_compatible_formats(image_create_info, physical_device, compression, surface_formats,
                                               importable_formats, exportable_modifiers, drm_format_props));

   /* TODO: Handle exportable images which use ICD allocated memory in preference to an external allocator. */
   if (importable_formats.empty())
   {
      WSI_LOG_ERROR("Export/Import not supported.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   util::vector<VkSubresourceLayout> plane_layouts(allocator);
   auto wsialloc_preferred_format_result =
      query_wsialloc_preferred_format(image_create_info, compression, wsi_allocator, importable_formats, plane_layouts);
   if (auto error = std::get_if<VkResult>(&wsialloc_preferred_format_result))
   {
      return *error;
   }

   auto wsialloc_preferred_format = std::get<wsialloc_allocate_result>(wsialloc_preferred_format_result);

   auto ext_memory = allocator.make_unique<swapchain_image_create_external_memory>(
      image_create_info, compression, wsialloc_preferred_format, std::move(plane_layouts), drm_format_props);
   if (ext_memory == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   return ext_memory;
}

swapchain_image_create_external_memory::swapchain_image_create_external_memory(
   const VkImageCreateInfo &image_create_info, std::optional<image_create_compression_control> &compression,
   wsialloc_allocate_result wsialloc_selected_format, util::vector<VkSubresourceLayout> plane_layouts,
   const util::vector<VkDrmFormatModifierPropertiesEXT> &drm_format_props)
   : m_wsialloc_selected_format(wsialloc_selected_format)
   , m_create_flags()
   , m_create_extent()
   , m_use_fixed_rate_compression(false)
   , m_prop_plane_count()
   , m_plane_layouts(std::move(plane_layouts))
   , m_drm_mod_info()
   , m_external_info()
{
   if (compression)
   {
      if (compression->get_bitmask_for_image_compression_flags() & VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT)
      {
         m_use_fixed_rate_compression = true;
      }
   }

   for (auto &prop : drm_format_props)
   {
      if (prop.drmFormatModifier == wsialloc_selected_format.format.modifier)
      {
         m_prop_plane_count = prop.drmFormatModifierPlaneCount;
      }
   }

   m_create_extent = image_create_info.extent;
   m_create_flags = image_create_info.flags;
   if (m_wsialloc_selected_format.is_disjoint)
   {
      m_create_flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
   }
}

VkResult swapchain_image_create_external_memory::extend_image_create_info(VkImageCreateInfo *image_create_info)
{
   assert(image_create_info != nullptr);

   if (m_wsialloc_selected_format.is_disjoint)
   {
      image_create_info->flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
   }

   m_drm_mod_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
   m_drm_mod_info.pNext = image_create_info->pNext;
   m_drm_mod_info.drmFormatModifier = m_wsialloc_selected_format.format.modifier;
   m_drm_mod_info.drmFormatModifierPlaneCount = m_prop_plane_count;
   m_drm_mod_info.pPlaneLayouts = m_plane_layouts.data();

   m_external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
   m_external_info.pNext = &m_drm_mod_info;
   m_external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   image_create_info->pNext = &m_external_info;
   image_create_info->tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

   return VK_SUCCESS;
}

external_image_create_info swapchain_image_create_external_memory::get_external_image_create_info() const
{
   assert(m_wsialloc_selected_format.format.fourcc != 0);

   external_image_create_info image_info = { m_wsialloc_selected_format.format, m_create_flags, m_create_extent,
                                             m_use_fixed_rate_compression };
   return image_info;
}

} /* namespace wsi */
