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
 * @file external_memory_extension.hpp
 */

#pragma once

#include <optional>
#include <utility>

#include <util/custom_allocator.hpp>
#include <util/macros.hpp>
#include "util/drm/drm_utils.hpp"

#include <wsi/image_backing_memory_external.hpp>
#include <wsi/wsi_alloc_utils.hpp>

#include "image_create_info_extension.hpp"
#include "image_compression_control.hpp"

namespace wsi
{

struct external_image_create_info
{
   wsialloc_format selected_format;
   VkImageCreateFlags flags;
   VkExtent3D extent;
   bool explicit_compression;
};

/**
 * @brief Class used to extend VkImageCreateInfo for external memory handles.
 */
class swapchain_image_create_external_memory : public image_create_info_extension
{
public:
   /**
    * @brief Construct a new swapchain image create external memory::swapchain image create external memory object
    *
    * @param image_create_info The image creation info.
    * @param compression Optional compression control parameters for the swapchain image.
    * @param wsialloc_selected_format Selected format for allocation by WSIAlloc.
    * @param plane_layouts Plane layouts description for each plane.
    * @param drm_format_props A list of all device supported VkDrmFormatModifierPropertiesEXT.
    */
   swapchain_image_create_external_memory(const VkImageCreateInfo &image_create_info,
                                          std::optional<image_create_compression_control> &compression,
                                          wsialloc_allocate_result wsialloc_selected_format,
                                          util::vector<VkSubresourceLayout> plane_layouts,
                                          const util::vector<VkDrmFormatModifierPropertiesEXT> &drm_format_props);

   /**
    * @brief Create swapchain_image_create_external_memory object.
    *
    * @param image_create_info The image creation info.
    * @param compression Optional compression control parameters for the swapchain image.
    * @param wsi_allocator WSIAlloc allocator.
    * @param surface_formats Supported surface formats from the presentation engine.
    * @param physical_device Vulkan physical device handle.
    * @param allocator User provided allocation callbacks.
    * @return VkResult in case of error, util::unique_ptr<swapchain_image_create_external_memory> otherwise.
    */
   static std::variant<VkResult, util::unique_ptr<swapchain_image_create_external_memory>> create(
      const VkImageCreateInfo &image_create_info, std::optional<image_create_compression_control> compression,
      swapchain_wsialloc_allocator &wsi_allocator, const util::vector<util::drm::drm_format_pair> &surface_formats,
      VkPhysicalDevice physical_device, util::allocator allocator);

   /**
    * @brief Extend VkImageCreateInfo
    *
    * @param image_create_info VkImageCreateInfo structure to extend.
    * @return VkResult error code.
    */
   VkResult extend_image_create_info(VkImageCreateInfo *image_create_info) override;

   /**
    * @brief Get the external image create information.
    *
    * @return external_image_create_info structure.
    */
   external_image_create_info get_external_image_create_info() const;

private:
   wsialloc_allocate_result m_wsialloc_selected_format;

   VkImageCreateFlags m_create_flags;
   VkExtent3D m_create_extent;
   bool m_use_fixed_rate_compression;
   uint32_t m_prop_plane_count;

   util::vector<VkSubresourceLayout> m_plane_layouts;
   VkImageDrmFormatModifierExplicitCreateInfoEXT m_drm_mod_info;
   VkExternalMemoryImageCreateInfoKHR m_external_info;
};

} /* namespace wsi */