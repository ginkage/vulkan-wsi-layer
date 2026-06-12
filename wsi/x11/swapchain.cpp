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
#include <thread>

#include <unistd.h>
#include <vulkan/vulkan_core.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "swapchain.hpp"
#include "util/log.hpp"
#include "util/macros.hpp"
#include "wsi/external_memory.hpp"
#include "wsi/swapchain_base.hpp"
#include "wsi/extensions/present_id.hpp"
#include "shm_presenter.hpp"

namespace wsi
{
namespace x11
{

#define X11_SWAPCHAIN_MAX_PENDING_COMPLETIONS 128

swapchain::swapchain(layer::device_private_data &dev_data, const VkAllocationCallbacks *pAllocator,
                     surface &wsi_surface)
   : swapchain_base(dev_data, pAllocator)
   , m_connection(wsi_surface.get_connection())
   , m_window(wsi_surface.get_window())
   , m_wsi_surface(&wsi_surface)
   , m_image_factory(m_allocator, m_device_data)
   , m_send_sbc(0)
   , m_target_msc(0)
   , m_present_event_thread_run(false)
   , m_thread_status_lock()
   , m_thread_status_cond()
{
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

   thread_status_lock.unlock();

   /* Release the per-image SHM resources while the presenter (and its xcb connection) is still alive.
    * The host-visible image memory is freed by each x11_image_data's external_memory during teardown. */
   if (m_shm_presenter)
   {
      for (auto &image : m_swapchain_images)
      {
         auto *data = image.get_data<x11_image_data>();
         if (data != nullptr)
         {
            m_shm_presenter->destroy_image_resources(data);
         }
      }
   }

   /* Call the base's teardown */
   teardown();
}

VkResult swapchain::init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread)
{
   UNUSED(device);

   if (m_wsi_surface == nullptr)
   {
      WSI_LOG_ERROR("X11 swapchain init_platform: m_wsi_surface is null");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   TRY_LOG_CALL(init_image_factory(*swapchain_create_info));

   try
   {
      m_shm_presenter = std::make_unique<shm_presenter>();

      if (!m_shm_presenter->is_available(m_connection, m_wsi_surface))
      {
         WSI_LOG_ERROR("SHM presenter is not available");
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      VkResult init_result = m_shm_presenter->init(m_connection, m_window, m_wsi_surface);
      if (init_result != VK_SUCCESS)
      {
         WSI_LOG_ERROR("Failed to initialize SHM presenter");
         return init_result;
      }
   }
   catch (const std::exception &e)
   {
      WSI_LOG_ERROR("Exception creating presentation strategy: %s", e.what());

      return VK_ERROR_INITIALIZATION_FAILED;
   }

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

VkResult swapchain::init_image_factory(const VkSwapchainCreateInfoKHR &swapchain_create_info)
{
   auto image_handle_creator = m_allocator.make_unique<vulkan_image_handle_creator>(m_allocator, swapchain_create_info);
   if (image_handle_creator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* The X11 SHM presenter reads the rendered image on the CPU, so the images must be linearly tiled. */
   auto linear_ext = m_allocator.make_unique<linear_tiling_extension>();
   if (linear_ext == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   TRY_LOG_CALL(image_handle_creator->add_extension(std::move(linear_ext)));

   auto backing_memory_creator = m_allocator.make_unique<device_backing_memory_creator>(m_device_data);
   if (backing_memory_creator == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* exportable_fence=false, wait_on_present_fence=true: the X11 SHM path is implicit-sync. The base
    * CPU-waits on the present fence before present_image, so the rendered image is ready to copy. The
    * real (host-visible) binding is done in allocate_and_bind_swapchain_image; the device backing
    * memory created here is unused. */
   m_image_factory.init(std::move(image_handle_creator), std::move(backing_memory_creator), false, true);
   return VK_SUCCESS;
}

swapchain_image_factory &swapchain::get_image_factory()
{
   return m_image_factory;
}

VkResult swapchain::allocate_and_bind_swapchain_image(swapchain_image &image)
{
   auto image_data_ptr = m_allocator.make_unique<x11_image_data>(m_device, m_allocator);
   if (image_data_ptr == nullptr)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   x11_image_data *image_data = image_data_ptr.get();
   image_data->device_data = &m_device_data;

   const auto image_create_info = get_image_factory().get_image_handle_creator().get_image_create_info();

   /* Allocate host-visible memory and bind it to the (linear) image created by the factory. */
   /* Prefer HOST_CACHED so the per-frame CPU copy reads from cached (not write-combined) memory - the
    * dominant cost of SHM present. The cached type may be non-coherent, so external_memory invalidates
    * the cache before reading (see invalidate_host_memory). */
   const VkMemoryPropertyFlags optimal = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
   const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   TRY_LOG_CALL(image_data->external_mem.configure_for_host_visible(image_create_info, required, optimal));
   TRY_LOG_CALL(image_data->external_mem.allocate_and_bind_image(image.get_image(), image_create_info));

   /* Set up the X11 shared-memory resources for this image. */
   uint32_t width = image_create_info.extent.width;
   uint32_t height = image_create_info.extent.height;
   int depth = 24;
   uint32_t surface_width = 0;
   uint32_t surface_height = 0;
   if (!m_wsi_surface->get_size_and_depth(&surface_width, &surface_height, &depth))
   {
      WSI_LOG_WARNING("Could not get surface depth, using default: %d", depth);
   }
   TRY_LOG(m_shm_presenter->create_image_resources(image_data, width, height, depth),
           "Failed to create presentation image resources");

   image.set_data(std::move(image_data_ptr));
   image.set_status(swapchain_image::FREE);
   return VK_SUCCESS;
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
         if (image.get_status() == swapchain_image::UNALLOCATED)
         {
            continue;
         }

         auto data = image.get_data<x11_image_data>();
         if (data != nullptr && data->pending_completions.size() != 0)
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

      thread_status_lock.lock();
      std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Short polling interval
   }

   m_present_event_thread_run = false;
   m_thread_status_cond.notify_all();
}

void swapchain::present_image(const pending_present_request &pending_present)
{
   auto image_data = m_swapchain_images[pending_present.image_index].get_data<x11_image_data>();
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);

   while (image_data->pending_completions.size() == X11_SWAPCHAIN_MAX_PENDING_COMPLETIONS)
   {
      if (!m_present_event_thread_run)
      {
         if (m_device_data.is_present_id_enabled())
         {
            auto *ext = get_swapchain_extension<wsi_ext_present_id>(true);
            ext->mark_delivered(pending_present.present_id);
         }
         return unpresent_image(pending_present.image_index);
      }
      m_thread_status_cond.wait(thread_status_lock);
   }

   m_send_sbc++;
   uint32_t serial = static_cast<uint32_t>(m_send_sbc);

   VkResult present_result = m_shm_presenter->present_image(image_data, serial);
   if (present_result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Failed to present image using presentation strategy: %d", present_result);
   }

   if (m_device_data.is_present_id_enabled())
   {
      auto *ext = get_swapchain_extension<wsi_ext_present_id>(true);
      ext->mark_delivered(pending_present.present_id);
   }

   m_thread_status_cond.notify_all();

   thread_status_lock.unlock();

   unpresent_image(pending_present.image_index);
}

bool swapchain::free_image_found()
{
   while (m_free_buffer_pool.size() > 0)
   {
      auto pixmap = m_free_buffer_pool.pop_front();
      assert(pixmap.has_value());
      for (size_t i = 0; i < m_swapchain_images.size(); i++)
      {
         auto data = m_swapchain_images[i].get_data<x11_image_data>();
         if (data != nullptr && data->pixmap == pixmap.value())
         {
            unpresent_image(static_cast<uint32_t>(i));
         }
      }
   }

   for (auto &img : m_swapchain_images)
   {
      if (img.get_status() == swapchain_image::FREE)
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
         {
            return VK_ERROR_OUT_OF_DATE_KHR;
         }

         m_thread_status_cond.wait(thread_status_lock);
      }
   }
   else
   {
      auto time_point = std::chrono::high_resolution_clock::now() + std::chrono::nanoseconds(*timeout);

      while (!free_image_found())
      {
         if (!m_present_event_thread_run)
         {
            return VK_ERROR_OUT_OF_DATE_KHR;
         }

         if (m_thread_status_cond.wait_until(thread_status_lock, time_point) == std::cv_status::timeout)
         {
            return VK_TIMEOUT;
         }
      }
   }

   *timeout = 0;
   return VK_SUCCESS;
}

VkResult swapchain::add_required_extensions(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info)
{
   UNUSED(device);
   UNUSED(swapchain_create_info);

   if (m_device_data.is_present_id_enabled())
   {
      if (!add_swapchain_extension(m_allocator.make_unique<wsi_ext_present_id>()))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   return VK_SUCCESS;
}

} /* namespace x11 */
} /* namespace wsi */
