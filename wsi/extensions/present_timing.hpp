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
 * @brief Contains the implentation for the VK_EXT_present_timing extension.
 *
 */

#pragma once

#include <layer/wsi_layer_experimental.hpp>
#include <util/custom_allocator.hpp>
#include <util/macros.hpp>

#include <iterator>
#include <type_traits>

#include "wsi_extension.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL
namespace wsi
{

/**
 * @brief Swapchain presentation entry
 *
 * This structure is used to keep the parameters for the swapchain presentation entry.
 *
 */
struct swapchain_presentation_entry
{
   /**
    * Whether this entry is an outstanding result or not.
    */
   bool is_outstanding{ false };
   /**
    * The present id.
    */
   uint64_t present_id{ 0 };
};

/**
 * @brief Timings queue
 *
 * This structure is used to keep the parameters related to the presentation timing queue.
 *
 */
struct timings_queue
{
   timings_queue(const util::allocator &allocator)
      : m_timings(allocator)
   {
   }

   util::vector<swapchain_presentation_entry> m_timings;
};

// Predefined struct for calibrated time
struct swapchain_calibrated_time
{
   VkTimeDomainKHR time_domain;
   uint64_t offset;
};

// Base struct for swapchain time domain
class swapchain_time_domain
{
public:
   swapchain_time_domain(VkPresentStageFlagsEXT presentStages)
      : m_present_stages(presentStages)
   {
   }

   virtual swapchain_calibrated_time calibrate() = 0;

   VkPresentStageFlagsEXT get_present_stages()
   {
      return m_present_stages;
   }

private:
   VkPresentStageFlagsEXT m_present_stages;
};

class vulkan_time_domain : public swapchain_time_domain
{
public:
   vulkan_time_domain(VkPresentStageFlagsEXT presentStages, VkTimeDomainKHR time_domain)
      : swapchain_time_domain(presentStages)
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

   VkResult calibrate(VkPresentStageFlagBitsEXT presentStages, swapchain_calibrated_time *calibrated_time);

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

   template <typename T, std::size_t N>
   static util::unique_ptr<T> create(const util::allocator &allocator,
                                     std::array<util::unique_ptr<wsi::vulkan_time_domain>, N> &domains)
   {
      auto present_timing = allocator.make_unique<T>(allocator);
      for (auto &domain : domains)
      {
         if (!present_timing->get_swapchain_time_domains().add_time_domain(std::move(domain)))
         {
            WSI_LOG_ERROR("Failed to add a time domain.");
            return nullptr;
         }
      }

      return present_timing;
   }

   /**
    * @brief Constructor for the wsi_ext_present_timing class.
    */
   wsi_ext_present_timing(const util::allocator &allocator);

   /**
    * @brief Destructor for the wsi_ext_present_timing class.
    */
   virtual ~wsi_ext_present_timing();

   /**
    * @brief Set the queue size for the present timing queue.
    *
    * This API allows modifying the queue size of the present timing queue.
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
    * @brief Get the number of outstanding presnt timing results.
    *
    * This API allows getting the number of the current outstanding results.
    *
    * @return The size of the currently outstanding present timing items.
    */
   size_t present_timing_get_num_outstanding_results();

   /**
    * @brief Add a presentation entry to the present timing queue.
    *
    * This API pushes a presentation entry to the present timing queue.
    *
    * @param sc_presentation_entry Reference to the presentation entry to be added.
    *
    * @return VK_SUCCESS when the entry was inserted successfully and VK_ERROR_OUT_OF_HOST_MEMORY
    * when there is no host memory.
    */
   VkResult add_presentation_entry(const wsi::swapchain_presentation_entry &sc_presentation_entry);

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
    * @brief The presentation timing queue.
    */
   timings_queue m_queue;

   /**
    *  @brief Handle the backend specific time domains for each present stage.
    */
   swapchain_time_domains m_time_domains;
};

} /* namespace wsi */
#endif