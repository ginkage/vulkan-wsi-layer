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
 * @file present_timing_handler.cpp
 *
 * @brief Contains the functionality to implement features for present timing extension.
 */

#include <util/custom_mutex.hpp>
#include "present_timing_handler.hpp"
#include "surface.hpp"

namespace wsi
{
namespace wayland
{
wsi_ext_present_timing_wayland::wsi_ext_present_timing_wayland(
   const util::allocator &allocator, VkDevice device, uint32_t num_images,
   util::vector<std::optional<uint64_t>> &&timestamp_first_pixel_out_storage, bool stage_first_pixel_out_supported)
   : wsi_ext_present_timing(allocator, device, num_images)
   , m_timestamp_first_pixel_out(allocator)
   , m_stage_first_pixel_out_supported(stage_first_pixel_out_supported)
{
   m_timestamp_first_pixel_out.swap(timestamp_first_pixel_out_storage);
}

util::unique_ptr<wsi_ext_present_timing_wayland> wsi_ext_present_timing_wayland::create(
   VkDevice device, const util::allocator &allocator, std::optional<VkTimeDomainKHR> image_first_pixel_out_time_domain,
   uint32_t num_images)
{

   util::vector<util::unique_ptr<wsi::vulkan_time_domain>> domains(allocator);
   if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
          VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT, VK_TIME_DOMAIN_DEVICE_KHR)))
   {
      return nullptr;
   }

   util::vector<std::optional<uint64_t>> timestamp_first_pixel_out_storage(allocator);
   bool stage_first_pixel_out_supported = false;
   if (image_first_pixel_out_time_domain.has_value())
   {
      std::tuple<VkTimeDomainEXT, bool> monotonic_query = { *image_first_pixel_out_time_domain, false };

      const layer::device_private_data &device_data = layer::device_private_data::get(device);
      auto result = wsi::check_time_domain_support(device_data.physical_device, &monotonic_query, 1);
      if (result != VK_SUCCESS)
      {
         return nullptr;
      }

      if (std::get<1>(monotonic_query))
      {
         if (!domains.try_push_back(allocator.make_unique<wsi::vulkan_time_domain>(
                VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT, image_first_pixel_out_time_domain.value())))
         {
            return nullptr;
         }
         if (!timestamp_first_pixel_out_storage.try_resize(num_images))
         {
            return nullptr;
         }
         stage_first_pixel_out_supported = true;
      }
   }
   else
   {
      timestamp_first_pixel_out_storage.clear();
      timestamp_first_pixel_out_storage.shrink_to_fit();
   }

   return wsi_ext_present_timing::create<wsi_ext_present_timing_wayland>(
      allocator, domains.data(), domains.size(), device, num_images, std::move(timestamp_first_pixel_out_storage),
      stage_first_pixel_out_supported);
}

VkResult wsi_ext_present_timing_wayland::get_swapchain_timing_properties(
   uint64_t &timing_properties_counter, VkSwapchainTimingPropertiesEXT &timing_properties)
{
   timing_properties_counter = 0;
   timing_properties.refreshDuration = 0;
   timing_properties.refreshInterval = 0;

   return VK_SUCCESS;
}

void wsi_ext_present_timing_wayland::mark_delivered(uint32_t image_index, uint64_t time)
{
   pixelout_callback(image_index, time);
   remove_from_pending_present_feedback_list(image_index);
}

void wsi_ext_present_timing_wayland::mark_buffer_release(uint32_t image_index)
{
   auto was_removed = remove_from_pending_present_feedback_list(image_index);
   if (was_removed)
   {
      pixelout_callback(image_index, 0);
   }
}

presentation_feedback *wsi_ext_present_timing_wayland::insert_into_pending_present_feedback_list(
   uint32_t image_index, struct wp_presentation_feedback *feedback_obj, uint64_t id)
{

   util::unique_lock<util::mutex> lock(m_pending_presents_lock);
   if (!lock)
   {
      WSI_LOG_ERROR("Failed to acquire pending presents lock in insert_into_pending_present_feedback_list.\n");
      abort();
   }

   /* We should not be replacing pending entries. This most likely has happened due to events not being triggered
    * that would discard or mark the pending present request as completed which could be an inidcation of a bug somewhere. */
   assert(!m_pending_presents[image_index].has_value());

   m_pending_presents[image_index] = presentation_feedback(feedback_obj, this, image_index, id);
   return &m_pending_presents[image_index].value();
}

bool wsi_ext_present_timing_wayland::remove_from_pending_present_feedback_list(uint32_t image_index)
{
   util::unique_lock<util::mutex> lock(m_pending_presents_lock);
   if (!lock)
   {
      WSI_LOG_ERROR("Failed to acquire pending presents lock in remove_from_pending_present_feedback_list.\n");
      abort();
   }

   const bool has_entry = m_pending_presents[image_index].has_value();
   m_pending_presents[image_index].reset();
   return has_entry;
}

void wsi_ext_present_timing_wayland::pixelout_callback(uint32_t image_index, uint64_t time)
{
   /* m_timestamp_first_pixel_out for a particular index is updated by thread safe Wayland event
    * and is read only after the event had processed. This is because we get the read request
    * either during the get_free_buffer() or by calling the dispatch_queue(). Additionally, the
    * variable being std::optional, there is no time-race happening due to hardware reording.
    * This is because the variable is read after checking for whether it has value.
    * This way each index of the variable is thread safe.
    */
   m_timestamp_first_pixel_out[image_index] = time;
}

VkResult wsi_ext_present_timing_wayland::get_pixel_out_timing_to_queue(
   uint32_t image_index, std::optional<std::reference_wrapper<swapchain_presentation_timing>> stage_timing_optional)
{
   /* Try to get the event until there are no more events in
    * the queue or till we get the presentation callback. */
   while (!m_timestamp_first_pixel_out[image_index].has_value())
   {
      int res = dispatch_queue(m_display, m_queue, 0);
      if (res < 0)
      {
         return VK_ERROR_SURFACE_LOST_KHR;
      }
      else if (res == 0)
      {
         break;
      }
   }
   if (m_timestamp_first_pixel_out[image_index].has_value())
   {
      stage_timing_optional->get().set_time(m_timestamp_first_pixel_out[image_index].value());
      m_timestamp_first_pixel_out[image_index].reset();
   }
   return VK_SUCCESS;
}

void wsi_ext_present_timing_wayland::init(wl_display *display, struct wl_event_queue *queue)
{
   /* These objects shouldn't be set twice */
   assert(m_display == nullptr);
   assert(m_queue == nullptr);
   m_display = display;
   m_queue = queue;
}

VkPresentStageFlagsEXT wsi_ext_present_timing_wayland::stages_supported()
{
   VkPresentStageFlagsEXT stages = VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT;

   if (m_stage_first_pixel_out_supported)
   {
      stages |= VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
   }
   return stages;
}

} // namespace wayland
} // namespace wsi
