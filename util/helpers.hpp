/*
 * Copyright (c) 2021-2022, 2024-2025 Arm Limited.
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
 * @file helpers.hpp
 *
 * @brief Contains common utility functions used across the project.
 */

#pragma once

#include <vulkan/vulkan.h>
#include "log.hpp"

/*
 * Conditional return statement. This allows functions to return early for
 * failing Vulkan commands with a terse syntax.
 *
 * Example usage:
 *
 * VkResult foo()
 * {
 *    TRY(vkCommand());
 *    return VK_SUCCESS;
 * }
 *
 * The above is equivalent to:
 *
 * VkResult foo()
 * {
 *    VkResult result = vkCommand();
 *    if (result != VK_SUCCESS)
 *    {
 *       return result;
 *    }
 *    return VK_SUCCESS;
 * }
 */
#define TRY_HANDLER(expression, do_log, ...) \
   do                                        \
   {                                         \
      VkResult try_result = expression;      \
      if (try_result != VK_SUCCESS)          \
      {                                      \
         if (do_log)                         \
         {                                   \
            WSI_LOG_ERROR(__VA_ARGS__);      \
         }                                   \
         return try_result;                  \
      }                                      \
   } while (0)

#define TRY(expression) TRY_HANDLER(expression, false, "PLACEHOLDER")
#define TRY_LOG_CALL(expression) TRY_HANDLER(expression, true, #expression)
#define TRY_LOG(expression, ...) TRY_HANDLER(expression, true, __VA_ARGS__)

namespace util
{
/**
 * @brief Search a Vulkan pNext chain (read-only) for a specific extension struct.
 *
 * Use for input pNext chains (application → driver), e.g. VkSwapchainCreateInfoKHR::pNext.
 * Walks the chain via VkBaseInStructure and returns the first match.
 *
 * @tparam T     Vulkan struct type to find (e.g., VkImageFormatListCreateInfo)
 * @param sType  VkStructureType corresponding to T
 * @param pNext  Head of the pNext chain (may be nullptr)
 * @return const T* to the first matching node; nullptr if not found
 *
 * @note Unknown sTypes are skipped per Vulkan forward-compat rules.
 */
template <typename T>
inline const T *find_extension(VkStructureType sType, const void *pNext) noexcept
{
   auto p = reinterpret_cast<const VkBaseInStructure *>(pNext);
   while (p)
   {
      if (p->sType == sType)
      {
         return reinterpret_cast<const T *>(p);
      }
      p = p->pNext;
   }
   return nullptr;
}

/**
 * @brief Search a Vulkan pNext chain (writable) for a specific extension struct.
 *
 * Use for output pNext chains (driver → application), e.g. VkPhysicalDeviceFeatures2::pNext.
 * Walks the chain via VkBaseOutStructure and returns the first match.
 *
 * @tparam T     Vulkan struct type to find (e.g., VkPhysicalDeviceVulkan11Features)
 * @param sType  VkStructureType corresponding to T
 * @param pNext  Head of the pNext chain (may be nullptr)
 * @return T* to the first matching node; nullptr if not found
 *
 * @note Unknown sTypes are skipped per Vulkan forward-compat rules.
 */
template <typename T>
inline T *find_extension(VkStructureType sType, void *pNext) noexcept
{
   auto p = reinterpret_cast<VkBaseOutStructure *>(pNext);
   while (p)
   {
      if (p->sType == sType)
      {
         return reinterpret_cast<T *>(p);
      }
      p = p->pNext;
   }
   return nullptr;
}

template <typename T>
inline T shallow_copy_extension(const T *structure_to_copy)
{
   T shallow_copy = *structure_to_copy;
   shallow_copy.pNext = nullptr;

   return shallow_copy;
}

class noncopyable
{
protected:
   noncopyable() = default;
   ~noncopyable() = default;

private:
   noncopyable(const noncopyable &) = delete;
   noncopyable &operator=(const noncopyable &) = delete;
};

static constexpr uint32_t MAX_PLANES = 4;

/**
 * @brief Helper variable for plane image aspect flag bits.
 */
const VkImageAspectFlagBits PLANE_FLAG_BITS[MAX_PLANES] = {
   VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
   VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
   VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
   VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT,
};

} // namespace util
