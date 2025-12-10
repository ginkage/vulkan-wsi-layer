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
#include <util/helpers.hpp>

#include "present_timing.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL
namespace wsi
{
/* VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT,
 * VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT,
 * VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT,
 * VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
 */
static constexpr size_t MAX_PRESENT_STAGES = 4;
const std::array<VkPresentStageFlagBitsEXT, MAX_PRESENT_STAGES> g_present_stages = {
   VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT, VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT,
   VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT, VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
};

wsi_ext_present_timing::wsi_ext_present_timing(const util::allocator &allocator, VkDevice device, uint32_t num_images)
   : m_allocator(allocator)
   , m_time_domains(allocator)
   , m_device(layer::device_private_data::get(device))
   , m_queue(allocator)
   , m_device_timestamp_cached(allocator)
   , m_queue_mutex()
   , m_scheduled_present_targets(allocator)
   , m_num_images(num_images)
   , m_present_semaphore(allocator)
   , m_timestamp_period(0.f)
   , m_queue_family_resources(allocator, m_device)
{
   assert(m_device.is_device_extension_enabled(VK_KHR_PRESENT_ID_2_EXTENSION_NAME));
   VkPhysicalDeviceProperties2KHR physical_device_properties{};
   physical_device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
   auto &inst = layer::instance_private_data::get(m_device.physical_device);
   inst.disp.GetPhysicalDeviceProperties2KHR(m_device.physical_device, &physical_device_properties);
   m_timestamp_period = physical_device_properties.properties.limits.timestampPeriod;
}

wsi_ext_present_timing::~wsi_ext_present_timing()
{
   for (const auto &semaphore : m_present_semaphore)
   {
      if (semaphore != VK_NULL_HANDLE)
      {
         m_device.disp.DestroySemaphore(m_device.device, semaphore, m_allocator.get_original_callbacks());
      }
   }
}

VkResult wsi_ext_present_timing::init(util::unique_ptr<wsi::vulkan_time_domain> *domains, size_t domain_count)
{
   for (size_t i = 0; i < domain_count; i++)
   {
      if (!get_swapchain_time_domains().add_time_domain(std::move(domains[i])))
      {
         WSI_LOG_ERROR("Failed to add a time domain.");
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   if (is_present_stage_supported(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT))
   {
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
      TRY_LOG_CALL(m_queue_family_resources.init(m_device.get_best_queue_family_index(), m_num_images));
   }

   if (!m_scheduled_present_targets.try_resize(m_num_images))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
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

swapchain_presentation_timing *wsi_ext_present_timing::get_pending_stage_timing(uint32_t image_index,
                                                                                VkPresentStageFlagBitsEXT stage)
{
   if (auto *entry = get_pending_stage_entry(image_index, stage))
   {
      return &entry->get_stage_timing(stage)->get();
   }
   return nullptr;
}

swapchain_presentation_entry *wsi_ext_present_timing::get_pending_stage_entry(uint32_t image_index,
                                                                              VkPresentStageFlagBitsEXT stage)
{
   for (auto &entry : m_queue)
   {
      if (entry.m_image_index == image_index && entry.is_pending(stage))
      {
         return &entry;
      }
   }
   return nullptr;
}

bool wsi_ext_present_timing::is_present_stage_supported(VkPresentStageFlagBitsEXT present_stage)
{
   return stages_supported() & present_stage;
}

VkResult wsi_ext_present_timing::write_pending_results()
{
   for (auto &slot : m_queue)
   {
      if (slot.is_pending(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT))
      {
         /* Resize cached device timestamp records to the number of images. */
         if (!m_device_timestamp_cached.try_resize(m_num_images, 0ULL))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }

         uint64_t timestamp;
         VkResult res = m_device.disp.GetQueryPoolResults(m_device.device, m_queue_family_resources.m_query_pool,
                                                          slot.m_image_index, 1, sizeof(timestamp), &timestamp, 0,
                                                          VK_QUERY_RESULT_64_BIT);
         if (res != VK_SUCCESS && res != VK_NOT_READY)
         {
            return res;
         }
         if (res == VK_SUCCESS && m_device_timestamp_cached[slot.m_image_index] != timestamp)
         {
            m_device_timestamp_cached[slot.m_image_index] = timestamp;
            slot.set_stage_timing(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT,
                                  ticks_to_ns(timestamp, m_timestamp_period));
         }
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
   const util::unique_lock<util::mutex> lock(m_queue_mutex);
   if (!lock)
   {
      return VK_ERROR_UNKNOWN;
   }
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
   assert(image_index < m_queue_family_resources.m_command_buffer.size());
   command_buffer_data command_buffer_data(&m_queue_family_resources.m_command_buffer[image_index], 1);
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
                                                              uint64_t target_time,
                                                              VkPresentStageFlagsEXT present_stage_queries)
{
   const util::unique_lock<util::mutex> lock(m_queue_mutex);
   if (!lock)
   {
      return VK_ERROR_UNKNOWN;
   }
   TRY_LOG_CALL(write_pending_results());

   /* Keep the internal queue to the limit defined by the application. */
   if (m_queue.size() == m_queue.capacity())
   {
      return VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT;
   }
   wsi::swapchain_presentation_entry presentation_entry(target_time, present_stage_queries, present_id, image_index,
                                                        m_device.get_best_queue_family_index(), stages_supported());
   if (!m_queue.try_push_back(std::move(presentation_entry)))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   if ((present_stage_queries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) &&
       is_present_stage_supported(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT))
   {
      TRY_LOG_CALL(queue_submit_queue_end_timing(m_device, queue, image_index));
   }

   return VK_SUCCESS;
}

void wsi_ext_present_timing::add_presentation_target_entry(uint32_t image_index,
                                                           const VkPresentTimingInfoEXT &timing_info)
{
   assert(timing_info.targetTime != 0);
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
      TRY_LOG_CALL(add_presentation_query_entry(queue, present_id, image_index, timing_info.targetTime,
                                                timing_info.presentStageQueries));
   }
   if (timing_info.targetTime != 0)
   {
      add_presentation_target_entry(image_index, timing_info);
   }

   return VK_SUCCESS;
}

VkResult wsi_ext_present_timing::queue_has_space()
{
   const util::unique_lock<util::mutex> lock(m_queue_mutex);
   if (!lock)
   {
      WSI_LOG_WARNING("Failed to acquire queue mutex while checking for space in the queue");
      return VK_ERROR_UNKNOWN;
   }
   return (m_queue.size() == m_queue.capacity()) ? VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT : VK_SUCCESS;
}

swapchain_time_domains &wsi_ext_present_timing::get_swapchain_time_domains()
{
   return m_time_domains;
}

VkSemaphore wsi_ext_present_timing::get_image_present_semaphore(uint32_t image_index)
{
   assert(is_present_stage_supported(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT));
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
   const util::unique_lock<util::mutex> lock(m_queue_mutex);
   if (!lock)
   {
      return VK_ERROR_UNKNOWN;
   }
   assert(past_present_timing_properties != nullptr);
   /* Get any outstanding timings to the internal queue. */
   TRY_LOG_CALL(write_pending_results());
   if (past_present_timing_properties->pPresentationTimings == nullptr)
   {
      past_present_timing_properties->presentationTimingCount = get_num_available_results(flags);
      return VK_SUCCESS;
   }

   uint64_t timing_count = 0, removed_entries = 0;
   VkPastPresentationTimingEXT *timings = past_present_timing_properties->pPresentationTimings;
   const bool allow_partial = (flags & VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT) != 0;
   const bool allow_out_of_order = (flags & VK_PAST_PRESENTATION_TIMING_ALLOW_OUT_OF_ORDER_RESULTS_BIT_EXT) != 0;
   bool incomplete = false;

   for (uint64_t i = 0; (i - removed_entries) < m_queue.size(); ++i)
   {
      auto slot = m_queue.begin() + (i - removed_entries);

      if (!slot->has_completed_stages(allow_partial))
      {
         if (allow_out_of_order)
         {
            continue;
         }
         else
         {
            break;
         }
      }

      if (timing_count < past_present_timing_properties->presentationTimingCount)
      {
         slot->populate(timings[timing_count]);
         if (timings[timing_count++].reportComplete)
         {
            m_queue.erase(slot);
            removed_entries++;
         }
      }
      else
      {
         /* There are available results but were not returned. */
         incomplete = true;
      }
   }
   past_present_timing_properties->presentationTimingCount = timing_count;
   return incomplete ? VK_INCOMPLETE : VK_SUCCESS;
}

bool wsi_ext_present_timing::is_stage_pending_for_image_index(uint32_t image_index,
                                                              VkPresentStageFlagBitsEXT present_stage)
{
   const util::unique_lock<util::mutex> lock(m_queue_mutex);
   if (!lock)
   {
      WSI_LOG_ERROR("Failed to acquire queue mutex in is_stage_pending_for_image_index.");
      abort();
   }
   return (get_pending_stage_timing(image_index, present_stage) != nullptr);
}

VkResult wsi_ext_present_timing::physical_device_has_supported_queue_family(VkPhysicalDevice physical_device, bool &out)
{
   auto &instance = layer::instance_private_data::get(physical_device);
   const auto all_props = instance.get_queue_family_properties(physical_device);
   if (all_props.empty())
   {
      out = false;
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   out = std::any_of(all_props.begin(), all_props.end(), [](const VkQueueFamilyProperties2 &props) {
      return (props.queueFamilyProperties.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) &&
             (props.queueFamilyProperties.timestampValidBits > 0);
   });
   return VK_SUCCESS;
}

swapchain_presentation_entry::swapchain_presentation_entry(uint64_t target_time,
                                                           VkPresentStageFlagsEXT present_stage_queries,
                                                           uint64_t present_id, uint32_t image_index,
                                                           uint32_t queue_family,
                                                           VkPresentStageFlagsEXT stages_supported)
   : m_target_time(target_time)
   , m_target_stages(0)
   , m_present_id(present_id)
   , m_image_index(image_index)
   , m_num_present_stages(0)
   , m_queue_family(queue_family)
{
   if (present_stage_queries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT)
   {
      m_queue_end_timing =
         swapchain_presentation_timing(stages_supported & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT);
      m_num_present_stages++;
   }
   if (present_stage_queries & VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT)
   {
      m_latch_timing = swapchain_presentation_timing(stages_supported & VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT);
      m_num_present_stages++;
   }
   if (present_stage_queries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT)
   {
      m_first_pixel_out_timing =
         swapchain_presentation_timing(stages_supported & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT);
      m_num_present_stages++;
   }
   if (present_stage_queries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT)
   {
      m_first_pixel_visible_timing =
         swapchain_presentation_timing(stages_supported & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT);
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
           is_pending(VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT) ||
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
   case VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT:
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
      timing.targetTime = m_target_time;
   }
   /* Set how many stages have definitive results */
   timing.presentStageCount = stage_index;
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

bool swapchain_time_domains::add_time_domain(util::unique_ptr<swapchain_time_domain> time_domain)
{
   if (time_domain)
   {
      return m_time_domains.try_push_back(std::move(time_domain));
   }
   return false;
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
   if (pSwapchainTimeDomainProperties->pTimeDomains != nullptr)
   {
      pSwapchainTimeDomainProperties->pTimeDomains[0] = VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT;
   }
   if (pSwapchainTimeDomainProperties->pTimeDomainIds != nullptr)
   {
      pSwapchainTimeDomainProperties->pTimeDomainIds[0] = 0;
   }
   pSwapchainTimeDomainProperties->timeDomainCount = domains_count_to_write;

   return (domains_count_to_write < available_domains_count) ? VK_INCOMPLETE : VK_SUCCESS;
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

std::variant<bool, VkResult> present_timing_dependencies_supported(VkPhysicalDevice physical_device)
{
   auto &instance_data = layer::instance_private_data::get(physical_device);
   util::vector<VkExtensionProperties> properties{ util::allocator(instance_data.get_allocator(),
                                                                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND) };
   uint32_t count = 0;
   TRY_LOG(instance_data.disp.EnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr),
           "Failed to enumurate properties of available physical device extensions");

   if (!properties.try_resize(count))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   TRY_LOG(instance_data.disp.EnumerateDeviceExtensionProperties(physical_device, nullptr, &count, properties.data()),
           "Failed to enumurate properties of available physical device extensions");

   const bool maintenance9_supported =
      std::find_if(properties.begin(), properties.end(), [](const VkExtensionProperties &ext) {
         return strcmp(ext.extensionName, VK_KHR_MAINTENANCE_9_EXTENSION_NAME) == 0;
      }) != properties.end();

   if (!maintenance9_supported)
   {
      return false;
   }

   VkPhysicalDeviceMaintenance9FeaturesKHR maintenance9 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR, nullptr, VK_FALSE
   };
   VkPhysicalDeviceFeatures2KHR features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR, &maintenance9, {} };

   instance_data.disp.GetPhysicalDeviceFeatures2KHR(physical_device, &features);

   return maintenance9.maintenance9 != VK_FALSE;
}
} /* namespace wsi */

#endif /* VULKAN_WSI_LAYER_EXPERIMENTAL */
