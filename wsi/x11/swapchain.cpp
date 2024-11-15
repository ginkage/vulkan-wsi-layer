/*
 * Copyright (c) 2017-2022 Arm Limited.
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
 * @file swapchain.cpp
 *
 * @brief Contains the implementation for a x11 swapchain.
 */

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <system_error>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <util/timed_semaphore.hpp>
#include <vulkan/vulkan_core.h>

#include <xcb/present.h>
#include <xcb/dri3.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "drm_display.hpp"
#include "swapchain.hpp"
#include "util/log.hpp"
#include "util/macros.hpp"
#include "wsi/external_memory.hpp"
#include "wsi/swapchain_base.hpp"

namespace wsi
{
namespace x11
{

#define X11_SWAPCHAIN_MAX_PENDING_COMPLETIONS 128

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator, surface &wsi_surface)
   : swapchain_base(dev_data, pAllocator)
   , m_connection(wsi_surface.get_connection())
   , m_window(wsi_surface.get_window())
   , m_wsi_surface(&wsi_surface)
   , m_wsi_allocator(nullptr)
   , m_image_creation_parameters({}, m_allocator, {}, {})
   , m_send_sbc(0)
   , m_target_msc(0)
   , m_last_present_msc(0)
   , m_thread_status_lock()
   , m_thread_status_cond()
{
   m_image_create_info.format = VK_FORMAT_UNDEFINED;
}

swapchain::~swapchain()
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   if (m_present_event_thread_run)
   {
      m_present_event_thread_run = false;
      m_thread_status_cond.notify_all();
      thread_status_lock.unlock();

      if (m_present_event_thread.joinable())
      {
         m_present_event_thread.join();
      }

      thread_status_lock.lock();
   }

   xcb_unregister_for_special_event(m_connection, m_special_event);

   thread_status_lock.unlock();

   /* Call the base's teardown */
   teardown();
}

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread)
{
   UNUSED(device);
   UNUSED(swapchain_create_info);
   m_device_data.instance_data.disp.GetPhysicalDeviceMemoryProperties2KHR(m_device_data.physical_device,
                                                                          &m_memory_props);
   if (m_wsi_surface == nullptr)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   WSIALLOC_ASSERT_VERSION();
   if (wsialloc_new(&m_wsi_allocator) != WSIALLOC_ERROR_NONE)
   {
      WSI_LOG_ERROR("Failed to create wsi allocator.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   auto eid = xcb_generate_id(m_connection);
   m_special_event = xcb_register_for_special_xge(m_connection, &xcb_present_id, eid, nullptr);
   xcb_present_select_input(m_connection, eid, m_window,
                            XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY | XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
                               XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY);

   try
   {
      m_present_event_thread = std::thread(&swapchain::present_event_thread, this);
   }
   catch (const std::system_error &)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   catch (const std::bad_alloc &)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /*
    * When VK_PRESENT_MODE_MAILBOX_KHR has been chosen by the application we don't
    * initialize the page flip thread so the present_image function can be called
    * during vkQueuePresent.
    */
   use_presentation_thread = (m_present_mode != VK_PRESENT_MODE_MAILBOX_KHR);

   return VK_SUCCESS;
}

VkResult swapchain::get_surface_compatible_formats(const VkImageCreateInfo &info,
                                                   util::vector<wsialloc_format> &importable_formats,
                                                   util::vector<uint64_t> &exportable_modifers,
                                                   util::vector<VkDrmFormatModifierPropertiesEXT> &drm_format_props)
{
   TRY_LOG(util::get_drm_format_properties(m_device_data.physical_device, info.format, drm_format_props),
           "Failed to get format properties");

   auto &display = drm_display::get_display();
   if (!display.has_value())
   {
      WSI_LOG_ERROR("DRM display not available.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (const auto &prop : drm_format_props)
   {
      drm_format_pair drm_format{ util::drm::vk_to_drm_format(info.format), prop.drmFormatModifier };

      if (!display->is_format_supported(drm_format))
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

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
         VkImageCompressionControlEXT compression_control = {};
         compression_control.sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT;
         compression_control.flags = m_image_compression_control_params.flags;
         compression_control.compressionControlPlaneCount =
            m_image_compression_control_params.compression_control_plane_count;
         compression_control.pFixedRateFlags = m_image_compression_control_params.fixed_rate_flags.data();

         if (m_device_data.is_swapchain_compression_control_enabled())
         {
            compression_control.pNext = image_info.pNext;
            image_info.pNext = &compression_control;
         }
#endif
         result = m_device_data.instance_data.disp.GetPhysicalDeviceImageFormatProperties2KHR(
            m_device_data.physical_device, &image_info, &format_props);
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

VkResult swapchain::allocate_wsialloc(VkImageCreateInfo &image_create_info, x11_image_data *image_data,
                                      util::vector<wsialloc_format> &importable_formats,
                                      wsialloc_format *allocated_format, bool avoid_allocation)
{
   bool is_protected_memory = (image_create_info.flags & VK_IMAGE_CREATE_PROTECTED_BIT) != 0;
   uint64_t allocation_flags = is_protected_memory ? WSIALLOC_ALLOCATE_PROTECTED : 0;
   if (avoid_allocation)
   {
      allocation_flags |= WSIALLOC_ALLOCATE_NO_MEMORY;
   }

#if WSI_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN
   if (m_image_compression_control_params.flags & VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT)
   {
      allocation_flags |= WSIALLOC_ALLOCATE_HIGHEST_FIXED_RATE_COMPRESSION;
   }
#endif

   wsialloc_allocate_info alloc_info = { importable_formats.data(), static_cast<unsigned>(importable_formats.size()),
                                         image_create_info.extent.width, image_create_info.extent.height,
                                         allocation_flags };

   wsialloc_allocate_result alloc_result = { {}, { 0 }, { 0 }, { -1 }, false };
   /* Clear buffer_fds and average_row_strides for error purposes */
   for (int i = 0; i < WSIALLOC_MAX_PLANES; ++i)
   {
      alloc_result.buffer_fds[i] = -1;
      alloc_result.average_row_strides[i] = -1;
   }
   const auto res = wsialloc_alloc(m_wsi_allocator, &alloc_info, &alloc_result);
   if (res != WSIALLOC_ERROR_NONE)
   {
      WSI_LOG_ERROR("Failed allocation of DMA Buffer. WSI error: %d", static_cast<int>(res));
      if (res == WSIALLOC_ERROR_NOT_SUPPORTED)
      {
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   *allocated_format = alloc_result.format;
   auto &external_memory = image_data->external_mem;
   external_memory.set_strides(alloc_result.average_row_strides);
   external_memory.set_buffer_fds(alloc_result.buffer_fds);
   external_memory.set_offsets(alloc_result.offsets);

   uint32_t num_planes = util::drm::drm_fourcc_format_get_num_planes(alloc_result.format.fourcc);

   if (!avoid_allocation)
   {
      uint32_t num_memory_planes = 0;

      for (uint32_t i = 0; i < num_planes; ++i)
      {
         auto it = std::find(std::begin(alloc_result.buffer_fds) + i + 1, std::end(alloc_result.buffer_fds),
                             alloc_result.buffer_fds[i]);
         if (it == std::end(alloc_result.buffer_fds))
         {
            num_memory_planes++;
         }
      }

      assert(alloc_result.is_disjoint == (num_memory_planes > 1));
      external_memory.set_num_memories(num_memory_planes);
   }

   external_memory.set_format_info(alloc_result.is_disjoint, num_planes);
   external_memory.set_memory_handle_type(VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
   return VK_SUCCESS;
}

static VkResult fill_image_create_info(VkImageCreateInfo &image_create_info,
                                       util::vector<VkSubresourceLayout> &image_plane_layouts,
                                       VkImageDrmFormatModifierExplicitCreateInfoEXT &drm_mod_info,
                                       VkExternalMemoryImageCreateInfoKHR &external_info,
                                       x11_image_data &image_data, uint64_t modifier)
{
   TRY_LOG_CALL(image_data.external_mem.fill_image_plane_layouts(image_plane_layouts));

   if (image_data.external_mem.is_disjoint())
   {
      image_create_info.flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
   }

   image_data.external_mem.fill_drm_mod_info(image_create_info.pNext, drm_mod_info, image_plane_layouts, modifier);
   image_data.external_mem.fill_external_info(external_info, &drm_mod_info);
   image_create_info.pNext = &external_info;
   image_create_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
   return VK_SUCCESS;
}

VkResult swapchain::allocate_image(VkImageCreateInfo &image_create_info, x11_image_data *image_data)
{
   UNUSED(image_create_info);
   util::vector<wsialloc_format> importable_formats(util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
   auto &m_allocated_format = m_image_creation_parameters.m_allocated_format;
   if (!importable_formats.try_push_back(m_allocated_format))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   TRY_LOG_CALL(allocate_wsialloc(m_image_create_info, image_data, importable_formats, &m_allocated_format, false));

   return VK_SUCCESS;
}

static int os_dupfd_cloexec(int fd)
{
   int minfd = 3;
   int newfd = fcntl(fd, F_DUPFD_CLOEXEC, minfd);

   if (newfd >= 0)
      return newfd;

   if (errno != EINVAL)
      return -1;

   newfd = fcntl(fd, F_DUPFD, minfd);

   if (newfd < 0)
      return -1;

   long flags = fcntl(newfd, F_GETFD);
   if (flags == -1) {
      close(newfd);
      return -1;
   }

   if (fcntl(newfd, F_SETFD, flags | FD_CLOEXEC) == -1) {
      close(newfd);
      return -1;
   }

   return newfd;
}

VkResult swapchain::create_pixmap(const VkImageCreateInfo &image_create_info, swapchain_image &image,
                                  x11_image_data *image_data)
{
   UNUSED(image);
   auto &mem = image_data->external_mem;
   auto &offset = mem.get_offsets();
   auto &stride = mem.get_strides();
   auto pixmap = xcb_generate_id(m_connection);

   int fds[4] = { -1, -1, -1, -1 };
   for (uint32_t i = 0; i < mem.get_num_planes(); i++) {
      fds[i] = os_dupfd_cloexec(mem.get_buffer_fds()[i]);
      if (fds[i] == -1) {
         for (uint32_t j = 0; j < i; j++)
            close(fds[j]);

         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   auto cookie = xcb_dri3_pixmap_from_buffers_checked(
      m_connection, pixmap, m_window,
      mem.get_num_planes(),
      image_create_info.extent.width, image_create_info.extent.height,
      stride[0], offset[0], stride[1], offset[1], stride[2], offset[2], stride[3], offset[3],
      24, 32, m_image_creation_parameters.m_allocated_format.modifier, &fds[0]);

   auto error = xcb_request_check(m_connection, cookie);
   if (error)
   {
      free(error);
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   image_data->pixmap = pixmap;
   return VK_SUCCESS;
}

VkResult swapchain::allocate_and_bind_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image)
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);
   image.status = swapchain_image::FREE;

   assert(image.data != nullptr);
   auto image_data = static_cast<x11_image_data *>(image.data);
   TRY_LOG(allocate_image(image_create_info, image_data), "Failed to allocate image");
   image_status_lock.unlock();

   TRY_LOG(create_pixmap(image_create_info, image, image_data), "Failed to create pixmap");

   TRY_LOG(image_data->external_mem.import_memory_and_bind_swapchain_image(image.image),
           "Failed to import memory and bind swapchain image");

   /* Initialize presentation fence. */
   auto present_fence = sync_fd_fence_sync::create(m_device_data);
   if (!present_fence.has_value())
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   image_data->present_fence = std::move(present_fence.value());

   return VK_SUCCESS;
}

VkResult swapchain::create_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image)
{
   /* Create image_data */
   auto image_data = m_allocator.create<x11_image_data>(1, m_device, m_allocator);
   if (image_data == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   image.data = image_data;

   if (m_image_create_info.format == VK_FORMAT_UNDEFINED)
   {
      util::vector<wsialloc_format> importable_formats(
         util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));
      util::vector<uint64_t> exportable_modifiers(util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));

      /* Query supported modifers. */
      util::vector<VkDrmFormatModifierPropertiesEXT> drm_format_props(
         util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND));

      TRY_LOG_CALL(
         get_surface_compatible_formats(image_create_info, importable_formats, exportable_modifiers, drm_format_props));

      /* TODO: Handle exportable images which use ICD allocated memory in preference to an external allocator. */
      if (importable_formats.empty())
      {
         WSI_LOG_ERROR("Export/Import not supported.");
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      wsialloc_format allocated_format = { 0, 0, 0 };
      TRY_LOG_CALL(allocate_wsialloc(image_create_info, image_data, importable_formats, &allocated_format, true));

      for (auto &prop : drm_format_props)
      {
         if (prop.drmFormatModifier == allocated_format.modifier)
         {
            image_data->external_mem.set_num_memories(prop.drmFormatModifierPlaneCount);
         }
      }

      TRY_LOG_CALL(fill_image_create_info(
         image_create_info, m_image_creation_parameters.m_image_layout, m_image_creation_parameters.m_drm_mod_info,
         m_image_creation_parameters.m_external_info, *image_data, allocated_format.modifier));

      m_image_create_info = image_create_info;
      m_image_creation_parameters.m_allocated_format = allocated_format;
   }

   return m_device_data.disp.CreateImage(m_device, &m_image_create_info, get_allocation_callbacks(), &image.image);
}

void swapchain::present_event_thread()
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);
   m_present_event_thread_run = true;

   while (m_present_event_thread_run)
   {
      auto assume_forward_progress = false;

      for (auto &image : m_swapchain_images)
      {
         if (image.status == swapchain_image::INVALID)
            continue;

         auto data = reinterpret_cast<x11_image_data *>(image.data);
         if (data->pending_completions.size() != 0)
         {
            assume_forward_progress = true;
            break;
         }
      }

      if (!assume_forward_progress)
      {
         m_thread_status_cond.wait(thread_status_lock);
         continue;
      }

      if (error_has_occured())
      {
         break;
      }

      thread_status_lock.unlock();

      auto event = xcb_wait_for_special_event(m_connection, m_special_event);
      if (event == nullptr)
      {
         set_error_state(VK_ERROR_SURFACE_LOST_KHR);
         break;
      }

      thread_status_lock.lock();

      auto pe = reinterpret_cast<xcb_present_generic_event_t *>(event);
      switch (pe->evtype)
      {
      case XCB_PRESENT_EVENT_CONFIGURE_NOTIFY:
      {
         auto config = reinterpret_cast<xcb_present_configure_notify_event_t *>(event);
         if (config->pixmap_flags & (1 << 0))
         {
            set_error_state(VK_ERROR_SURFACE_LOST_KHR);
         }
         else if (config->width != m_image_create_info.extent.width ||
                  config->height != m_image_create_info.extent.height)
         {
            set_error_state(VK_SUBOPTIMAL_KHR);
         }
         break;
      }
      case XCB_PRESENT_EVENT_IDLE_NOTIFY:
      {
         auto idle = reinterpret_cast<xcb_present_idle_notify_event_t *>(event);
         m_free_buffer_pool.push_back(idle->pixmap);
         m_thread_status_cond.notify_all();
         break;
      }
      case XCB_PRESENT_EVENT_COMPLETE_NOTIFY:
      {
         auto complete = reinterpret_cast<xcb_present_complete_notify_event_t *>(event);
         if (complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP)
         {
            for (auto &image : m_swapchain_images)
            {
               auto data = reinterpret_cast<x11_image_data *>(image.data);
               auto iter = std::find_if(data->pending_completions.begin(), data->pending_completions.end(),
                                        [complete](auto &pending_completion) -> bool {
                                           return complete->serial == pending_completion.serial;
                                        });
               if (iter != data->pending_completions.end())
               {
                  set_present_id(iter->present_id);
                  data->pending_completions.erase(iter);
                  m_thread_status_cond.notify_all();
               }
            }
            m_last_present_msc = complete->msc;
         }
         break;
      }
      }
      free(event);
   }

   m_present_event_thread_run = false;
   m_thread_status_cond.notify_all();
}

void swapchain::present_image(const pending_present_request &pending_present)
{
   auto image_data = reinterpret_cast<x11_image_data *>(m_swapchain_images[pending_present.image_index].data);
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   while (image_data->pending_completions.size() == X11_SWAPCHAIN_MAX_PENDING_COMPLETIONS)
   {
      if (!m_present_event_thread_run)
      {
         set_present_id(pending_present.present_id);
         return unpresent_image(pending_present.image_index);
      }
      m_thread_status_cond.wait(thread_status_lock);
   }

   m_send_sbc++;
   uint32_t serial = (uint32_t)m_send_sbc;
   uint32_t options = XCB_PRESENT_OPTION_NONE;

   auto cookie = xcb_present_pixmap_checked(m_connection, m_window, image_data->pixmap, serial, 0, 0, 0, 0, 0, 0, 0,
                                            options, m_target_msc, 0, 0, 0, nullptr);
   xcb_discard_reply(m_connection, cookie.sequence);
   xcb_flush(m_connection);

   image_data->pending_completions.push_back({ serial, pending_present.present_id });
   m_thread_status_cond.notify_all();

   if (m_present_mode == VK_PRESENT_MODE_FIFO_KHR)
   {
      while (image_data->pending_completions.size() > 0)
      {
         if (!m_present_event_thread_run)
         {
            return;
         }
         m_thread_status_cond.wait(thread_status_lock);
      }
      m_target_msc = m_last_present_msc + 1;
   }
}

bool swapchain::free_image_found()
{
   while (m_free_buffer_pool.size() > 0)
   {
      auto pixmap = m_free_buffer_pool.pop_front();
      assert(pixmap.has_value());
      for (size_t i = 0; i < m_swapchain_images.size(); i++)
      {
         auto data = reinterpret_cast<x11_image_data *>(m_swapchain_images[i].data);
         if (data->pixmap == pixmap.value())
         {
            unpresent_image(i);
         }
      }
   }

   for (auto &img : m_swapchain_images)
   {
      if (img.status == swapchain_image::FREE)
      {
         return true;
      }
   }
   return false;
}

VkResult swapchain::get_free_buffer(uint64_t *timeout)
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   if (*timeout == 0)
   {
      return free_image_found() ? VK_SUCCESS : VK_NOT_READY;
   }
   else if (*timeout == UINT64_MAX)
   {
      while (!free_image_found())
      {
         if (!m_present_event_thread_run)
            return VK_ERROR_OUT_OF_DATE_KHR;

         m_thread_status_cond.wait(thread_status_lock);
      }
   }
   else
   {
      auto time_point = std::chrono::high_resolution_clock::now() + std::chrono::nanoseconds(*timeout);

      while (!free_image_found())
      {
         if (!m_present_event_thread_run)
            return VK_ERROR_OUT_OF_DATE_KHR;

         if (m_thread_status_cond.wait_until(thread_status_lock, time_point) == std::cv_status::timeout)
         {
            return VK_TIMEOUT;
         }
      }
   }

   *timeout = 0;
   return VK_SUCCESS;
}

void swapchain::destroy_image(wsi::swapchain_image &image)
{
   std::unique_lock<std::recursive_mutex> image_status_lock(m_image_status_mutex);
   if (image.status != wsi::swapchain_image::INVALID)
   {
      if (image.image != VK_NULL_HANDLE)
      {
         m_device_data.disp.DestroyImage(m_device, image.image, get_allocation_callbacks());
         image.image = VK_NULL_HANDLE;
      }

      image.status = wsi::swapchain_image::INVALID;
   }

   image_status_lock.unlock();

   if (image.data != nullptr)
   {
      auto data = reinterpret_cast<x11_image_data *>(image.data);
      if (data->pixmap)
      {
         xcb_free_pixmap(m_connection, data->pixmap);
      }
      m_allocator.destroy(1, data);
      image.data = nullptr;
   }
}

VkResult swapchain::image_set_present_payload(swapchain_image &image, VkQueue queue,
                                              const queue_submit_semaphores &semaphores, const void *submission_pnext)
{
   auto data = reinterpret_cast<x11_image_data *>(image.data);
   return data->present_fence.set_payload(queue, semaphores, submission_pnext);
}

VkResult swapchain::image_wait_present(swapchain_image &image, uint64_t timeout)
{
   auto data = reinterpret_cast<x11_image_data *>(image.data);
   return data->present_fence.wait_payload(timeout);
}

VkResult swapchain::bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                         const VkBindImageMemorySwapchainInfoKHR *bind_sc_info)
{
   UNUSED(device);
   const wsi::swapchain_image &swapchain_image = m_swapchain_images[bind_sc_info->imageIndex];
   auto image_data = reinterpret_cast<x11_image_data *>(swapchain_image.data);
   return image_data->external_mem.bind_swapchain_image_memory(bind_image_mem_info->image);
}

} /* namespace x11 */
} /* namespace wsi */
