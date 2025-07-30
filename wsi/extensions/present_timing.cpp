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
#include <cmath>
#include <wsi/swapchain_base.hpp>
#include <util/helpers.hpp>

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
   , m_device(layer::device_private_data::get(device))
   , m_query_pool(VK_NULL_HANDLE)
   , m_command_pool(VK_NULL_HANDLE)
   , m_command_buffer(allocator)
   , m_queue_mutex()
   , m_queue(allocator)
   , m_scheduled_present_targets(allocator)
   , m_num_images(num_images)
   , m_present_semaphore(allocator)
   , m_timestamp_period(0.f)
{
   if (!m_device.is_present_id_enabled())
   {
      WSI_LOG_ERROR(VK_EXT_PRESENT_TIMING_EXTENSION_NAME
                    " enabled but required extension " VK_KHR_PRESENT_ID_EXTENSION_NAME " is not enabled.");
   }

   VkPhysicalDeviceProperties2KHR physical_device_properties{};
   physical_device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
   auto &inst = layer::instance_private_data::get(m_device.physical_device);
   inst.disp.GetPhysicalDeviceProperties2KHR(m_device.physical_device, &physical_device_properties);
   m_timestamp_period = physical_device_properties.properties.limits.timestampPeriod;
}

wsi_ext_present_timing::~wsi_ext_present_timing()
{
   m_device.disp.FreeCommandBuffers(m_device.device, m_command_pool, m_command_buffer.size(), m_command_buffer.data());
   for (auto &command_buffer : m_command_buffer)
   {
      command_buffer = VK_NULL_HANDLE;
   }
   if (m_command_pool != VK_NULL_HANDLE)
   {
      m_device.disp.DestroyCommandPool(m_device.device, m_command_pool, m_allocator.get_original_callbacks());
      m_command_pool = VK_NULL_HANDLE;
   }
   if (m_query_pool != VK_NULL_HANDLE)
   {
      m_device.disp.DestroyQueryPool(m_device.device, m_query_pool, m_allocator.get_original_callbacks());
      m_query_pool = VK_NULL_HANDLE;
   }

   for (const auto &semaphore : m_present_semaphore)
   {
      if (semaphore != VK_NULL_HANDLE)
      {
         m_device.disp.DestroySemaphore(m_device.device, semaphore, m_allocator.get_original_callbacks());
      }
   }
}

VkResult wsi_ext_present_timing::init_timing_resources()
{
   if (!m_scheduled_present_targets.try_resize(m_num_images))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (!m_present_semaphore.try_resize(m_num_images))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (auto &semaphore : m_present_semaphore)
   {
      semaphore = VK_NULL_HANDLE;
      VkSemaphoreCreateInfo semaphore_info = {};
      semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      if (m_device.disp.CreateSemaphore(m_device.device, &semaphore_info, m_allocator.get_original_callbacks(),
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
   TRY_LOG_CALL(m_device.disp.CreateQueryPool(m_device.device, &query_pool_info, m_allocator.get_original_callbacks(),
                                              &m_query_pool));
   VkCommandPoolCreateInfo command_pool_info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0 };
   TRY_LOG_CALL(m_device.disp.CreateCommandPool(m_device.device, &command_pool_info,
                                                m_allocator.get_original_callbacks(), &m_command_pool));
   /* Allocate and write the command buffer. */
   VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
                                                       m_command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_num_images };
   TRY_LOG_CALL(m_device.disp.AllocateCommandBuffers(m_device.device, &command_buffer_info, m_command_buffer.data()));
   VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr };
   for (size_t image_index = 0; image_index < m_num_images; image_index++)
   {
      TRY_LOG_CALL(m_device.disp.BeginCommandBuffer(m_command_buffer[image_index], &begin_info));
      m_device.disp.CmdResetQueryPool(m_command_buffer[image_index], m_query_pool, image_index, 1);
      m_device.disp.CmdWriteTimestamp(m_command_buffer[image_index], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_query_pool,
                                      image_index);
      TRY_LOG_CALL(m_device.disp.EndCommandBuffer(m_command_buffer[image_index]));
   }
   return VK_SUCCESS;
}

VkResult wsi_ext_present_timing::get_pixel_out_timing_to_queue(
   uint32_t image_index, std::optional<std::reference_wrapper<swapchain_presentation_timing>> stage_timing_optional)
{
   UNUSED(image_index);
   UNUSED(stage_timing_optional);
   return VK_SUCCESS;
}

static inline uint64_t ticks_to_ns(uint64_t ticks, const float &timestamp_period)
{
   /* timestamp_period is float (ns per tick).  Use double so we keep
      52-bit integer precision (≈4.5×10¹⁵ ticks) without overflow. */
   assert(std::isfinite(timestamp_period) && timestamp_period > 0.0f);
   double ns = static_cast<double>(ticks) * static_cast<double>(timestamp_period);
   return static_cast<uint64_t>(std::llround(ns));
}

swapchain_presentation_timing *wsi_ext_present_timing::get_pending_stage_timing(uint32_t image_index,
                                                                                VkPresentStageFlagBitsEXT stage)
{
   for (auto &entry : m_queue)
   {
      if (entry.m_image_index == image_index && entry.is_pending(stage))
      {
         return &entry.get_stage_timing(stage)->get();
      }
   }
   return nullptr;
}

VkResult wsi_ext_present_timing::write_pending_results()
{
   for (auto &slot : m_queue)
   {
      if (slot.is_pending(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT))
      {
         uint64_t time;
         TRY(m_device.disp.GetQueryPoolResults(m_device.device, m_query_pool, slot.m_image_index, 1, sizeof(time),
                                               &time, 0, VK_QUERY_RESULT_64_BIT));
         slot.set_stage_timing(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT, ticks_to_ns(time, m_timestamp_period));
      }
      if (slot.is_pending(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT))
      {
         TRY_LOG_CALL(get_pixel_out_timing_to_queue(
            slot.m_image_index, slot.get_stage_timing(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT)));
      }
   }
   return VK_SUCCESS;
}

VkResult wsi_ext_present_timing::present_timing_queue_set_size(size_t queue_size)
{
   const std::lock_guard<std::mutex> lock(m_queue_mutex);
   if (m_queue.size() > queue_size)
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

VkResult wsi_ext_present_timing::add_presentation_query_entry(VkQueue queue, uint64_t present_id, uint32_t image_index,
                                                              VkPresentStageFlagsEXT present_stage_queries)
{
   const std::lock_guard<std::mutex> lock(m_queue_mutex);
   TRY_LOG_CALL(write_pending_results());

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
      TRY_LOG_CALL(queue_submit_queue_end_timing(m_device, queue, image_index));
   }

   return VK_SUCCESS;
}

void wsi_ext_present_timing::add_presentation_target_entry(uint32_t image_index,
                                                           const VkPresentTimingInfoEXT &timing_info)
{
   assert(timing_info.targetPresentStage);
   m_scheduled_present_targets[image_index] = scheduled_present_target(timing_info);
}

void wsi_ext_present_timing::remove_presentation_target_entry(uint32_t image_index)
{
   m_scheduled_present_targets[image_index] = std::nullopt;
}

std::optional<scheduled_present_target> wsi_ext_present_timing::get_presentation_target_entry(uint32_t image_index)
{
   return m_scheduled_present_targets[image_index];
}

VkResult wsi_ext_present_timing::add_presentation_entry(VkQueue queue, uint64_t present_id, uint32_t image_index,
                                                        const VkPresentTimingInfoEXT &timing_info)
{
   if (timing_info.presentStageQueries)
   {
      TRY_LOG_CALL(add_presentation_query_entry(queue, present_id, image_index, timing_info.presentStageQueries));
   }
   if (timing_info.targetPresentStage)
   {
      add_presentation_target_entry(image_index, timing_info);
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

uint32_t wsi_ext_present_timing::get_num_available_results(VkPastPresentationTimingFlagsEXT flags)
{
   uint32_t num_pending_results = 0;
   const bool allow_partial = (flags & VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT) != 0;
   const bool allow_out_of_order = (flags & VK_PAST_PRESENTATION_TIMING_ALLOW_OUT_OF_ORDER_RESULTS_BIT_EXT) != 0;
   for (auto &slot : m_queue)
   {
      if (slot.has_completed_stages(allow_partial))
      {
         num_pending_results++;
      }
      else if (!allow_out_of_order)
      {
         break;
      }
   }
   return num_pending_results;
}

VkResult wsi_ext_present_timing::get_past_presentation_results(
   VkPastPresentationTimingPropertiesEXT *past_present_timing_properties, VkPastPresentationTimingFlagsEXT flags)
{
   const std::lock_guard<std::mutex> lock(m_queue_mutex);

   assert(past_present_timing_properties != nullptr);
   /* Get any outstanding timings to the internal queue. */
   TRY_LOG_CALL(write_pending_results());
   if (past_present_timing_properties->pPresentationTimings == nullptr)
   {
      past_present_timing_properties->presentationTimingCount = get_num_available_results(flags);
      return VK_SUCCESS;
   }

   VkPastPresentationTimingEXT *timings = past_present_timing_properties->pPresentationTimings;

   bool seen_zero = false;
   size_t last_zero_entry = 0;
   uint64_t in = 0;
   uint64_t out = 0;
   uint64_t removed_entries = 0;
   const bool allow_partial = (flags & VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT) != 0;
   const bool allow_out_of_order = (flags & VK_PAST_PRESENTATION_TIMING_ALLOW_OUT_OF_ORDER_RESULTS_BIT_EXT) != 0;
   /*
    * Single forward pass over the caller-supplied pPresentationTimings array:
    *
    * Locate the first matching presentation slot in `m_queue`.
    *
    * When a matching slot exists and at least one stage has available timings,
    * copy its timestamps into the current entry.  Valid results are compacted
    * in-place by writing to the `out` cursor while `in` continues to scan,
    * so gaps are skipped without repeated shifting.
    */
   while (in < past_present_timing_properties->presentationTimingCount)
   {
      const uint64_t present_id = timings[in].presentId;
      /*
       * If presentId != 0, match the exact ID.
       * If presentId == 0, pick the next unused zero-ID slot appearing
       * after `last_zero_entry`, ensuring we never report the same slot twice.
       */
      auto slot = std::find_if(m_queue.begin(), m_queue.end(), [&](const swapchain_presentation_entry &e) {
         bool zero_extra_cond =
            (present_id == 0 && seen_zero) ? (&e - m_queue.data()) > static_cast<ptrdiff_t>(last_zero_entry) : true;
         return (e.m_present_id == present_id) && zero_extra_cond;
      });

      if (slot != m_queue.end())
      {
         if (!slot->has_completed_stages(allow_partial))
         {
            if (allow_out_of_order)
            {
               in++;
               continue;
            }
            else
            {
               break;
            }
         }

         if (present_id == 0)
         {
            seen_zero = true;
            last_zero_entry = std::distance(m_queue.begin(), slot);
         }

         slot->populate(timings[in]);

         if (in != out)
         {
            timings[out] = timings[in];
         }

         ++out;

         if (timings[in].reportComplete)
         {
            m_queue.erase(slot);
            removed_entries++;
         }
      }

      ++in;
   }

   past_present_timing_properties->presentationTimingCount = out;

   const bool incomplete = (out < in) || (out < (get_num_available_results(flags) + removed_entries));

   return incomplete ? VK_INCOMPLETE : VK_SUCCESS;
}

bool wsi_ext_present_timing::is_stage_pending_for_image_index(uint32_t image_index,
                                                              VkPresentStageFlagBitsEXT present_stage)
{
   const std::lock_guard<std::mutex> lock(m_queue_mutex);
   return (get_pending_stage_timing(image_index, present_stage) != nullptr);
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

std::optional<bool> swapchain_presentation_entry::is_complete(VkPresentStageFlagBitsEXT stage)
{
   auto stage_timing_optional = get_stage_timing(stage);
   if (!stage_timing_optional.has_value())
   {
      return std::nullopt;
   }
   return stage_timing_optional->get().m_set;
}

bool swapchain_presentation_entry::is_pending(VkPresentStageFlagBitsEXT stage)
{
   auto stage_timing_optional = get_stage_timing(stage);
   return stage_timing_optional.has_value() ? !stage_timing_optional->get().m_set : false;
}

bool swapchain_presentation_entry::has_outstanding_stages()
{
   return (is_pending(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) ||
           is_pending(VK_PRESENT_STAGE_IMAGE_LATCHED_BIT_EXT) ||
           is_pending(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT) ||
           is_pending(VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT));
}

bool swapchain_presentation_entry::has_completed_stages(bool allow_partial)
{
   bool partial_result = false;
   bool non_partial_result = true;

   for (const auto &stage : g_present_stages)
   {
      auto complete = is_complete(stage);
      if (complete.has_value())
      {
         partial_result |= complete.value();
         non_partial_result &= complete.value();
      }
   }

   return allow_partial ? partial_result : non_partial_result;
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

void swapchain_presentation_entry::set_stage_timing(VkPresentStageFlagBitsEXT stage, uint64_t time)
{
   auto stage_timing_optional = get_stage_timing(stage);
   if (stage_timing_optional->get().m_set)
   {
      return;
   }
   stage_timing_optional->get().set_time(time);
}

void swapchain_presentation_entry::populate(VkPastPresentationTimingEXT &timing)
{
   uint32_t stage_index = 0;
   for (const auto &stage : g_present_stages)
   {
      auto stage_timing_optional = get_stage_timing(stage);
      if (!stage_timing_optional.has_value())
      {
         continue;
      }

      if (stage_timing_optional->get().m_set)
      {
         timing.timeDomainId = stage_timing_optional->get().m_timedomain_id;
         timing.pPresentStages[stage_index].stage = stage;
         timing.pPresentStages[stage_index++].time = stage_timing_optional->get().m_time;
      }
   }

   /* If at least one entry is made to the timings, update the other fields. */
   if (stage_index != 0)
   {
      timing.presentId = m_present_id;
      timing.reportComplete = !has_outstanding_stages();
   }
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
   /* Since we only have a single time domain available we don't need to check
    * timeDomainCount since it can only be >= 1 */
   constexpr uint32_t available_domains_count = 1;

   if (pTimeDomainsCounter != nullptr)
   {
      *pTimeDomainsCounter = 1;
   }

   if (pSwapchainTimeDomainProperties->pTimeDomains == nullptr &&
       pSwapchainTimeDomainProperties->pTimeDomainIds == nullptr)
   {
      pSwapchainTimeDomainProperties->timeDomainCount = available_domains_count;
      return VK_SUCCESS;
   }

   const uint32_t requested_domains_count = pSwapchainTimeDomainProperties->timeDomainCount;
   const uint32_t domains_count_to_write = std::min(requested_domains_count, available_domains_count);

   pSwapchainTimeDomainProperties->pTimeDomains[0] = VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT;
   pSwapchainTimeDomainProperties->pTimeDomainIds[0] = 0;
   pSwapchainTimeDomainProperties->timeDomainCount = domains_count_to_write;

   return (domains_count_to_write < available_domains_count) ? VK_INCOMPLETE : VK_SUCCESS;
}

bool swapchain_time_domains::add_time_domain(util::unique_ptr<swapchain_time_domain> time_domain)
{
   if (time_domain)
   {
      return m_time_domains.try_push_back(std::move(time_domain));
   }
   return false;
}

VkResult check_time_domain_support(VkPhysicalDevice physical_device, std::tuple<VkTimeDomainEXT, bool> *domains,
                                   size_t domain_size)
{
   auto &instance_data = layer::instance_private_data::get(physical_device);

   uint32_t supported_domains_count = 0;
   TRY(instance_data.disp.GetPhysicalDeviceCalibrateableTimeDomainsKHR(physical_device, &supported_domains_count,
                                                                       nullptr));

   util::allocator allocator(instance_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   util::vector<VkTimeDomainEXT> supported_domains(allocator);
   if (!supported_domains.try_resize(supported_domains_count))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY(instance_data.disp.GetPhysicalDeviceCalibrateableTimeDomainsKHR(physical_device, &supported_domains_count,
                                                                       supported_domains.data()));
   for (size_t i = 0; i < domain_size; i++)
   {
      std::get<1>(domains[i]) =
         std::find(supported_domains.begin(), supported_domains.begin() + supported_domains_count,
                   std::get<0>(domains[i])) != (supported_domains.begin() + supported_domains_count);
   }

   return VK_SUCCESS;
}

} /* namespace wsi */

#endif /* VULKAN_WSI_LAYER_EXPERIMENTAL */
