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
 * @file present_timing.hpp
 *
 * @brief Contains the implementation for the VK_EXT_present_timing extension.
 *
 */

#pragma once

#include <layer/wsi_layer_experimental.hpp>
#include <layer/private_data.hpp>
#include <util/custom_allocator.hpp>
#include <util/macros.hpp>

#include <atomic>
#include <iterator>
#include <type_traits>
#include <array>
#include <tuple>
#include <optional>
#include <functional>

#include "wsi_extension.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL
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
   std::atomic<uint64_t> m_time{};

   /**
    * Needed to mark “logically complete” timings even for presentations that the WSI
    * implementation ultimately rejected (e.g. in MAILBOX the presentation engine
    * rejected the one present request)
    */
   std::atomic<bool> m_set{};

   swapchain_presentation_timing()
   {
   }

   swapchain_presentation_timing(swapchain_presentation_timing &&rhs) noexcept
   {
      m_timedomain_id = rhs.m_timedomain_id;
      m_time.store(rhs.m_time.load());
      m_set.store(rhs.m_set.load());
   }

   swapchain_presentation_timing &operator=(swapchain_presentation_timing &&rhs) noexcept
   {
      m_timedomain_id = rhs.m_timedomain_id;
      m_time.store(rhs.m_time.load());
      m_set.store(rhs.m_set.load());
      return *this;
   }

   swapchain_presentation_timing(const swapchain_presentation_timing &) = delete;
   swapchain_presentation_timing &operator=(const swapchain_presentation_timing &) = delete;
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
    * The variables to keep timing stages.
    */
   std::optional<swapchain_presentation_timing> m_queue_end_timing;
   std::optional<swapchain_presentation_timing> m_latch_timing;
   std::optional<swapchain_presentation_timing> m_first_pixel_out_timing;
   std::optional<swapchain_presentation_timing> m_first_pixel_visible_timing;

   swapchain_presentation_entry(VkPresentStageFlagsEXT present_stage_queries, uint64_t present_id,
                                uint32_t image_index);
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
   VkResult get_swapchain_time_domain_properties(VkSwapchainTimeDomainPropertiesEXT *pSwapchainTimeDomainProperties,
                                                 uint64_t *pTimeDomainsCounter);

private:
   util::vector<util::unique_ptr<swapchain_time_domain>> m_time_domains;
};

/**
 * @brief Present timing extension class
 *
 * This class implements or act as a base class for the present timing extension
 * features.
 */
class wsi_ext_present_timing : public wsi_ext
{
public:
   /**
    * @brief The name of the extension.
    */
   WSI_DEFINE_EXTENSION(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);

   template <typename T>
   static util::unique_ptr<T> create(const util::allocator &allocator,
                                     util::unique_ptr<wsi::vulkan_time_domain> *domains, size_t domain_count,
                                     VkDevice device, uint32_t num_images)
   {
      auto present_timing = allocator.make_unique<T>(allocator, device, num_images);
      for (size_t i = 0; i < domain_count; i++)
      {
         if (!present_timing->get_swapchain_time_domains().add_time_domain(std::move(domains[i])))
         {
            WSI_LOG_ERROR("Failed to add a time domain.");
            return nullptr;
         }
      }
      if (present_timing->init_timing_resources() != VK_SUCCESS)
      {
         WSI_LOG_ERROR("Failed to initialize present timing.");
         return nullptr;
      }

      return present_timing;
   }

   /**
    * @brief Constructor for the wsi_ext_present_timing class.
    *
    * @param allocator  Reference to the custom allocator.
    * @param device     The device to which the swapchain belongs.
    * @param num_images Number of images in the swapchain.
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
    * @param present_stage_queries The present stages application had requested timings for.
    *
    * @return VK_SUCCESS when the entry was inserted successfully and VK_ERROR_OUT_OF_HOST_MEMORY
    * when there is no host memory.
    */
   VkResult add_presentation_entry(const layer::device_private_data &device, VkQueue queue, uint64_t present_id,
                                   uint32_t image_index, VkPresentStageFlagsEXT present_stage_queries);

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
    * @brief Backend specific implementation of the vkGetSwapchainTimingPropertiesEXT
    * entrypoint.
    *
    * @param timing_properties_counter Timing properties counter.
    * @param timing_properties Timing properties.
    * @return Vulkan result code.
    */
   virtual VkResult get_swapchain_timing_properties(uint64_t &timing_properties_counter,
                                                    VkSwapchainTimingPropertiesEXT &timing_properties) = 0;

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
    *  @brief The Vulkan device.
    */
   VkDevice m_device;

   /**
    * @brief Query pool to allocate for present stage timing queries.
    */
   VkQueryPool m_query_pool;

   /**
    * @brief The command pool for allocating the buffers for the present stage timings.
    */
   VkCommandPool m_command_pool;

   /**
    * @brief The command buffer for the present stage timings.
    */
   util::vector<VkCommandBuffer> m_command_buffer;

   /**
    * @brief Mutex guarding the internal presentation-timing queue.
    *
    * Public methods lock this mutex before accessing the queue.
    * Private helpers assume **the caller already holds the lock**; that
    * pre-condition must be met before invoking them.
    */
   std::mutex m_queue_mutex;

   /**
    * @brief The presentation timing queue.
    */
   util::vector<swapchain_presentation_entry> m_queue;

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
    * @brief Perform a queue submission for getting the queue end timing.
    *
    * @param device      The device private data.
    * @param queue       The Vulkan queue used to submit synchronization commands.
    * @param image_index The index of the image in the swapchain.
    *
    * @return VK_SUCCESS when the submission is successfully and error otherwise.
    */
   VkResult queue_submit_queue_end_timing(const layer::device_private_data &device, VkQueue queue,
                                          uint32_t image_index);

   /**
    * @brief Initialize resources for timing queries.
    *
    * @return VK_SUCCESS if the initialization is successful and error if otherwise.
    */
   VkResult init_timing_resources();

   /**
    * @pre Caller must hold m_queue_mutex.
    *
    * @brief Get the queue end timings for an image.
    *
    * Gets the queue end timings for a swapchain image and stores it in the internal queue.
    *
    * @param image_index The index of the image in the swapchain.
    *
    * @return VK_SUCCESS if the query is successful and error if otherwise.
    */
   VkResult get_queue_end_timing_to_queue(uint32_t image_index);

   /**
    * @brief Query and get every completed queue-end timing.
    *
    * Any slots that were read are then reset so they can be reused.
    *
    * @retval VK_SUCCESS if the records are copied successfully or partially
    */
   VkResult query_present_queue_end_timings();

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

} /* namespace wsi */
#endif
