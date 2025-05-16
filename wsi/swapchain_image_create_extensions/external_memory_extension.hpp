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

#include <wsi/wsi_alloc_utils.hpp>

#include "swapchain_image_create_info_extension.hpp"
#include "image_compression_control.hpp"

namespace wsi
{

/**
 * @brief
 */
class swapchain_image_create_external_memory : public swapchain_image_create_info_extension
{
public:
   /**
    * @brief Constructs a swapchain_image_create_external_memory object.
    *
    * @param[in] compression      Optional compression control parameters for the swapchain image.
    * @param[in] wsi_allocator    wsialloc allocator pointer.
    * @param[in] surface_formats  Supported surface formats from the presentation engine.
    * @param[in] physical_device  Vulkan physical device handle.
    * @param[in] allocator        User provided allocation callbacks.
    */
   swapchain_image_create_external_memory(std::optional<swapchain_image_create_compression_control> compression,
                                          swapchain_wsialloc_allocator *wsi_allocator,
                                          const util::vector<util::drm::drm_format_pair> *surface_formats,
                                          VkPhysicalDevice physical_device, const util::allocator &allocator)
      : m_compression(compression)
      , m_wsi_allocator(wsi_allocator)
      , m_surface_formats(surface_formats)
      , m_physical_device(physical_device)
      , m_allocator(allocator)
      , m_plane_layouts(allocator)
      , m_drm_mod_info()
      , m_external_info()
      , m_allocated_format()
   {
   }

   VkResult extend_image_create_info(VkImageCreateInfo *image_create_info) override;

   /**
    * @brief Finds what formats are compatible with the requested swapchain image Vulkan Device and backend surface.
    *
    * @param      info               The Swapchain image creation info.
    * @param[out] importable_formats A list of formats that can be imported to the Vulkan Device.
    * @param[out] exportable_formats A list of formats that can be exported from the Vulkan Device.
    * @param[out] drm_format_props   A list of all device supported VkDrmFormatModifierPropertiesEXT.
    *
    * @return VK_SUCCESS or VK_ERROR_OUT_OF_HOST_MEMORY
    */
   VkResult get_surface_compatible_formats(const VkImageCreateInfo &info,
                                           util::vector<wsialloc_format> &importable_formats,
                                           util::vector<uint64_t> &exportable_modifers,
                                           util::vector<VkDrmFormatModifierPropertiesEXT> &drm_format_props);

private:
   std::optional<swapchain_image_create_compression_control> m_compression;
   swapchain_wsialloc_allocator *m_wsi_allocator;
   const util::vector<util::drm::drm_format_pair> *m_surface_formats;
   VkPhysicalDevice m_physical_device;
   const util::allocator &m_allocator;

   util::vector<VkSubresourceLayout> m_plane_layouts;
   VkImageDrmFormatModifierExplicitCreateInfoEXT m_drm_mod_info;
   VkExternalMemoryImageCreateInfoKHR m_external_info;
   wsialloc_allocate_result m_allocated_format;
};

} /* namespace wsi */