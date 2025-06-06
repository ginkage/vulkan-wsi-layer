/*
 * Copyright (c) 2024-2025 Arm Limited.
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
 * @file present_timing.cpp
 *
 * @brief Contains the implentation for the VK_EXT_present_timing extension.
 */
#include <array>
#include <cassert>
#include <wsi/swapchain_base.hpp>

#include "present_timing.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL
namespace wsi
{
/* VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT,
 * VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT,
 * VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT,
 * VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
 */
static constexpr size_t MAX_PRESENT_STAGES = 4;
const std::array<VkPresentStageFlagBitsEXT, MAX_PRESENT_STAGES> g_present_stages = {
   VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT, VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT,
   VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT, VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
};

wsi_ext_present_timing::wsi_ext_present_timing(const util::allocator &allocator, VkDevice device, uint32_t num_images)
   : m_allocator(allocator)
   , m_time_domains(allocator)
   , m_device(device)
   , m_query_pool(VK_NULL_HANDLE)
   , m_command_pool(VK_NULL_HANDLE)
   , m_command_buffer(allocator)
   , m_queue(allocator)
   , m_num_images(num_images)
   , m_present_semaphore(allocator)
{
}

wsi_ext_present_timing::~wsi_ext_present_timing()
{
   const layer::device_private_data &device_data = layer::device_private_data::get(m_device);
   device_data.disp.FreeCommandBuffers(m_device, m_command_pool, m_command_buffer.size(), m_command_buffer.data());
   for (auto &command_buffer : m_command_buffer)
   {
      command_buffer = VK_NULL_HANDLE;
   }
   if (m_command_pool != VK_NULL_HANDLE)
   {
      device_data.disp.DestroyCommandPool(m_device, m_command_pool, m_allocator.get_original_callbacks());
      m_command_pool = VK_NULL_HANDLE;
   }
   if (m_query_pool != VK_NULL_HANDLE)
   {
      device_data.disp.DestroyQueryPool(m_device, m_query_pool, m_allocator.get_original_callbacks());
      m_query_pool = VK_NULL_HANDLE;
   }

   for (const auto &semaphore : m_present_semaphore)
   {
      if (semaphore != VK_NULL_HANDLE)
      {
         device_data.disp.DestroySemaphore(m_device, semaphore, m_allocator.get_original_callbacks());
      }
   }
}

VkResult wsi_ext_present_timing::init_timing_resources()
{
   const layer::device_private_data &device_data = layer::device_private_data::get(m_device);
   if (!m_present_semaphore.try_resize(m_num_images))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (auto &semaphore : m_present_semaphore)
   {
      semaphore = VK_NULL_HANDLE;
      VkSemaphoreCreateInfo semaphore_info = {};
      semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      if (device_data.disp.CreateSemaphore(m_device, &semaphore_info, m_allocator.get_original_callbacks(),
                                           &semaphore) != VK_SUCCESS)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
   /* Resize the command buffer to the number of images. */
   if (!m_command_buffer.try_resize(m_num_images))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (auto &command_buffer : m_command_buffer)
   {
      command_buffer = VK_NULL_HANDLE;
   }
   /* Allocate the command pool and query pool. */
   VkQueryPoolCreateInfo query_pool_info = {
      VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_TIMESTAMP, m_num_images, 0
   };
   TRY_LOG_CALL(device_data.disp.CreateQueryPool(m_device, &query_pool_info, m_allocator.get_original_callbacks(),
                                                 &m_query_pool));
   VkCommandPoolCreateInfo command_pool_info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0 };
   TRY_LOG_CALL(device_data.disp.CreateCommandPool(m_device, &command_pool_info, m_allocator.get_original_callbacks(),
                                                   &m_command_pool));
   /* Allocate and write the command buffer. */
   VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
                                                       m_command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_num_images };
   TRY_LOG_CALL(device_data.disp.AllocateCommandBuffers(m_device, &command_buffer_info, m_command_buffer.data()));
   VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr };
   for (size_t image_index = 0; image_index < m_num_images; image_index++)
   {
      TRY_LOG_CALL(device_data.disp.BeginCommandBuffer(m_command_buffer[image_index], &begin_info));
      device_data.disp.CmdResetQueryPool(m_command_buffer[image_index], m_query_pool, image_index, 1);
      device_data.disp.CmdWriteTimestamp(m_command_buffer[image_index], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                         m_query_pool, image_index);
      TRY_LOG_CALL(device_data.disp.EndCommandBuffer(m_command_buffer[image_index]));
   }
   return VK_SUCCESS;
}

VkResult wsi_ext_present_timing::get_queue_end_timing_to_queue(uint32_t image_index)
{
   for (auto &slot : m_queue)
   {
      if ((slot.m_image_index == image_index) && slot.is_pending(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT))
      {
         uint64_t time;
         auto stage_timing_optional = slot.get_stage_timing(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT);
         const layer::device_private_data &device_data = layer::device_private_data::get(m_device);
         TRY(device_data.disp.GetQueryPoolResults(m_device, m_query_pool, image_index, 1, sizeof(time), &time, 0,
                                                  VK_QUERY_RESULT_64_BIT));
         stage_timing_optional->get().m_time.store(time);
         /* For an image index, there can only be one entry in the internal queue with pending results. */
         break;
      }
   }
   return VK_SUCCESS;
}

VkResult wsi_ext_present_timing::query_present_queue_end_timings()
{
   for (uint32_t image_index = 0; image_index < m_num_images; ++image_index)
   {
      VkResult result = get_queue_end_timing_to_queue(image_index);
      if ((result != VK_SUCCESS) && (result != VK_NOT_READY))
      {
         return result;
      }
   }
   return VK_SUCCESS;
}

VkResult wsi_ext_present_timing::present_timing_queue_set_size(size_t queue_size)
{
   if (present_timing_get_num_outstanding_results() > queue_size)
   {
      return VK_NOT_READY;
   }
   /* A  vector is reserved with the updated size and the outstanding entries
    * are copied over. A vector resize is not used since the outstanding entries
    * are not sequential.
    */
   util::vector<swapchain_presentation_entry> presentation_timing(
      util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT));
   if (!presentation_timing.try_reserve(queue_size))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (auto &slot : m_queue)
   {
      if (slot.has_outstanding_stages())
      {
         /* The memory is already reserved for the new vector
          * and there are no possibilities for an exception
          * at this point. An exception at this point will
          * cause bad state as the vector has partially copied.
          */
         bool res = presentation_timing.try_push_back(std::move(slot));
         assert(res);
         UNUSED(res);
      }
   }
   m_queue.swap(presentation_timing);
   return VK_SUCCESS;
}

size_t wsi_ext_present_timing::present_timing_get_num_outstanding_results()
{
   size_t num_outstanding = 0;

   for (auto &slot : m_queue)
   {
      if (slot.has_outstanding_stages())
      {
         num_outstanding++;
      }
   }
   return num_outstanding;
}

VkResult wsi_ext_present_timing::queue_submit_queue_end_timing(const layer::device_private_data &device, VkQueue queue,
                                                               uint32_t image_index)
{
   assert(image_index < m_command_buffer.size());
   command_buffer_data command_buffer_data(&m_command_buffer[image_index], 1);
   VkSemaphore present_timing_semaphore = get_image_present_semaphore(image_index);
   queue_submit_semaphores present_timing_semaphores = {
      &present_timing_semaphore,
      1,
      nullptr,
      0,
   };
   TRY_LOG_CALL(sync_queue_submit(device, queue, VK_NULL_HANDLE, present_timing_semaphores, command_buffer_data));
   return VK_SUCCESS;
}

VkResult wsi_ext_present_timing::add_presentation_entry(const layer::device_private_data &device, VkQueue queue,
                                                        uint64_t present_id, uint32_t image_index,
                                                        VkPresentStageFlagsEXT present_stage_queries)
{
   if (present_stage_queries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT)
   {
      /* Get results for the previous presentation. The queue end stage of
       * the previous presentation for the same image must had
       * finished when the same image is going to be presented again. */
      TRY_LOG_CALL(get_queue_end_timing_to_queue(image_index));
   }
   /* Keep the internal queue to the limit defined by the application. */
   if (m_queue.size() == m_queue.capacity())
   {
      return VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT;
   }
   wsi::swapchain_presentation_entry presentation_entry(present_stage_queries, present_id, image_index);
   if (!m_queue.try_push_back(std::move(presentation_entry)))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   if (present_stage_queries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT)
   {
      TRY_LOG_CALL(queue_submit_queue_end_timing(device, queue, image_index));
   }
   return VK_SUCCESS;
}

swapchain_time_domains &wsi_ext_present_timing::get_swapchain_time_domains()
{
   return m_time_domains;
}

VkSemaphore wsi_ext_present_timing::get_image_present_semaphore(uint32_t image_index)
{
   return m_present_semaphore[image_index];
}

uint32_t wsi_ext_present_timing::get_num_available_results()
{
   uint32_t num_pending_results = 0;
   for (auto &slot : m_queue)
   {
      if (slot.has_completed_stages())
      {
         num_pending_results++;
      }
   }
   return num_pending_results;
}

VkResult wsi_ext_present_timing::get_past_presentation_results(
   VkPastPresentationTimingPropertiesEXT *past_present_timing_properties)
{
   assert(past_present_timing_properties != nullptr);
   /* Get any outstanding timings in the query pool to the internal queue. */
   TRY_LOG_CALL(query_present_queue_end_timings());
   if ((past_present_timing_properties->presentationTimingCount == 0) ||
       (past_present_timing_properties->pPresentationTimings == nullptr))
   {
      past_present_timing_properties->presentationTimingCount = get_num_available_results();
      return VK_SUCCESS;
   }
   /* When application request entries with multiple zero present ids or combination of zero and
    * non-zero present ids, this field helps avoiding the same slot getting copied to the results.
    */
   for (auto &slot : m_queue)
   {
      slot.copied = false;
   }
   /* When application request entries with presentIds in an order where there are presentId=0
    * requested earlier than presentId!=0, then the incoming pointer get filled with first available
    * slots when handling the zero presentIds. Later when non-zero presentIds are handled, if the
    * matching slot was already copied to the output, then no slot will be copied for that.
    * This creates a situation where a fewer results being responded for that particular request
    * compared to the amount that would have achieved with handling non-zeros first and zeros later. */
   uint32_t count_results = 0;
   for (uint32_t i = 0; i < past_present_timing_properties->presentationTimingCount; ++i)
   {
      bool timings_found = false;
      if (count_results == past_present_timing_properties->presentationTimingCount)
      {
         if (count_results < get_num_available_results())
         {
            return VK_INCOMPLETE;
         }
         return VK_SUCCESS;
      }
      VkPastPresentationTimingEXT &timing = past_present_timing_properties->pPresentationTimings[i];
      for (auto slot = m_queue.begin(); slot != m_queue.end();)
      {
         if (!slot->copied && slot->has_completed_stages())
         {
            /* There will be only one slot in the queue per presentId. */
            if ((timing.presentId == 0) || (timing.presentId == slot->m_present_id))
            {
               assert(timing.presentStageCount >= slot->m_num_present_stages);
               if (slot->populate(timing))
               {
                  count_results++;
                  slot->copied = true;
                  timings_found = true;
                  if (timing.reportComplete)
                  {
                     slot = m_queue.erase(slot);
                     continue;
                  }
               }
            }
         }
         slot++;
      }
      /* When the timings are not filled, reset the count to zero. */
      if (!timings_found)
      {
         timing.presentStageCount = 0;
      }
   }
   if ((count_results < past_present_timing_properties->presentationTimingCount) ||
       (count_results < get_num_available_results()))
   {
      past_present_timing_properties->presentationTimingCount = count_results;
      return VK_INCOMPLETE;
   }
   return VK_SUCCESS;
}

swapchain_presentation_entry::swapchain_presentation_entry(VkPresentStageFlagsEXT present_stage_queries,
                                                           uint64_t present_id, uint32_t image_index)
   : m_target_stages(0)
   , m_present_id(present_id)
   , m_image_index(image_index)
   , m_num_present_stages(0)
{
   if (present_stage_queries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT)
   {
      m_queue_end_timing = swapchain_presentation_timing();
      m_num_present_stages++;
   }
   if (present_stage_queries & VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT)
   {
      m_latch_timing = swapchain_presentation_timing();
      m_num_present_stages++;
   }
   if (present_stage_queries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT)
   {
      m_first_pixel_out_timing = swapchain_presentation_timing();
      m_num_present_stages++;
   }
   if (present_stage_queries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT)
   {
      m_first_pixel_visible_timing = swapchain_presentation_timing();
      m_num_present_stages++;
   }
}

bool swapchain_presentation_entry::is_pending(VkPresentStageFlagBitsEXT stage)
{
   auto stage_timing_optional = get_stage_timing(stage);
   if (stage_timing_optional.has_value() && (stage_timing_optional->get().m_time.load() == 0))
   {
      return true;
   }
   return false;
}

bool swapchain_presentation_entry::is_complete(VkPresentStageFlagBitsEXT stage)
{
   auto stage_timing_optional = get_stage_timing(stage);
   if (stage_timing_optional.has_value() && (stage_timing_optional->get().m_time.load() != 0))
   {
      return true;
   }
   return false;
}

bool swapchain_presentation_entry::has_outstanding_stages()
{
   /* Check if any of the requested stages is pending to be completed. */
   return (is_pending(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) ||
           is_pending(VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT) ||
           is_pending(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT) ||
           is_pending(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT));
}

bool swapchain_presentation_entry::has_completed_stages()
{
   /* Check if any of the requested stages is complete. */
   return (is_complete(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) ||
           is_complete(VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT) ||
           is_complete(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT) ||
           is_complete(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT));
}

std::optional<std::reference_wrapper<swapchain_presentation_timing>> swapchain_presentation_entry::get_stage_timing(
   VkPresentStageFlagBitsEXT stage)
{
   switch (stage)
   {
   case VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT:
      if (m_queue_end_timing.has_value())
      {
         return *m_queue_end_timing;
      }
      break;
   case VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT:
      if (m_latch_timing.has_value())
      {
         return *m_latch_timing;
      }
      break;
   case VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT:
      if (m_first_pixel_out_timing.has_value())
      {
         return *m_first_pixel_out_timing;
      }
      break;
   case VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT:
      if (m_first_pixel_visible_timing.has_value())
      {
         return *m_first_pixel_visible_timing;
      }
      break;
   default:
      assert(0);
   }
   return std::nullopt;
}

bool swapchain_presentation_entry::populate(VkPastPresentationTimingEXT &timing)
{
   uint64_t stage_index = 0;
   for (const auto &stage : g_present_stages)
   {
      auto stage_timing_optional = get_stage_timing(stage);
      if (!stage_timing_optional.has_value())
      {
         continue;
      }
      uint64_t time = stage_timing_optional->get().m_time.load();
      if (time > 0)
      {
         timing.timeDomainId = stage_timing_optional->get().m_timedomain_id;
         timing.pPresentStages[stage_index].stage = stage;
         timing.pPresentStages[stage_index++].time = time;
      }
   }
   timing.presentStageCount = stage_index;
   /* If atleast one entry is made to the timings, update the other fields. */
   if (stage_index != 0)
   {
      /* and all requested stages in the entry had been responded,
       * set the report complete to true. */
      timing.presentId = m_present_id;
      /* All the available stages are now populated. If there are no more outstanding stages,
       * then the report is complete and the slot can be freed. */
      timing.reportComplete = !has_outstanding_stages();
      return true;
   }
   return false;
}

VkResult swapchain_time_domains::calibrate(VkPresentStageFlagBitsEXT present_stage,
                                           swapchain_calibrated_time *calibrated_time)
{
   for (auto &domain : m_time_domains)
   {
      if ((domain->get_present_stages() & present_stage) != 0)
      {
         *calibrated_time = domain->calibrate();
         return VK_SUCCESS;
      }
   }

   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult swapchain_time_domains::get_swapchain_time_domain_properties(
   VkSwapchainTimeDomainPropertiesEXT *pSwapchainTimeDomainProperties, uint64_t *pTimeDomainsCounter)
{
   if (pTimeDomainsCounter != nullptr)
   {
      *pTimeDomainsCounter = 1;
   }

   if (pSwapchainTimeDomainProperties != nullptr)
   {
      if ((pSwapchainTimeDomainProperties->pTimeDomains == nullptr &&
           pSwapchainTimeDomainProperties->pTimeDomainIds == nullptr) ||
          pSwapchainTimeDomainProperties->timeDomainCount == 0)
      {
         pSwapchainTimeDomainProperties->timeDomainCount = 1;
      }
      else
      {
         /* Since we only have a single time domain available we don't need to check
          * timeDomainCount since it can only be >= 1 */
         pSwapchainTimeDomainProperties->timeDomainCount = 1;
         pSwapchainTimeDomainProperties->pTimeDomains[0] = VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT;
         pSwapchainTimeDomainProperties->pTimeDomainIds[0] = 0;
      }
   }

   return VK_SUCCESS;
}

bool swapchain_time_domains::add_time_domain(util::unique_ptr<swapchain_time_domain> time_domain)
{
   if (time_domain)
   {
      return m_time_domains.try_push_back(std::move(time_domain));
   }
   return false;
}

} /* namespace wsi */

#endif /* VULKAN_WSI_LAYER_EXPERIMENTAL */
