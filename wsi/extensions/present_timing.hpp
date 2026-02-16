/*
 * Copyright (c) 2024-2026 Arm Limited.
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
 * @file present_timing.hpp
 *
 * @brief Contains the implementation for the VK_EXT_present_timing extension.
 *
 */

#pragma once

#include <atomic>
#include <iterator>
#include <type_traits>
#include <array>
#include <tuple>
#include <optional>
#include <functional>
#include <cassert>
#include <variant>
#include <cmath>

#include <layer/private_data.hpp>
#include <layer/present_timing_api.hpp>
#include <util/custom_allocator.hpp>
#include <util/custom_mutex.hpp>
#include <util/macros.hpp>
#include <util/wsi_extension.hpp>
#include <wsi/swapchain_base.hpp>

namespace wsi
{

/**
 * @brief Swapchain presentation timing
 *
 * This structure is used to keep the timing parameters for various presentation stages.
 *
 */
struct swapchain_presentation_timing
{
   /**
    * ID of the time domain that used when getting the timestamp.
    */
   uint64_t m_timedomain_id{};

   /**
    * Timestamp for this entry.
    */
   uint64_t m_time{};

   /**
    * Needed to mark “logically complete” timings even for presentations that the WSI
    * implementation ultimately rejected (e.g. in MAILBOX the presentation engine
    * rejected the one present request)
    */
   bool m_set{};

   /**
    * For unsupported present stages (i.e. when stage_supported = false), the m_set is
    * set to true. This allows the application to request stages that are not supported
    * by the backend and get a response of zero.
    */
   swapchain_presentation_timing(bool stage_supported)
      : m_timedomain_id(0)
      , m_time(0)
      , m_set(!stage_supported)
   {
   }

   swapchain_presentation_timing(swapchain_presentation_timing &&) noexcept = default;
   swapchain_presentation_timing &operator=(swapchain_presentation_timing &&) noexcept = default;
   swapchain_presentation_timing(const swapchain_presentation_timing &) = delete;
   swapchain_presentation_timing &operator=(const swapchain_presentation_timing &) = delete;

   void set_time(uint64_t time)
   {
      m_time = time;
      m_set = true;
   }
};

/**
 * @brief Swapchain presentation entry
 *
 * This structure is used to keep the parameters for the swapchain presentation entry.
 *
 */
struct swapchain_presentation_entry
{
   /**
    * Target time used in the present request.
    */
   uint64_t m_target_time{ 0 };

   /**
    * The target stages for the presentation entry.
    */
   VkPresentStageFlagsEXT m_target_stages{ 0 };

   /**
    * The present id. Zero is a valid value for present id.
    */
   uint64_t m_present_id{ 0 };

   /**
    * The image index of the entry in the swapchain.
    */
   uint32_t m_image_index{ 0 };

   /**
    * The number of requested stages for this entry.
    */
   size_t m_num_present_stages;

   /**
    * The queue family used to submit synchronization commands for this entry.
    */
   uint32_t m_queue_family{ 0 };

   /**
    * When serving a get past presentation timings request, this field
    * keeps the status of whether the slot had already been copied to
    * the results.
    */
   bool copied{ false };

   /**
    * The variables to keep timing stages.
    */
   std::optional<swapchain_presentation_timing> m_queue_end_timing;
   std::optional<swapchain_presentation_timing> m_latch_timing;
   std::optional<swapchain_presentation_timing> m_first_pixel_out_timing;
   std::optional<swapchain_presentation_timing> m_first_pixel_visible_timing;

   swapchain_presentation_entry(uint64_t target_time, VkPresentStageFlagsEXT present_stage_queries, uint64_t present_id,
                                uint32_t image_index, uint32_t queue_family, VkPresentStageFlagsEXT stages_supported);
   swapchain_presentation_entry(swapchain_presentation_entry &&) noexcept = default;
   swapchain_presentation_entry &operator=(swapchain_presentation_entry &&) noexcept = default;

   swapchain_presentation_entry(const swapchain_presentation_entry &) = delete;
   swapchain_presentation_entry &operator=(const swapchain_presentation_entry &) = delete;

   /**
    * @brief Check if a present stage is completed.
    *
    * @param stage The stage to get the status for.
    *
    * @return true when the stage is completed and false otherwise.
    */
   std::optional<bool> is_complete(VkPresentStageFlagBitsEXT stage);

   /**
    * @brief Check if a present stage is pending.
    *
    * @param stage The stage to get the status for.
    *
    * @return true when the stage is pending and false otherwise.
    */
   bool is_pending(VkPresentStageFlagBitsEXT stage);

   /**
    * @brief Check if there are outstanding present stages.
    *
    * @return true when there are outstanding stages and false otherwise.
    */
   bool has_outstanding_stages();

   /**
    * @brief Check if there are completed present stages.
    *
    * @return true when there are completed stages and false otherwise.
    */
   bool has_completed_stages(bool allow_partial);

   /**
    * @brief Populate a VkPastPresentationTimingEXT object.
    *
    * @param timing Reference to the timing to be populated.
    */
   void populate(VkPastPresentationTimingEXT &timing);

   /**
    * @brief Get the swapchain_presentation_entry for a present stage.
    *
    * @param stage The stage to get the timing for.
    *
    * @return optional reference to the particular stage, std::nullopt if the stage doesn't exit.
    */
   std::optional<std::reference_wrapper<swapchain_presentation_timing>> get_stage_timing(
      VkPresentStageFlagBitsEXT stage);

   /**
    * @brief Sets the stage timing.
    *
    * @param stage The stage to set the timing for.
    * @param time The time value to be updated.
    */
   void set_stage_timing(VkPresentStageFlagBitsEXT stage, uint64_t time);
};

/* Predefined struct for calibrated time */
struct swapchain_calibrated_time
{
   VkTimeDomainKHR time_domain;
   uint64_t offset;
};

/* Base struct for swapchain time domain */
class swapchain_time_domain
{
public:
   swapchain_time_domain(VkPresentStageFlagsEXT present_stage)
      : m_present_stage(present_stage)
   {
   }

   virtual ~swapchain_time_domain() = default;

   virtual swapchain_calibrated_time calibrate() = 0;

   VkPresentStageFlagsEXT get_present_stages()
   {
      return m_present_stage;
   }

private:
   VkPresentStageFlagsEXT m_present_stage;
};

class vulkan_time_domain : public swapchain_time_domain
{
public:
   vulkan_time_domain(VkPresentStageFlagsEXT present_stage, VkTimeDomainKHR time_domain)
      : swapchain_time_domain(present_stage)
      , m_time_domain(time_domain)
   {
   }

   /* The calibrate function should return a Vulkan time domain + an offset.*/
   swapchain_calibrated_time calibrate() override
   {
      return { m_time_domain, 0 };
   }

private:
   VkTimeDomainKHR m_time_domain;
};

/**
 * @brief Class holding time domains for a swapchain
 *
 */
class swapchain_time_domains
{
public:
   swapchain_time_domains(const util::allocator &allocator)
      : m_time_domains(allocator)
   {
   }

   /**
    * @brief Add new time domain
    *
    * @param time_domain Time domain to add
    * @return true Time domain has been added.
    * @return false Time domain has not been added due to an issue.
    */
   bool add_time_domain(util::unique_ptr<swapchain_time_domain> time_domain);

   /*
    * @brief The calibrate returns a Vulkan time domain + an offset
    *
    * @param present_stage   The present stage to calibrate
    * @param calibrated_time The calibrated time output
    * @return VK_SUCCESS when calibrated successfully VK_ERROR_OUT_OF_HOST_MEMORY otherwise.
    */
   VkResult calibrate(VkPresentStageFlagBitsEXT present_stage, swapchain_calibrated_time *calibrated_time);

   /**
    * @brief Get swapchain time domain properties.
    *
    * @param pSwapchainTimeDomainProperties time domain struct to be set
    * @param pTimeDomainsCounter size of the pSwapchainTimeDomainProperties
    *
    * @return Returns VK_SUCCESS on success, otherwise an appropriate error code.
    */
   static VkResult get_swapchain_time_domain_properties(
      VkSwapchainTimeDomainPropertiesEXT *pSwapchainTimeDomainProperties, uint64_t *pTimeDomainsCounter);

private:
   util::vector<util::unique_ptr<swapchain_time_domain>> m_time_domains;
};

/**
 * @brief Resources specific to a particular queue family index.
 */
class queue_family_resources
{
public:
   queue_family_resources(util::allocator allocator, layer::device_private_data &device)
      : m_command_pool(VK_NULL_HANDLE)
      , m_command_buffer(allocator)
      , m_query_pool(VK_NULL_HANDLE)
      , m_allocator(allocator)
      , m_device(device)
   {
   }

   ~queue_family_resources()
   {
      if (m_command_pool != VK_NULL_HANDLE)
      {
         m_device.disp.FreeCommandBuffers(m_device.device, m_command_pool,
                                          static_cast<uint32_t>(m_command_buffer.size()), m_command_buffer.data());

         m_device.disp.DestroyCommandPool(m_device.device, m_command_pool, m_allocator.get_original_callbacks());
         m_command_pool = VK_NULL_HANDLE;
      }
      if (m_query_pool != VK_NULL_HANDLE)
      {
         m_device.disp.DestroyQueryPool(m_device.device, m_query_pool, m_allocator.get_original_callbacks());
         m_query_pool = VK_NULL_HANDLE;
      }
   }

   VkResult init(uint32_t queue_family_index, uint32_t num_images)
   {
      /* Resize the command buffer to the number of images. */
      if (!m_command_buffer.try_resize(num_images))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      for (auto &command_buffer : m_command_buffer)
      {
         command_buffer = VK_NULL_HANDLE;
      }
      /* Allocate the command pool and query pool. */
      VkQueryPoolCreateInfo query_pool_info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                                nullptr,
                                                VK_QUERY_POOL_CREATE_RESET_BIT_KHR,
                                                VK_QUERY_TYPE_TIMESTAMP,
                                                num_images,
                                                0 };
      TRY_LOG_CALL(m_device.disp.CreateQueryPool(m_device.device, &query_pool_info,
                                                 m_allocator.get_original_callbacks(), &m_query_pool));
      VkCommandPoolCreateInfo command_pool_info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                                 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, queue_family_index };
      TRY_LOG_CALL(m_device.disp.CreateCommandPool(m_device.device, &command_pool_info,
                                                   m_allocator.get_original_callbacks(), &m_command_pool));
      /* Allocate and write the command buffer. */
      VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
                                                          m_command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, num_images };
      TRY_LOG_CALL(
         m_device.disp.AllocateCommandBuffers(m_device.device, &command_buffer_info, m_command_buffer.data()));
      VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr };
      for (uint32_t image_index = 0; image_index < num_images; image_index++)
      {
         TRY_LOG_CALL(m_device.disp.BeginCommandBuffer(m_command_buffer[image_index], &begin_info));
         m_device.disp.CmdResetQueryPool(m_command_buffer[image_index], m_query_pool, image_index, 1u);
         m_device.disp.CmdWriteTimestamp(m_command_buffer[image_index], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                         m_query_pool, image_index);
         TRY_LOG_CALL(m_device.disp.EndCommandBuffer(m_command_buffer[image_index]));
      }

      return VK_SUCCESS;
   }

   /**
    * @brief The command pool for allocating the buffers for the present stage timings.
    */
   VkCommandPool m_command_pool;

   /**
    * @brief The command buffer for the present stage timings.
    */
   util::vector<VkCommandBuffer> m_command_buffer;

   /**
    * @brief Query pool to allocate for present stage timing queries.
    */
   VkQueryPool m_query_pool;

private:
   util::allocator m_allocator;
   layer::device_private_data &m_device;
};

/**
 * @brief Structure describing a scheduled present target.
 */
struct scheduled_present_target
{
   scheduled_present_target(const VkPresentTimingInfoEXT &timing_info)
      : m_time_domain_id(timing_info.timeDomainId)
      , m_flags(timing_info.flags)
      , m_target_present_time(timing_info.targetTime)
   {
   }

   uint64_t m_time_domain_id;
   VkPresentTimingInfoFlagsEXT m_flags;
   uint64_t m_target_present_time;
};

/**
 * @brief Present timing extension class
 *
 * This class implements or act as a base class for the present timing extension
 * features.
 */
class wsi_ext_present_timing : public util::wsi_ext
{
public:
   /**
    * @brief The name of the extension.
    */
   WSI_DEFINE_EXTENSION(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);

   template <typename T, typename... arg_types>
   static util::unique_ptr<T> create(const util::allocator &allocator,
                                     util::unique_ptr<wsi::vulkan_time_domain> *domains, size_t domain_count,
                                     VkDevice device, uint32_t num_images, arg_types &&...args)
   {
      auto present_timing = allocator.make_unique<T>(allocator, device, num_images, std::forward<arg_types>(args)...);
      if (present_timing->wsi_ext_present_timing::init(domains, domain_count) != VK_SUCCESS)
      {
         WSI_LOG_ERROR("Failed to initialize present timing.");
         return nullptr;
      }

      return present_timing;
   }

   /**
    * @brief Constructor for the wsi_ext_present_timing class.
    *
    * @param allocator             Reference to the custom allocator.
    * @param device                The device to which the swapchain belongs.
    * @param num_images            Number of images in the swapchain.
    *
    */
   wsi_ext_present_timing(const util::allocator &allocator, VkDevice device, uint32_t num_images);

   /**
    * @brief Destructor for the wsi_ext_present_timing class.
    */
   virtual ~wsi_ext_present_timing();

   /**
    * @brief Set the queue size for the present timing queue.
    *
    * @param queue_size The new queue size to set.
    *
    * @return VK_SUCCESS on if the queue size was updated correctly.
    * VK_NOT_READY when the number of present timing outstanding
    * items are larger than the queue size. VK_ERROR_OUT_OF_HOST_MEMORY
    * when there is no host memory available for the new size.
    */
   VkResult present_timing_queue_set_size(size_t queue_size);

   /**
    * @brief Add a presentation entry to the present timing queue.
    *
    * @param device                The device private data.
    * @param queue                 The Vulkan queue used to submit synchronization commands.
    * @param present_id            The present id of the current presentation.
    * @param image_index           The index of the image in the swapchain.
    * @param target_time           The target time for the presentation.
    * @param present_stage_queries The present stages application had requested timings for.
    *
    * @return VK_SUCCESS when the entry was inserted successfully and VK_ERROR_OUT_OF_HOST_MEMORY
    * when there is no host memory.
    */
   VkResult add_presentation_query_entry(VkQueue queue, uint64_t present_id, uint32_t image_index, uint64_t target_time,
                                         VkPresentStageFlagsEXT present_stage_queries);

   /**
    * @brief Add a presentation target entry.
    *
    * @param image_index The index of the image in the swapchain.
    * @param timing_info The timing info for the presentation target.
    */
   void add_presentation_target_entry(uint32_t image_index, const VkPresentTimingInfoEXT &timing_info);

   /**
    * @brief Remove a presentation target entry.
    *
    * @param image_index The index of the image in the swapchain for which to remove the entry.
    */
   void remove_presentation_target_entry(uint32_t image_index);

   /**
    * @brief Get the presentation target entry for @p image_index if any
    *
    * @param image_index The index of the image in the swapchain.
    * @return Scheduled present target if any exists currently for the image.
    */
   std::optional<scheduled_present_target> get_presentation_target_entry(uint32_t image_index);

   /**
    * @brief Add a presentation entry to the present timing queue.
    *
    * @param queue                 The Vulkan queue used to submit synchronization commands.
    * @param present_id            The present id of the current presentation.
    * @param image_index           The index of the image in the swapchain.
    * @param timing_info           The timing info for the presentation.
    *
    * @return VK_SUCCESS when the entry was inserted successfully, error otherwise.
    */
   VkResult add_presentation_entry(VkQueue queue, uint64_t present_id, uint32_t image_index,
                                   const VkPresentTimingInfoEXT &timing_info);

   /**
    * @brief Check whether the queue has space for an entry.
    *
    * @return VK_SUCCESS when the there is space left in the queue, error otherwise.
    */
   VkResult queue_has_space();

   /**
    * @brief Set the time for a stage, if it exists and is pending.
    *
    * @param image_index The index of the image in the present queue.
    * @param stage The present stage to set the time for.
    * @param time The time to set for the stage.
    */
   void set_pending_stage_time(uint32_t image_index, VkPresentStageFlagBitsEXT stage, uint64_t time)
   {
      const util::unique_lock<util::mutex> lock(m_queue_mutex);
      if (!lock)
      {
         WSI_LOG_ERROR("Failed to acquire queue mutex in set_pending_stage_time.");
         abort();
      }
      if (auto timing = get_pending_stage_timing(image_index, stage))
      {
         timing->set_time(time);
      }
   }

   /**
    * @brief Get the image's present semaphore.
    *
    * @param image_index Image's index
    *
    * @return the image's present semaphore.
    */
   VkSemaphore get_image_present_semaphore(uint32_t image_index);

   /**
    * @brief Get the results of the past presentation from the internal queue.
    *
    * @param past_present_timing_properties Pointer for returing results.
    *
    * @return VK_SUCCESS when the requested results are returned, VK_INCOMPLETE when returning fewer results.
    */
   VkResult get_past_presentation_results(VkPastPresentationTimingPropertiesEXT *past_present_timing_properties,
                                          VkPastPresentationTimingFlagsEXT flags);

   /**
    * @brief Get the swapchain time domains
    */
   swapchain_time_domains &get_swapchain_time_domains();

   /**
    * @brief Check whether the stage is pending with the provided image index.
    *
    * @param image_index The index of the image.
    * @param present_stage The present stage to be checked.
    *
    * @return true when the stage is pending of the image index provided and false otherwise.
    */
   bool is_stage_pending_for_image_index(uint32_t image_index, VkPresentStageFlagBitsEXT present_stage);

   /**
    * @brief Backend specific implementation of the vkGetSwapchainTimingPropertiesEXT
    * entrypoint.
    *
    * @param timing_properties_counter Timing properties counter.
    * @param timing_properties Timing properties.
    *
    * @return Vulkan result code.
    */
   virtual VkResult get_swapchain_timing_properties(uint64_t &timing_properties_counter,
                                                    VkSwapchainTimingPropertiesEXT &timing_properties) = 0;
   /**
    * @brief This function will get called to update the first visible pixel timing in the internal queue.
    * Backends could optionally override the API to implement backend specific update for the stage.
    *
    * @param image_index The index of the image.
    * @param stage_timing_optional The optional stage timing reference wrapper.
    *
    * @return Vulkan result code.
    */
   virtual VkResult get_pixel_out_timing_to_queue(
      uint32_t image_index, std::optional<std::reference_wrapper<swapchain_presentation_timing>> stage_timing_optional);

   /**
    * @brief Determines if present_timing is supported by the physical device.
    *
    * @param physical_device The physical device to check.
    * @param out Reference to a boolean that will be set to true if the physical device has a queue family that supports
    *            presentation and timestamp queries, false otherwise.
    *
    * @return VK_SUCCESS iff out set.
    */
   static VkResult physical_device_has_supported_queue_family(VkPhysicalDevice physical_device, bool &out);

   /**
    * @brief This function is used to check the present timing stages that are supported. It can be overriden by specific backends.
    *
    * @return The stages that are supported.
    */
   virtual VkPresentStageFlagsEXT stages_supported() = 0;

   /**
    * @brief Check if a present stage is supported.
    *
    * @param present_stage Present stage to check.
    * @return true if stage is supported, false otherwise.
    */
   bool is_present_stage_supported(VkPresentStageFlagBitsEXT present_stage);

protected:
   /**
    * @brief User provided memory allocation callbacks.
    */
   const util::allocator m_allocator;

private:
   /**
    *  @brief Handle the backend specific time domains for each present stage.
    */
   swapchain_time_domains m_time_domains;

   /**
    *  @brief The Vulkan layer device.
    */
   layer::device_private_data &m_device;

   /**
    * @brief The presentation timing queue.
    */
   util::vector<swapchain_presentation_entry> m_queue;

   /**
    * @brief Stores the device timestamp recorded from the previous
    * VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT stage for each image
    * index of the swapchain.
    */
   util::vector<uint64_t> m_device_timestamp_cached;

   /**
    * @brief Mutex guarding the internal presentation-timing queue.
    *
    * Public methods lock this mutex before accessing the queue.
    * Private helpers assume **the caller already holds the lock**; that
    * pre-condition must be met before invoking them.
    */
   util::mutex m_queue_mutex;

   /**
    * @brief The presentation target entries.
    */
   util::vector<std::optional<scheduled_present_target>> m_scheduled_present_targets;

   /**
    * @brief The number of images in the swapchain.
    */
   uint32_t m_num_images;

   /**
    * @brief Semaphore per image.
    */
   util::vector<VkSemaphore> m_present_semaphore;

   /**
    * @brief The timestamp period from the device properties.
    */
   float m_timestamp_period;

   /**
    * @brief Resources associated with the 'best' queue family.
    */
   queue_family_resources m_queue_family_resources;

   /**
    * @brief Perform a queue submission for getting the queue end timing.
    *
    * @param device      The device private data.
    * @param queue       The Vulkan queue used to submit synchronization commands.
    * @param image_index The index of the image in the swapchain.
    *
    * @return VK_SUCCESS when the submission is successful and error otherwise.
    */
   VkResult queue_submit_queue_end_timing(const layer::device_private_data &device, VkQueue queue,
                                          uint32_t image_index);

   /**
    * @brief Initialize the present timing extension.
    *
    * @param domains Array of time domains.
    * @param domain_count Size of the @p domains array.
    * @return VK_SUCCESS when the initialization is successful and error otherwise.
    */
   VkResult init(util::unique_ptr<wsi::vulkan_time_domain> *domains, size_t domain_count);

   /**
    * @brief Get all the pending results that are available to the queue.
    *
    * @retval VK_SUCCESS if there are no errors with copying the results,
    * error otherwise.
    * @pre Caller must hold m_queue_mutex for the call and lifetime of the returned pointer.
    *
    * @brief Search for a pending presentation entry and access its timing info.
    *
    * For an image index, there can only be one entry in the queue with pending stages.
    * This does not take a present ID because zero is a valid, nonunique value and thus cannot uniquely identify an
    * entry.
    *
    * @param image_index The index of the image in the present queue.
    * @param stage The present stage to get the entry for.
    *
    * @return Pointer to timing information for the stage, or nullptr if it is not found or it is not pending.
    */
   swapchain_presentation_timing *get_pending_stage_timing(uint32_t image_index, VkPresentStageFlagBitsEXT stage);

   /**
    * @pre Caller must hold m_queue_mutex for the call and lifetime of the returned pointer.
    *
    * @brief Search for a pending presentation entry.
    *
    * For an image index, there can only be one entry in the queue with pending stages.
    * This does not take a present ID because zero is a valid, nonunique value and thus cannot uniquely identify an
    * entry.
    *
    * @param image_index The index of the image in the present queue.
    * @param stage The present stage to get the entry for.
    *
    * @return Pointer to the entry, or nullptr if it is not found or the stage is not pending.
    */
   swapchain_presentation_entry *get_pending_stage_entry(uint32_t image_index, VkPresentStageFlagBitsEXT stage);

   /**
    * @pre Caller must hold m_queue_mutex
    *
    * @brief Get all the pending results that are available to the queue.
    *
    * @retval VK_SUCCESS if there are no errors with copying the results,
    * error otherwise.
    */
   VkResult write_pending_results();

   /**
    * @pre Caller must hold m_queue_mutex
    *
    * @brief Get the number of results that are available in the internal queue.
    *
    * @return The number of available results.
    */
   uint32_t get_num_available_results(VkPastPresentationTimingFlagsEXT flags);

   /**
    * @pre Caller must hold m_queue_mutex
    *
    * @brief Get the number of outstanding present timing results.
    *
    * @return The size of the currently outstanding present timing items.
    */
   size_t present_timing_get_num_outstanding_results();
};

/**
 * @brief Check if any of the time domains are supported
 *
 * @param physical_device Physical device used for the query
 * @param domains Array of time domains. The boolean will be modified to indicate
 *                whether the domain is supported.
 * @param domain_size Size of the @p domains array
 * @return Vulkan result code
 */
VkResult check_time_domain_support(VkPhysicalDevice physical_device, std::tuple<VkTimeDomainEXT, bool> *domains,
                                   size_t domain_size);

/**
 * @brief Checks whether present timing dependencies are supported on a given physical device.
 *
 * This function queries the list of available device extensions for the specified
 * Vulkan physical device and determines whether the `VK_KHR_maintenance9` extension
 * is supported. If the extension is found, it further queries the device features
 * to check if the `maintenance9` feature is enabled.
 *
 * @param physical_device Physical device used for the query
 *
 * @return std::variant<bool, VkResult>
 *         - `true`  if the device supports present timing dependencies
 *                    (i.e., `VK_KHR_maintenance9` extension and its feature are available).
 *         - `false` if the device does not support the `VK_KHR_maintenance9` extension or its feature.
 *         - `VkResult` (e.g., `VK_ERROR_OUT_OF_HOST_MEMORY`) if an error occurs during property enumeration.
 */
std::variant<bool, VkResult> present_timing_dependencies_supported(VkPhysicalDevice physical_device);

/**
 * @brief Converts a hardware tick count to nanoseconds.
 *
 * This function converts a given number of GPU hardware timestamp ticks
 * into nanoseconds using the provided timestamp period.
 *
 * @param ticks The number of timestamp ticks to convert.
 * @param timestamp_period The duration of a single tick, in nanoseconds per tick.
 *
 * @return The equivalent time duration in nanoseconds.
 *
 */
inline uint64_t ticks_to_ns(uint64_t ticks, const float &timestamp_period)
{
   /* timestamp_period is float (ns per tick).  Use double so we keep
      52-bit integer precision (≈4.5×10¹⁵ ticks) without overflow. */
   assert(std::isfinite(timestamp_period) && timestamp_period > 0.0f);
   double ns = static_cast<double>(ticks) * static_cast<double>(timestamp_period);
   return static_cast<uint64_t>(std::llround(ns));
}

} /* namespace wsi */
