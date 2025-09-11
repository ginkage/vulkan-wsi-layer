/*
 * Copyright (c) 2017-2025 Arm Limited.
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
 * @file swapchain_base.cpp
 *
 * @brief Contains the implementation for the swapchain.
 *
 * This file contains much of the swapchain implementation,
 * that is not specific to how images are created or presented.
 */

#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include <unistd.h>
#include <vulkan/vulkan.h>

#include "util/log.hpp"
#include "util/helpers.hpp"

#include "swapchain_base.hpp"
#include "swapchain_image_create_extensions/mutable_format_extension.hpp"
#include "wsi_factory.hpp"

#include "extensions/present_timing.hpp"
#include "extensions/swapchain_maintenance.hpp"

namespace wsi
{

void swapchain_base::page_flip_thread()
{
   auto &sc_images = m_swapchain_images;
   VkResult vk_res = VK_SUCCESS;
   uint64_t timeout = UINT64_MAX;
   constexpr uint64_t SEMAPHORE_TIMEOUT = 250000000; /* 250 ms. */

   /* No mutex is needed for the accesses to m_page_flip_thread_run variable as after the variable is
    * initialized it is only ever changed to false. The while loop will make the thread read the
    * value repeatedly, and the combination of semaphores and thread joins will force any changes to
    * the variable to be visible to this thread.
    */
   while (m_page_flip_thread_run)
   {
      pending_present_request submit_info{};
      if (m_present_mode == VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR)
      {
         /* In continuous mode the application will only make one presentation request,
          * therefore the page flip semaphore will only be signalled once. */
         if (m_first_present)
         {
            VkResult wait_res = m_page_flip_semaphore.wait(SEMAPHORE_TIMEOUT);
            if (wait_res == VK_TIMEOUT)
            {
               /* Image is not ready yet. */
               continue;
            }
            assert(wait_res == VK_SUCCESS);
         }

         /* For continuous mode there will be only one image in the swapchain.
          * This image will always be used, and there is no pending state in this case. */
         submit_info.image_index = 0;
      }
      else
      {
         /* Waiting for the page_flip_semaphore which will be signalled once there is an
          * image to display.*/
         {
            VkResult wait_res = m_page_flip_semaphore.wait(SEMAPHORE_TIMEOUT);
            if (wait_res == VK_TIMEOUT)
            {
               /* Image is not ready yet. */
               continue;
            }
         }

         /* We want to present the oldest queued for present image from our present queue,
          * which we can find at the sc->pending_buffer_pool.head index. */
         util::unique_lock<util::recursive_mutex> image_status_lock(m_image_status_mutex);
         if (!image_status_lock)
         {
            WSI_LOG_ERROR("Failed to acquire image status lock in page flip thread.");
            abort();
         }

         auto pending_submission = m_pending_buffer_pool.pop_front();
         assert(pending_submission.has_value());
         submit_info = *pending_submission;
      }

      /* We may need to wait for the payload of the present sync of the oldest pending image to be finished. */
      while ((vk_res = image_wait_present(sc_images[submit_info.image_index], timeout)) == VK_TIMEOUT)
      {
         WSI_LOG_WARNING("Timeout waiting for image's present fences, retrying..");
      }
      if (vk_res != VK_SUCCESS)
      {
         set_error_state(vk_res);
         m_free_image_semaphore.post();
         continue;
      }

      call_present(submit_info);
   }
}

void swapchain_base::call_present(const pending_present_request &pending_present)
{
   /* First present of the swapchain. If it has an ancestor, wait until all the
    * pending buffers from the ancestor have been presented. */
   if (m_first_present)
   {
      if (m_ancestor != VK_NULL_HANDLE)
      {
         auto *ancestor = reinterpret_cast<swapchain_base *>(m_ancestor);
         ancestor->wait_for_pending_buffers();
      }

      sem_post(&m_start_present_semaphore);

      present_image(pending_present);

      m_first_present = false;
   }
   /* The swapchain has already started presenting. */
   else
   {
      present_image(pending_present);
   }
}

bool swapchain_base::has_descendant_started_presenting()
{
   if (m_descendant == VK_NULL_HANDLE)
   {
      return false;
   }

   auto *desc = reinterpret_cast<swapchain_base *>(m_descendant);
   return desc->m_started_presenting;
}

VkResult swapchain_base::init_page_flip_thread()
{
   /* Setup semaphore for signaling pageflip thread */
   TRY_LOG_CALL(m_page_flip_semaphore.init(0));
   m_thread_sem_defined = true;

   /* Launch page flipping thread */
   m_page_flip_thread_run = true;
   try
   {
      m_page_flip_thread = std::thread(&swapchain_base::page_flip_thread, this);
   }
   catch (const std::system_error &)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   catch (const std::bad_alloc &)
   {
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   return VK_SUCCESS;
}

void swapchain_base::unpresent_image(uint32_t presented_index)
{
   util::unique_lock<util::recursive_mutex> image_status_lock(m_image_status_mutex);
   if (!image_status_lock)
   {
      WSI_LOG_ERROR("Failed to acquire image status lock in unpresent_image.");
      abort();
   }

   if (m_present_mode == VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR ||
       m_present_mode == VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR)
   {
      m_swapchain_images[presented_index].status = swapchain_image::ACQUIRED;
   }
   else
   {
      m_swapchain_images[presented_index].status = swapchain_image::FREE;
   }

   image_status_lock.unlock();

   if (m_present_mode != VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR &&
       m_present_mode != VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR)
   {
      m_free_image_semaphore.post();
   }
}

swapchain_base::swapchain_base(layer::device_private_data &dev_data, const VkAllocationCallbacks *callbacks)
   : m_device_data(dev_data)
   , m_page_flip_thread_run(false)
   , m_start_present_semaphore()
   , m_thread_sem_defined(false)
   , m_first_present(true)
   , m_pending_buffer_pool()
   , m_allocator(dev_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, callbacks)
   , m_swapchain_images(m_allocator)
   , m_surface(VK_NULL_HANDLE)
   , m_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
   , m_present_modes(m_allocator)
   , m_descendant(VK_NULL_HANDLE)
   , m_ancestor(VK_NULL_HANDLE)
   , m_device(VK_NULL_HANDLE)
   , m_queue(VK_NULL_HANDLE)
   , m_image_create_info()
   , m_image_acquire_lock()
   , m_error_state(VK_NOT_READY)
   , m_started_presenting(false)
   , m_extensions(m_allocator)
   , m_image_creator(m_allocator)
{
}

static bool external_sync_supported(const layer::device_private_data &device_data)
{
   return device_data.disp.get_fn<PFN_vkImportFenceFdKHR>("vkImportFenceFdKHR").has_value() &&
          device_data.disp.get_fn<PFN_vkImportSemaphoreFdKHR>("vkImportSemaphoreFdKHR").has_value();
}

VkResult swapchain_base::init(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info)
{
   assert(device != VK_NULL_HANDLE);
   assert(swapchain_create_info != nullptr);
   assert(swapchain_create_info->surface != VK_NULL_HANDLE);

   m_device = device;
   m_surface = swapchain_create_info->surface;
   m_present_mode = swapchain_create_info->presentMode;

   /* Register required extensions by the swapchain */
   TRY_LOG_CALL(add_required_extensions(device, swapchain_create_info));

   auto *ext = get_swapchain_extension<wsi::wsi_ext_swapchain_maintenance1>();
   if (ext)
   {
      TRY_LOG_CALL(ext->handle_swapchain_present_modes_create_info(device, swapchain_create_info, m_surface));
      TRY_LOG_CALL(ext->handle_scaling_create_info(device, swapchain_create_info, m_surface));
   }

   /* Init image to invalid values. */
   if (!m_swapchain_images.try_resize(swapchain_create_info->minImageCount))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   const bool want_mutable_format = (swapchain_create_info->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) != 0;

   /* Enable mutable-format early so backends can react in init_platform. */
   if (want_mutable_format)
   {
      enable_mutable_format();
   }

   /* We have allocated images, we can call the platform init function if something needs to be done. */
   bool use_presentation_thread = true;
   TRY_LOG_CALL(init_platform(device, swapchain_create_info, use_presentation_thread));

   if (use_presentation_thread)
   {
      TRY_LOG_CALL(init_page_flip_thread());
   }

   /* Initialize the image creator, and attach any required extensions */
   TRY_LOG_CALL(image_creator_init(*swapchain_create_info));
   m_image_create_info = m_image_creator.get_image_create_info();

   if (VkResult result = m_free_image_semaphore.init(m_swapchain_images.size()); result != VK_SUCCESS)
   {
      assert(result == VK_ERROR_OUT_OF_HOST_MEMORY);
      return result;
   }

   const bool image_deferred_allocation =
      swapchain_create_info->flags & VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_EXT;
   for (auto &img : m_swapchain_images)
   {
      TRY(create_swapchain_image(m_image_create_info, img));

      if (image_deferred_allocation)
      {
         img.status = swapchain_image::UNALLOCATED;
      }
      else
      {
         TRY_LOG_CALL(allocate_and_bind_swapchain_image(m_image_create_info, img));
      }

      VkSemaphoreCreateInfo semaphore_info = {};
      semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

      TRY_LOG_CALL(m_device_data.disp.CreateSemaphore(m_device, &semaphore_info, get_allocation_callbacks(),
                                                      &img.present_semaphore));
      TRY_LOG_CALL(m_device_data.disp.CreateSemaphore(m_device, &semaphore_info, get_allocation_callbacks(),
                                                      &img.present_fence_wait));
   }

   if (!external_sync_supported(m_device_data))
   {
      /*
       * This could be problematic when a queue has been created
       * with any flag enabled. The reason is that according to the
       * Vulkan spec, vkGetDeviceQueue2 should be used to get queues
       * that were created with non zero flags parameters.
       */
      m_device_data.disp.GetDeviceQueue(m_device, 0, 0, &m_queue);
      TRY_LOG_CALL(m_device_data.SetDeviceLoaderData(m_device, m_queue));
   }

   int res = sem_init(&m_start_present_semaphore, 0, 0);
   /* Only programming error can cause this to fail. */
   assert(res == 0);
   if (res != 0)
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* Release the swapchain images of the old swapchain in order
    * to free up memory for new swapchain. This is necessary especially
    * on platform with limited display memory size.
    *
    * NB: This must be done last in initialization, when the rest of
    * the swapchain is valid.
    */
   if (swapchain_create_info->oldSwapchain != VK_NULL_HANDLE)
   {
      /* Set ancestor. */
      m_ancestor = swapchain_create_info->oldSwapchain;

      auto *ancestor = reinterpret_cast<swapchain_base *>(m_ancestor);
      ancestor->deprecate(reinterpret_cast<VkSwapchainKHR>(this));
   }

   set_error_state(VK_SUCCESS);
   return VK_SUCCESS;
}

void swapchain_base::teardown()
{
   /* This method will block until all resources associated with this swapchain
    * are released. Images in the ACQUIRED or FREE state can be freed
    * immediately. For images in the PENDING state, we will block until the
    * presentation engine is finished with them. */

   if (has_descendant_started_presenting())
   {
      /* Here we wait for the start_present_semaphore, once this semaphore is up,
       * the descendant has finished waiting, we don't want to delete vkImages and vkFences
       * and semaphores before the waiting is done. */
      auto *desc = reinterpret_cast<swapchain_base *>(m_descendant);
      sem_wait(&desc->m_start_present_semaphore);
   }
   else if (!error_has_occured())
   {
      /* If descendant hasn't started presenting, there are pending buffers in the swapchain. */
      wait_for_pending_buffers();
   }

   if (m_queue != VK_NULL_HANDLE)
   {
      /* Make sure the vkFences are done signaling. */
      m_device_data.disp.QueueWaitIdle(m_queue);
   }

   /* We are safe to destroy everything. */
   if (m_thread_sem_defined)
   {
      /* Tell flip thread to end. */
      m_page_flip_thread_run = false;

      if (m_page_flip_thread.joinable())
      {
         m_page_flip_thread.join();
      }
      else
      {
         WSI_LOG_ERROR("m_page_flip_thread is not joinable");
      }
   }

   int res = sem_destroy(&m_start_present_semaphore);
   if (res != 0)
   {
      WSI_LOG_ERROR("sem_destroy failed for start_present_semaphore with %d", errno);
   }

   if (m_descendant != VK_NULL_HANDLE)
   {
      auto *sc = reinterpret_cast<swapchain_base *>(m_descendant);
      sc->clear_ancestor();
   }

   if (m_ancestor != VK_NULL_HANDLE)
   {
      auto *sc = reinterpret_cast<swapchain_base *>(m_ancestor);
      sc->clear_descendant();
   }
   /* Release the images array. */
   for (auto &img : m_swapchain_images)
   {
      /* Call implementation specific release */
      destroy_image(img);

      m_device_data.disp.DestroySemaphore(m_device, img.present_semaphore, get_allocation_callbacks());
      m_device_data.disp.DestroySemaphore(m_device, img.present_fence_wait, get_allocation_callbacks());
   }
}

VkResult swapchain_base::acquire_next_image(uint64_t timeout, VkSemaphore semaphore, VkFence fence,
                                            uint32_t *image_index)
{
   util::unique_lock<util::mutex> acquire_lock(m_image_acquire_lock);
   if (!acquire_lock)
   {
      WSI_LOG_ERROR("Failed to acquire image acquire lock.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY(wait_and_get_free_buffer(timeout));
   if (error_has_occured())
   {
      return get_error_state();
   }

   util::unique_lock<util::recursive_mutex> image_status_lock(m_image_status_mutex);
   if (!image_status_lock)
   {
      WSI_LOG_ERROR("Failed to acquire image status lock in acquire_next_image.");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   size_t i;
   for (i = 0; i < m_swapchain_images.size(); ++i)
   {
      if (m_swapchain_images[i].status == swapchain_image::UNALLOCATED)
      {
         auto res = allocate_and_bind_swapchain_image(m_image_create_info, m_swapchain_images[i]);
         if (res != VK_SUCCESS)
         {
            WSI_LOG_ERROR("Failed to allocate swapchain image.");
            return res != VK_ERROR_INITIALIZATION_FAILED ? res : VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }

      if (m_swapchain_images[i].status == swapchain_image::FREE)
      {
         m_swapchain_images[i].status = swapchain_image::ACQUIRED;
         *image_index = i;
         break;
      }
   }

   assert(i < m_swapchain_images.size());

   image_status_lock.unlock();

   if (!external_sync_supported(m_device_data))
   {
      /* Fallback for when importing fence/semaphore sync FDs is unsupported by the ICD. */
      queue_submit_semaphores semaphores = {
         nullptr,
         0,
         (semaphore != VK_NULL_HANDLE) ? &semaphore : nullptr,
         (semaphore != VK_NULL_HANDLE) ? 1u : 0,
      };
      return sync_queue_submit(m_device_data, m_queue, fence, semaphores);
   }

   /* Try to signal fences/semaphores with a sync FD for optimal performance. */
   if (fence != VK_NULL_HANDLE)
   {
      int already_signalled_sentinel_fd = -1;
      auto info = VkImportFenceFdInfoKHR{};
      {
         info.sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR;
         info.fence = fence;
         info.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
         info.fd = already_signalled_sentinel_fd;
         info.flags = VK_FENCE_IMPORT_TEMPORARY_BIT;
      }

      TRY_LOG_CALL(m_device_data.disp.ImportFenceFdKHR(m_device, &info));
      fence = VK_NULL_HANDLE;
   }

   if (semaphore != VK_NULL_HANDLE)
   {
      int already_signalled_sentinel_fd = -1;
      auto info = VkImportSemaphoreFdInfoKHR{};
      {
         info.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
         info.semaphore = semaphore;
         info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
         info.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
         info.fd = already_signalled_sentinel_fd;
      }

      TRY_LOG_CALL(m_device_data.disp.ImportSemaphoreFdKHR(m_device, &info));
      semaphore = VK_NULL_HANDLE;
   }

   return VK_SUCCESS;
}

VkResult swapchain_base::get_swapchain_images(uint32_t *swapchain_image_count, VkImage *swapchain_images)
{
   if (swapchain_images == nullptr)
   {
      /* Return the number of swapchain images. */
      *swapchain_image_count = m_swapchain_images.size();

      return VK_SUCCESS;
   }
   else
   {
      assert(m_swapchain_images.size() > 0);
      assert(*swapchain_image_count > 0);

      /* Populate array, write actual number of images returned. */
      uint32_t current_image = 0;

      do
      {
         swapchain_images[current_image] = m_swapchain_images[current_image].image;

         current_image++;

         if (current_image == m_swapchain_images.size())
         {
            *swapchain_image_count = current_image;

            return VK_SUCCESS;
         }

      } while (current_image < *swapchain_image_count);

      /* If swapchain_image_count is smaller than the number of presentable images
       * in the swapchain, VK_INCOMPLETE must be returned instead of VK_SUCCESS. */
      *swapchain_image_count = current_image;

      return VK_INCOMPLETE;
   }
}

VkResult swapchain_base::create_aliased_image_handle(VkImage *image)
{
   /* Build an alias VkImageCreateInfo compatible with swapchain memory binding. */
   VkImageCreateInfo alias_info = m_image_create_info;
   alias_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

   return m_device_data.disp.CreateImage(m_device, &alias_info, get_allocation_callbacks(), image);
}

VkResult swapchain_base::get_swapchain_status()
{
   return get_error_state();
}

VkResult swapchain_base::notify_presentation_engine(const pending_present_request &pending_present)
{
   const util::unique_lock<util::recursive_mutex> lock(m_image_status_mutex);
   if (!lock)
   {
      return VK_ERROR_UNKNOWN;
   }

   /* If the descendant has started presenting, we should release the image
    * however we do not want to block inside the main thread so we mark it
    * as free and let the page flip thread take care of it. */
   const bool descendant_started_presenting = has_descendant_started_presenting();
   if (descendant_started_presenting)
   {
      m_swapchain_images[pending_present.image_index].status = swapchain_image::FREE;
      m_free_image_semaphore.post();
      return VK_ERROR_OUT_OF_DATE_KHR;
   }

   m_swapchain_images[pending_present.image_index].status = swapchain_image::PENDING;
   m_started_presenting = true;

   if (m_page_flip_thread_run)
   {
      bool buffer_pool_res = m_pending_buffer_pool.push_back(pending_present);
      (void)buffer_pool_res;
      assert(buffer_pool_res);
      m_page_flip_semaphore.post();
   }
   else
   {
      call_present(pending_present);
   }

   return VK_SUCCESS;
}

VkResult swapchain_base::queue_present(VkQueue queue, const VkPresentInfoKHR *present_info,
                                       const swapchain_presentation_parameters &submit_info)
{
   if (submit_info.switch_presentation_mode)
   {
      /* Assert when a presentation mode switch is requested and the swapchain_maintenance1 extension which implements this is not available */
      auto *ext = get_swapchain_extension<wsi::wsi_ext_swapchain_maintenance1>(true);
      if (ext)
      {
         TRY_LOG_CALL(ext->handle_switching_presentation_mode(submit_info.present_mode, m_present_mode));
      }
   }

   const VkSemaphore *wait_semaphores = &m_swapchain_images[submit_info.pending_present.image_index].present_semaphore;
   uint32_t sem_count = 1;
   if (!submit_info.use_image_present_semaphore)
   {
      wait_semaphores = present_info->pWaitSemaphores;
      sem_count = present_info->waitSemaphoreCount;
   }

   if (!m_page_flip_thread_run)
   {
      /* If the page flip thread is not running, we need to wait for any present payload here, before setting a new present payload. */
      constexpr uint64_t WAIT_PRESENT_TIMEOUT = 1000000000; /* 1 second */
      TRY_LOG_CALL(
         image_wait_present(m_swapchain_images[submit_info.pending_present.image_index], WAIT_PRESENT_TIMEOUT));
   }

   void *submission_pnext = nullptr;
   uint32_t count_signal_semaphores = 0;
   std::optional<VkFrameBoundaryEXT> frame_boundary;
   /* Do not handle the event if it was handled before reaching this point */
   if (submit_info.handle_present_frame_boundary_event)
   {
      auto *ext = get_swapchain_extension<wsi::wsi_ext_frame_boundary>();
      frame_boundary = handle_frame_boundary_event(
         present_info, &m_swapchain_images[submit_info.pending_present.image_index].image, ext);

      if (frame_boundary)
      {
         submission_pnext = &frame_boundary.value();
      }
   }
   if (submit_info.present_fence != VK_NULL_HANDLE)
   {
      signal_semaphores[count_signal_semaphores++] =
         m_swapchain_images[submit_info.pending_present.image_index].present_fence_wait;
   }
#if VULKAN_WSI_LAYER_EXPERIMENTAL
   const VkPresentTimingInfoEXT *present_timing_info = nullptr;
   const auto *present_timings_info =
      util::find_extension<VkPresentTimingsInfoEXT>(VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT, present_info->pNext);
   if (present_timings_info != nullptr)
   {
      present_timing_info = present_timings_info->pTimingInfos;
      assert(present_timing_info != nullptr);
      if (present_timing_info->presentStageQueries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT)
      {
         auto *ext_present_timing = get_swapchain_extension<wsi::wsi_ext_present_timing>(true);
         signal_semaphores[count_signal_semaphores++] =
            ext_present_timing->get_image_present_semaphore(submit_info.pending_present.image_index);
      }
   }
#endif
   queue_submit_semaphores semaphores = {
      wait_semaphores,
      sem_count,
      count_signal_semaphores > 0 ? signal_semaphores.data() : nullptr,
      count_signal_semaphores,
   };
   TRY_LOG_CALL(image_set_present_payload(m_swapchain_images[submit_info.pending_present.image_index], queue,
                                          semaphores, submission_pnext));

   if (submit_info.present_fence != VK_NULL_HANDLE)
   {
      const queue_submit_semaphores wait_semaphores = {
         &m_swapchain_images[submit_info.pending_present.image_index].present_fence_wait, 1, nullptr, 0
      };
      /*
       * Here we chain wait_semaphores with present_fence through present_fence_wait.
       */
      TRY(sync_queue_submit(m_device_data, queue, submit_info.present_fence, wait_semaphores));
   }

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   if (present_timing_info != nullptr)
   {
      auto *ext_present_timing = get_swapchain_extension<wsi::wsi_ext_present_timing>(true);
      TRY_LOG_CALL(ext_present_timing->add_presentation_entry(
         queue, submit_info.pending_present.present_id, submit_info.pending_present.image_index, *present_timing_info));
   }
#endif
   TRY(notify_presentation_engine(submit_info.pending_present));

   return VK_SUCCESS;
}

void swapchain_base::deprecate(VkSwapchainKHR descendant)
{
   for (auto &img : m_swapchain_images)
   {
      if (img.status == swapchain_image::FREE)
      {
         destroy_image(img);
      }
   }

   /* Set its descendant. */
   m_descendant = descendant;
}

void swapchain_base::wait_for_pending_buffers()
{
   util::unique_lock<util::mutex> acquire_lock(m_image_acquire_lock);
   util::unique_lock<util::recursive_mutex> image_status_lock(m_image_status_mutex);
   if (!acquire_lock || !image_status_lock)
   {
      WSI_LOG_ERROR("Failed to acquire mutex lock in wait_for_pending_buffers.\n");
      abort();
   }

   uint64_t non_pending_images = 0;
   for (auto &img : m_swapchain_images)
   {
      /*
       * We don't want wait_and_get_free_buffer to be called when
       * there are free images, because it will consume the semaphore
       * and then subsequent calls might block if the semaphore value
       * has reached to zero.
       */
      if ((img.status == swapchain_image::ACQUIRED) || (img.status == swapchain_image::FREE) ||
          (img.status == swapchain_image::INVALID))
      {
         /* If the image is acquired or free, it is not pending. */
         non_pending_images++;
      }
   }

   /*
    * One pending image may be presented and acquired by a
    * compositor. The WSI backend may not necessarily know which
    * pending image is presented to change its state. It may
    * be impossible to wait for that one presented image.
    */
   int wait = static_cast<int>(m_swapchain_images.size() - non_pending_images - 1);
   image_status_lock.unlock();

   while (wait > 0)
   {
      /*
       * Take down one free image semaphore.
       *
       * In backends which unpresent an image only after a new one
       * has been submitted for presentation wait_and_get_free_buffer
       * could hang if the semaphore has a zero value  and the swapchain has been deprecated.
       */
      wait_and_get_free_buffer(UINT64_MAX);
      --wait;
   }
}

void swapchain_base::clear_ancestor()
{
   m_ancestor = VK_NULL_HANDLE;
}

void swapchain_base::clear_descendant()
{
   m_descendant = VK_NULL_HANDLE;
}

VkResult swapchain_base::wait_and_get_free_buffer(uint64_t timeout)
{
   /* first see if a buffer is already marked as free */
   VkResult retval = m_free_image_semaphore.wait(0);
   if (retval == VK_NOT_READY)
   {
      /* if not, we still have work to do even if timeout==0 -
       * the swapchain implementation may be able to get a buffer without
       * waiting */

      retval = get_free_buffer(&timeout);
      if (retval == VK_SUCCESS)
      {
         /* the sub-implementation has done it's thing, so re-check the
          * semaphore */
         retval = m_free_image_semaphore.wait(timeout);
      }
   }

   return retval;
}

void swapchain_base::release_images(uint32_t image_count, const uint32_t *indices)
{
   for (uint32_t i = 0; i < image_count; i++)
   {
      uint32_t index = indices[i];
      assert(index < m_swapchain_images.size());
      /* Applications can only pass acquired images that the device doesn't own */
      assert(m_swapchain_images[index].status == swapchain_image::ACQUIRED);
      unpresent_image(index);
   }
}

VkResult swapchain_base::is_bind_allowed(uint32_t image_index) const
{
   return m_swapchain_images[image_index].status != swapchain_image::UNALLOCATED ? VK_SUCCESS :
                                                                                   VK_ERROR_OUT_OF_HOST_MEMORY;
}

bool swapchain_base::add_swapchain_extension(util::unique_ptr<wsi_ext> extension)
{
   return m_extensions.add_extension(std::move(extension));
}

VkResult swapchain_base::image_creator_init(const VkSwapchainCreateInfoKHR &swapchain_create_info)
{
   m_image_creator.init(swapchain_create_info);

   util::vector<util::unique_ptr<swapchain_image_create_info_extension>> extensions(
      util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT));

   TRY_LOG_CALL(get_required_image_creator_extensions(swapchain_create_info, &extensions));

   if (is_mutable_format_enabled())
   {
      const auto *image_format_list = util::find_extension<VkImageFormatListCreateInfo>(
         VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, swapchain_create_info.pNext);
      auto mutable_format_uptr = swapchain_image_create_mutable_format::create_unique(image_format_list, m_allocator);
      if (!mutable_format_uptr)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      if (!extensions.try_push_back(std::move(mutable_format_uptr)))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   TRY_LOG_CALL(m_image_creator.add_extensions(extensions));

   return VK_SUCCESS;
}

} /* namespace wsi */
