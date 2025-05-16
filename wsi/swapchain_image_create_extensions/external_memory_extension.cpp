/*
 * Copyright (c) 2025 Arm Limited.
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

VkResult swapchain_image_create_external_memory::extend_image_create_info(VkImageCreateInfo *image_create_info)
{
   assert(image_create_info != nullptr);

   util::vector<wsialloc_format> importable_formats(util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
   util::vector<uint64_t> exportable_modifiers(util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));

   /* Query supported modifers. */
   util::vector<VkDrmFormatModifierPropertiesEXT> drm_format_props(
      util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));

   TRY_LOG_CALL(
      get_surface_compatible_formats(*image_create_info, importable_formats, exportable_modifiers, drm_format_props));

   /* TODO: Handle exportable images which use ICD allocated memory in preference to an external allocator. */
   if (importable_formats.empty())
   {
      WSI_LOG_ERROR("Export/Import not supported.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   bool enable_fixed_rate = false;
   if (m_compression)
   {
      if (m_compression->get_bitmask_for_image_compression_flags() & VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT)
      {
         enable_fixed_rate = true;
      }
   }

   allocation_params params = { (image_create_info->flags & VK_IMAGE_CREATE_PROTECTED_BIT) != 0,
                                image_create_info->extent, importable_formats, enable_fixed_rate, true };
   wsialloc_allocate_result alloc_result = { {}, { 0 }, { 0 }, { -1 }, false };
   assert(m_wsi_allocator);
   TRY(m_wsi_allocator->allocate(params, &alloc_result));

   uint32_t prop_plane_count = 0;
   for (auto &prop : drm_format_props)
   {
      if (prop.drmFormatModifier == alloc_result.format.modifier)
      {
         prop_plane_count = prop.drmFormatModifierPlaneCount;
      }
   }

   if (alloc_result.is_disjoint)
   {
      image_create_info->flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
   }

   const uint32_t format_planes = util::drm::drm_fourcc_format_get_num_planes(alloc_result.format.fourcc);
   if (!m_plane_layouts.try_resize(format_planes))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t plane = 0; plane < format_planes; ++plane)
   {
      assert(alloc_result.average_row_strides[plane] >= 0);
      m_plane_layouts[plane].offset = alloc_result.offsets[plane];
      m_plane_layouts[plane].rowPitch = static_cast<uint32_t>(alloc_result.average_row_strides[plane]);
   }

   m_drm_mod_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
   m_drm_mod_info.pNext = image_create_info->pNext;
   m_drm_mod_info.drmFormatModifier = alloc_result.format.modifier;
   m_drm_mod_info.drmFormatModifierPlaneCount = prop_plane_count;
   m_drm_mod_info.pPlaneLayouts = m_plane_layouts.data();

   m_external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR;
   m_external_info.pNext = &m_drm_mod_info;
   m_external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   image_create_info->pNext = &m_external_info;
   image_create_info->tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

   return VK_SUCCESS;
}

VkResult swapchain_image_create_external_memory::get_surface_compatible_formats(
   const VkImageCreateInfo &info, util::vector<wsialloc_format> &importable_formats,
   util::vector<uint64_t> &exportable_modifers, util::vector<VkDrmFormatModifierPropertiesEXT> &drm_format_props)
{
   TRY_LOG(util::get_drm_format_properties(m_physical_device, info.format, drm_format_props),
           "Failed to get format properties");

   for (const auto &prop : drm_format_props)
   {
      bool is_supported = true;
      util::drm::drm_format_pair drm_format{ util::drm::vk_to_drm_format(info.format), prop.drmFormatModifier };

      if (m_surface_formats != nullptr)
      {
         is_supported = false;
         for (const auto &format : *m_surface_formats)
         {
            if (format.fourcc == drm_format.fourcc && format.modifier == drm_format.modifier)
            {
               is_supported = true;
               break;
            }
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
         drm_mod_info.sharingMode = info.sharingMode;
         drm_mod_info.queueFamilyIndexCount = info.queueFamilyIndexCount;
         drm_mod_info.pQueueFamilyIndices = info.pQueueFamilyIndices;

         VkPhysicalDeviceImageFormatInfo2KHR image_info = {};
         image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR;
         image_info.pNext = &drm_mod_info;
         image_info.format = info.format;
         image_info.type = info.imageType;
         image_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         image_info.usage = info.usage;
         image_info.flags = info.flags;

         VkImageCompressionControlEXT compression_control = {};
         if (m_compression)
         {
            compression_control = m_compression->get_compression_control_properties();
            compression_control.pNext = image_info.pNext;
            image_info.pNext = &compression_control;
         }

         auto &instance_data = layer::instance_private_data::get(m_physical_device);
         result = instance_data.disp.GetPhysicalDeviceImageFormatProperties2KHR(m_physical_device, &image_info,
                                                                                &format_props);
      }
      if (result != VK_SUCCESS)
      {
         continue;
      }
      if (format_props.imageFormatProperties.maxExtent.width < info.extent.width ||
          format_props.imageFormatProperties.maxExtent.height < info.extent.height ||
          format_props.imageFormatProperties.maxExtent.depth < info.extent.depth)
      {
         continue;
      }
      if (format_props.imageFormatProperties.maxMipLevels < info.mipLevels ||
          format_props.imageFormatProperties.maxArrayLayers < info.arrayLayers)
      {
         continue;
      }
      if ((format_props.imageFormatProperties.sampleCounts & info.samples) != info.samples)
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
};
