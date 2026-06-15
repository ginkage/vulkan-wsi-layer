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
#include "wsi/image_backing_memory_external.hpp"
#include "wsi/wsi_alloc_utils.hpp"
#include "wsi/swapchain_base.hpp"
#include "wsi/extensions/present_id.hpp"
#include "wsi/extensions/external_memory_extension.hpp"
#include "wsi/extensions/image_compression_control.hpp"
#include "shm_presenter.hpp"
#include "dri3_presenter.hpp"
#include "drm_display.hpp"

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
   , m_last_present_msc(0)
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
   if (m_presenter)
   {
      for (auto &image : m_swapchain_images)
      {
         auto *data = image.get_data<x11_image_data>();
         if (data != nullptr)
         {
            m_presenter->destroy_image_resources(data);
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

   /* Prefer DRI3 + Present (zero-copy); fall back to MIT-SHM when the X server lacks DRI3/Present. */
   try
   {
      /* WSI_X11_FORCE_SHM forces the MIT-SHM path (CPU copy) - useful for A/B comparison against DRI3
       * and as an escape hatch for workloads that misbehave on DRI3. */
      const bool force_shm = (getenv("WSI_X11_FORCE_SHM") != nullptr);
      /* DRI3 strategy: default is true zero-copy (OPTION_NONE); set WSI_X11_DRI3_COPY to select
       * GPU-copy (OPTION_COPY) - the server blits the pixmap, trading a blit for deterministic
       * recycling. Pacing is separate (present mode), so the default is paced zero-copy (FIFO). */
      const bool dri3_copy = (getenv("WSI_X11_DRI3_COPY") != nullptr);
      auto dri3 = force_shm ? std::unique_ptr<dri3_presenter>() : std::make_unique<dri3_presenter>();
      if (dri3 != nullptr && dri3->is_available(m_connection, m_wsi_surface))
      {
         dri3->set_copy_mode(dri3_copy);
         m_presenter = std::move(dri3);
         m_use_dri3 = true;
         m_dri3_copy_mode = dri3_copy;
      }
      else
      {
         auto shm = std::make_unique<shm_presenter>();
         if (!shm->is_available(m_connection, m_wsi_surface))
         {
            WSI_LOG_ERROR("Neither DRI3 nor SHM presentation is available");
            return VK_ERROR_INITIALIZATION_FAILED;
         }
         m_presenter = std::move(shm);
         m_use_dri3 = false;
      }
   }
   catch (const std::exception &e)
   {
      WSI_LOG_ERROR("Exception creating presentation strategy: %s", e.what());
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Pacing follows the present mode (FIFO/FIFO_RELAXED -> paced). GPU-copy + unpaced is the only cell
    * that needs the fixed deferred-release pipeline (buffers must free immediately so the app can run
    * ahead); every other cell recycles via PresentIdleNotify. */
   const bool paced = (m_present_mode == VK_PRESENT_MODE_FIFO_KHR ||
                       m_present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
   m_dri3_deferred_release = m_dri3_copy_mode && !paced;

   /* Log the chosen cell once per process: per-swapchain state but constant in practice, and logging
    * it on every swapchain creation floods the log for apps that recreate them often. */
   static bool logged_presentation = false;
   if (!logged_presentation)
   {
      logged_presentation = true;
      if (m_use_dri3)
      {
         WSI_LOG_INFO("X11 swapchain using DRI3 presentation (%s, %s)",
                      m_dri3_copy_mode ? "GPU-copy" : "zero-copy", paced ? "paced" : "unpaced");
      }
      else
      {
         WSI_LOG_INFO("X11 swapchain using SHM presentation");
      }
   }

   /* DRI3 presents GPU-local dma-buf images, so it needs the wsialloc allocator. The SHM path uses
    * host-visible device memory and skips it. */
   if (m_use_dri3)
   {
      auto wsi_allocator = swapchain_wsialloc_allocator::create();
      if (!wsi_allocator.has_value())
      {
         WSI_LOG_ERROR("Failed to create wsialloc allocator for DRI3");
         return VK_ERROR_INITIALIZATION_FAILED;
      }
      m_wsi_allocator = m_allocator.make_unique<swapchain_wsialloc_allocator>(std::move(wsi_allocator.value()));
      if (m_wsi_allocator == nullptr)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   TRY_LOG_CALL(init_image_factory(*swapchain_create_info));

   VkResult init_result = m_presenter->init(m_connection, m_window, m_wsi_surface);
   if (init_result != VK_SUCCESS)
   {
      WSI_LOG_ERROR("Failed to initialize presentation strategy");
      return init_result;
   }

   /* DRI3 drives image recycling from Present events on this queue; null for SHM. */
   m_present_special_event = m_presenter->get_present_special_event();

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

   if (m_use_dri3)
   {
      /* DRI3: GPU-local dma-buf images shared with the X server, mirroring the display backend.
       * Supported formats come from the DRM device (lazily opened, /dev/dri/card0 by default). */
      auto &display = drm_display::get_display();
      if (!display.has_value())
      {
         WSI_LOG_ERROR("DRM display not available for DRI3 image allocation");
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      auto compression_control = image_create_compression_control::create(m_device, &swapchain_create_info);
      auto sc_img_create_ext_mem_result = swapchain_image_create_external_memory::create(
         image_handle_creator->get_image_create_info(), compression_control, *m_wsi_allocator,
         *display->get_supported_formats(), m_device_data.physical_device, m_allocator);
      if (auto error = std::get_if<VkResult>(&sc_img_create_ext_mem_result))
      {
         return *error;
      }
      auto sc_img_create_ext_mem =
         std::get<util::unique_ptr<swapchain_image_create_external_memory>>(std::move(sc_img_create_ext_mem_result));

      auto external_image_create_info = sc_img_create_ext_mem->get_external_image_create_info();
      TRY_LOG_CALL(image_handle_creator->add_extension(std::move(sc_img_create_ext_mem)));

      wsialloc_create_info_args wsialloc_args = { external_image_create_info.selected_format,
                                                  external_image_create_info.flags, external_image_create_info.extent,
                                                  external_image_create_info.explicit_compression };
      auto backing_memory_creator =
         m_allocator.make_unique<external_image_backing_memory_creator>(m_device_data, *m_wsi_allocator, wsialloc_args);
      if (backing_memory_creator == nullptr)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      /* X11 DRI3 is implicit-sync (no surface sync interface): non-exportable fence, CPU-wait the
       * present fence so the server never samples a half-rendered buffer. */
      m_image_factory.init(std::move(image_handle_creator), std::move(backing_memory_creator), false, true);
      return VK_SUCCESS;
   }

   /* SHM: host-visible, linearly-tiled images the CPU reads. */
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

   uint32_t width = image_create_info.extent.width;
   uint32_t height = image_create_info.extent.height;
   int depth = 24;
   uint32_t surface_width = 0;
   uint32_t surface_height = 0;
   if (!m_wsi_surface->get_size_and_depth(&surface_width, &surface_height, &depth))
   {
      WSI_LOG_WARNING("Could not get surface depth, using default: %d", depth);
   }

   if (m_use_dri3)
   {
      /* DRI3: allocate the GPU-local dma-buf, wrap it as an X pixmap (before the Vulkan import, so
       * the fds are still ours to duplicate), then import and bind. */
      auto &backing = swapchain_image_factory::get_backing_memory_from_image<image_backing_memory_external>(image);
      TRY_LOG_CALL(backing.allocate());
      TRY_LOG(m_presenter->create_image_resources(image, image_data, width, height, depth),
              "Failed to create DRI3 pixmap resources");
      TRY_LOG_CALL(backing.import_and_bind(image.get_image()));
   }
   else
   {
      /* SHM: allocate host-visible memory, bind it to the (linear) image, set up the SHM segments. */
      const VkMemoryPropertyFlags optimal = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      TRY_LOG_CALL(image_data->external_mem.configure_for_host_visible(image_create_info, required, optimal));
      TRY_LOG_CALL(image_data->external_mem.allocate_and_bind_image(image.get_image(), image_create_info));
      TRY_LOG(m_presenter->create_image_resources(image, image_data, width, height, depth),
              "Failed to create SHM image resources");
   }

   image.set_data(std::move(image_data_ptr));
   image.set_status(swapchain_image::FREE);
   return VK_SUCCESS;
}

void swapchain::present_event_thread()
{
   auto thread_status_lock = std::unique_lock<std::mutex>(m_thread_status_lock);
   m_present_event_thread_run = true;

   if (m_use_dri3)
   {
      /* DRI3: drain Present events. A PresentIdleNotify means the server is done reading that pixmap,
       * so the image can be recycled - hand the pixmap to free_image_found via the free-buffer pool. */
      while (m_present_event_thread_run)
      {
         thread_status_lock.unlock();
         xcb_generic_event_t *event = xcb_poll_for_special_event(m_connection, m_present_special_event);
         thread_status_lock.lock();

         if (event != nullptr)
         {
            auto *generic = reinterpret_cast<xcb_present_generic_event_t *>(event);
            if (generic->evtype == XCB_PRESENT_IDLE_NOTIFY)
            {
               /* The unpaced-GPU-copy cell recycles via the fixed deferred-release pipeline in
                * present_image, so just drain the idle event there; every other cell hands the pixmap
                * back through the free-buffer pool. */
               if (!m_dri3_deferred_release)
               {
                  auto *idle = reinterpret_cast<xcb_present_idle_notify_event_t *>(event);
                  if (!m_free_buffer_pool.push_back(idle->pixmap))
                  {
                     WSI_LOG_ERROR("DRI3: free buffer pool full, dropping idle pixmap");
                  }
                  m_thread_status_cond.notify_all();
               }
            }
            else if (generic->evtype == XCB_PRESENT_COMPLETE_NOTIFY)
            {
               /* Track the vsync count so present_image can pace the next frame (FIFO target_msc). */
               auto *complete = reinterpret_cast<xcb_present_complete_notify_event_t *>(event);
               m_last_present_msc = complete->msc;
               m_thread_status_cond.notify_all();
            }
            free(event);
            continue;
         }

         if (xcb_connection_has_error(m_connection))
         {
            break;
         }

         /* No event ready: brief wait (also bounds shutdown latency - the destructor notifies us). */
         m_thread_status_cond.wait_for(thread_status_lock, std::chrono::milliseconds(2));
      }

      m_present_event_thread_run = false;
      m_thread_status_cond.notify_all();
      return;
   }

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

   m_send_sbc++;
   uint32_t serial = static_cast<uint32_t>(m_send_sbc);

   /* FIFO: schedule each frame one vsync past the last completed present (strictly increasing) so the
    * server paces presents to the display refresh instead of releasing them in bursts. Other present
    * modes present as soon as possible (target_msc 0). If no Complete events arrive, m_last_present_msc
    * stays 0 and the targets fall in the past, degrading gracefully to as-soon-as-possible. */
   uint64_t target_msc = 0;
   if (m_present_mode == VK_PRESENT_MODE_FIFO_KHR || m_present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
   {
      m_target_msc = m_target_msc + 1;
      if (m_last_present_msc + 1 > m_target_msc)
      {
         m_target_msc = m_last_present_msc + 1;
      }
      target_msc = m_target_msc;
   }

   VkResult present_result = m_presenter->present_image(image_data, serial, target_msc);
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

   if (m_dri3_deferred_release)
   {
      /* Unpaced GPU-copy: the server has copied the pixmap (OPTION_COPY), so recycle on a fixed
       * pipeline - free image N-DRI3_DEFER_FRAMES now - instead of waiting for PresentIdleNotify,
       * whose latency is bimodal (fast when the compositor composites, ~a frame when it flips). This
       * frees buffers immediately so the app runs ahead (MAILBOX). unpresent_image under the lock
       * matches free_image_found; notify so a waiting acquire re-checks. */
      int oldest = m_dri3_deferred[m_dri3_defer_head];
      m_dri3_deferred[m_dri3_defer_head] = static_cast<int>(pending_present.image_index);
      m_dri3_defer_head = (m_dri3_defer_head + 1) % DRI3_DEFER_FRAMES;
      if (oldest >= 0)
      {
         unpresent_image(static_cast<uint32_t>(oldest));
         m_thread_status_cond.notify_all();
      }
   }
   else if (!m_use_dri3)
   {
      /* SHM completes synchronously inside present_image, so the image is free to reuse now. DRI3
       * leaves it presented until its PresentIdleNotify recycles it (present_event_thread ->
       * m_free_buffer_pool -> free_image_found). */
      thread_status_lock.unlock();
      unpresent_image(pending_present.image_index);
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
