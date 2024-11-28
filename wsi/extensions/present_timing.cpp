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
#include <cassert>
#include <wsi/swapchain_base.hpp>

#include "present_timing.hpp"

#if VULKAN_WSI_LAYER_EXPERIMENTAL
namespace wsi
{

wsi_ext_present_timing::wsi_ext_present_timing(const util::allocator &allocator)
   : m_allocator(allocator)
   , m_queue(allocator)
   , m_time_domains(allocator)
{
}

wsi_ext_present_timing::~wsi_ext_present_timing()
{
}

VkResult wsi_ext_present_timing::present_timing_queue_set_size(size_t queue_size)
{
   if (present_timing_get_num_outstanding_results() > queue_size)
   {
      return VK_NOT_READY;
   }

   util::vector<swapchain_presentation_entry> presentation_timing(
      util::allocator(m_allocator, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE));
   if (!presentation_timing.try_reserve(queue_size))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (auto iter : m_queue.m_timings)
   {
      if (iter.is_outstanding)
      {
         if (!presentation_timing.try_push_back(iter))
         {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
      }
   }
   m_queue.m_timings.swap(presentation_timing);
   return VK_SUCCESS;
}

size_t wsi_ext_present_timing::present_timing_get_num_outstanding_results()
{
   size_t num_outstanding = 0;

   for (const auto &iter : m_queue.m_timings)
   {
      if (iter.is_outstanding)
      {
         num_outstanding++;
      }
   }
   return num_outstanding;
}

VkResult wsi_ext_present_timing::add_presentation_entry(const wsi::swapchain_presentation_entry &presentation_entry)
{
   if (!m_queue.m_timings.try_push_back(presentation_entry))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   return VK_SUCCESS;
}

swapchain_time_domains &wsi_ext_present_timing::get_swapchain_time_domains()
{
   return m_time_domains;
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
      if (pSwapchainTimeDomainProperties == nullptr)
      {
         *pTimeDomainsCounter = 1;
         return VK_SUCCESS;
      }
      pSwapchainTimeDomainProperties->timeDomainCount = 1;
      pSwapchainTimeDomainProperties->pTimeDomains[0] = VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT;
      pSwapchainTimeDomainProperties->pTimeDomainIds = 0;

      return (*pTimeDomainsCounter < 1) ? VK_INCOMPLETE : VK_SUCCESS;
   }

   if (pSwapchainTimeDomainProperties != nullptr)
   {
      if (pSwapchainTimeDomainProperties->pTimeDomains == nullptr &&
          pSwapchainTimeDomainProperties->pTimeDomainIds == nullptr)
      {
         pSwapchainTimeDomainProperties->timeDomainCount = 1;
         return VK_SUCCESS;
      }
      if (pSwapchainTimeDomainProperties->pTimeDomains != nullptr &&
          pSwapchainTimeDomainProperties->pTimeDomainIds != nullptr)
      {
         pSwapchainTimeDomainProperties->timeDomainCount = 1;
         pSwapchainTimeDomainProperties->pTimeDomains[0] = VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT;
         pSwapchainTimeDomainProperties->pTimeDomainIds = 0;
         return VK_SUCCESS;
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
