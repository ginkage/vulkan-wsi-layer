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
 * @file
 *
 * @brief Contains the class header for swapchain images.
 */

#pragma once

#include <memory>
#include <array>
#include <utility>
#include <variant>
#include <functional>
#include <vulkan/vulkan.h>

#include <util/custom_allocator.hpp>
#include <util/drm/drm_utils.hpp>
#include <layer/private_data.hpp>

#include "synchronization.hpp"

namespace wsi
{

class swapchain_image_factory;

/**
 * @brief Base class describing swapchain image memory allocator/binder
 */
class image_backing_memory : private util::noncopyable
{
public:
   virtual ~image_backing_memory() = default;

   /**
    * @brief Bind Vulkan image
    *
    * @param bind_image_mem_info Bind info
    * @return Vulkan result code
    */
   virtual VkResult bind(const VkBindImageMemoryInfo *bind_image_mem_info) = 0;

   /**
    * @brief Get the modifier used for the image
    *
    * @return uint64_t DRM format modifier
    */
   virtual uint64_t get_modifier() const = 0;
};

/**
 * @brief Base class describing swapchain image data
 */
class swapchain_image_data : private util::noncopyable
{
public:
   virtual ~swapchain_image_data() = default;
};

/**
 * @brief Class describing a swapchain image, its syncronisation objects
 *        and any additional data attached to it
 */
class swapchain_image
{
public:
   /**
    * @brief Swapchain state
    */
   enum status
   {
      ACQUIRED,    // Image has been acquired through vkAcquireNextImage operation
      PENDING,     // Image has been submitted for presentation is waiting to be presented
      PRESENTED,   // Image has been presented
      FREE,        // Image is free to be acquired
      UNALLOCATED, // Image has yet to have memory and other resources allocated for it
   };

   ~swapchain_image()
   {
      destroy();
   }

   swapchain_image(swapchain_image &&other) noexcept
      : m_present_semaphore(std::exchange(other.m_present_semaphore, VK_NULL_HANDLE))
      , m_present_fence_wait_semaphore(std::exchange(other.m_present_fence_wait_semaphore, VK_NULL_HANDLE))
      , m_present_fence(std::move(other.m_present_fence))
      , m_wait_on_present_fence(other.m_wait_on_present_fence)
      , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
      , m_status(std::exchange(other.m_status, swapchain_image::UNALLOCATED))
      , m_allocator(other.m_allocator)
      , m_device_data(std::exchange(other.m_device_data, nullptr))
      , m_image_memory(std::move(other.m_image_memory))
      , m_data(std::move(other.m_data))
   {
   }

   swapchain_image &operator=(swapchain_image &&other) noexcept
   {
      if (this == &other)
      {
         return *this;
      }

      std::swap(m_present_semaphore, other.m_present_semaphore);
      std::swap(m_present_fence_wait_semaphore, other.m_present_fence_wait_semaphore);
      std::swap(m_present_fence, other.m_present_fence);
      std::swap(m_wait_on_present_fence, other.m_wait_on_present_fence);
      std::swap(m_image, other.m_image);
      std::swap(m_status, other.m_status);
      std::swap(m_allocator, other.m_allocator);
      std::swap(m_device_data, other.m_device_data);
      std::swap(m_image_memory, other.m_image_memory);
      std::swap(m_data, other.m_data);

      return *this;
   }

   /**
    * @brief Get the image object
    *
    * @return VkImage
    */
   VkImage get_image() const
   {
      return m_image;
   }

   /**
    * @brief Get the status of the swapchain image
    *
    * @return Status of the image
    */
   status get_status() const
   {
      return m_status;
   }

   /**
    * @brief Set the status of the swapchain image
    *
    * @param new_status New status
    */
   void set_status(status new_status)
   {
      m_status = new_status;
   }

   /**
    * @brief Get the present semaphore of the image
    *
    * @return VkSemaphore
    */
   VkSemaphore get_present_semaphore() const
   {
      return m_present_semaphore;
   }

   /**
    * @brief Get the present fence wait semaphore of the image
    *
    * @return VkSemaphore
    */
   VkSemaphore get_present_fence_wait_semaphore() const
   {
      return m_present_fence_wait_semaphore;
   }

   /**
    * @brief Get the present fence synchronisation object
    *
    * @return Fence sync object. Note that this can be nullptr if the image has been destroyed.
    */
   fence_sync *get_present_fence() const
   {
      return m_present_fence.get();
   }

   /**
    * @brief Bind Vulkan image
    *
    * @param bind_image_mem_info Bind info
    * @return Vulkan result code
    */
   VkResult bind(const VkBindImageMemoryInfo *bind_image_mem_info);

   /**
    * @brief Sets the present payload for a swapchain image.
    *
    * @param[in] image            The swapchain image for which to set a present payload.
    * @param     queue            A Vulkan queue that can be used for any Vulkan commands needed.
    * @param[in] semaphores       The wait and signal semaphores and their number of elements.
    * @param[in] submission_pnext Chain of pointers to attach to the payload submission.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult set_present_payload(VkQueue queue, const queue_submit_semaphores &semaphores, const void *submission_pnext);

   /**
    * @brief Wait on the present fence
    *
    * @param timeout_ns Timeout in nanoseconds
    * @return Vulkan result code
    */
   VkResult wait_present(uint64_t timeout_ns);

   /**
    * @brief Get the image data object
    *
    * @tparam T The type of the data object
    * @return The image data object
    */
   template <typename T>
   T *get_data()
   {
      return static_cast<T *>(m_data.get());
   }

   /**
    * @brief Set the image data object
    *
    * @param data Data object
    */
   void set_data(util::unique_ptr<swapchain_image_data> data)
   {
      m_data = std::move(data);
   }

   uint64_t get_modifier() const
   {
      return m_image_memory->get_modifier();
   }

private:
   /* Only allow swapchain image factory to create this class and acquire backing memory from it */
   friend swapchain_image_factory;

   /**
    * @brief Construct a new swapchain image object
    *
    * @param image_handle Vulkan image to use for this swapchain image
    * @param present_semaphore Present semaphore
    * @param present_fence_wait_semaphore Present fence wait semaphore
    * @param present_fence Present fence synchronisation object
    * @param wait_on_present_fence Whether wait_present will wait on the @p present_fence
    * @param device_data Device data
    * @param allocator Allocator
    * @param image_memory_creator Image memory binder/allocator
    */
   swapchain_image(VkImage image_handle, VkSemaphore present_semaphore, VkSemaphore present_fence_wait_semaphore,
                   util::unique_ptr<fence_sync> present_fence, bool wait_on_present_fence,
                   const layer::device_private_data *device_data, util::allocator allocator,
                   util::unique_ptr<image_backing_memory> image_memory)
      : m_present_semaphore(present_semaphore)
      , m_present_fence_wait_semaphore(present_fence_wait_semaphore)
      , m_present_fence(std::move(present_fence))
      , m_wait_on_present_fence(wait_on_present_fence)
      , m_image(image_handle)
      , m_status(swapchain_image::UNALLOCATED)
      , m_allocator(allocator)
      , m_device_data(device_data)
      , m_image_memory(std::move(image_memory))
      , m_data(nullptr)
   {
      assert(m_image_memory != nullptr);
   };

   struct create_args
   {
      /**
       * @brief Arguments that describe how the swapchain image will be created
       *
       * @param device_data Device data
       * @param allocator Allocator
       * @param image_handle Vulkan image handle that will be associated with this swapchain image
       * @param image_memory Swapchain image backing memory
       * @param exportable_fence Whether swapchain image needs an exportable fence
       * @param wait_on_present_fence Whether swapchain image should wait on the exportable fence
       *                              when asked to wait on present.
       */
      create_args(layer::device_private_data *device_data, util::allocator allocator, VkImage image_handle,
                  util::unique_ptr<image_backing_memory> image_memory, bool exportable_fence,
                  bool wait_on_present_fence)
         : m_device_data(device_data)
         , m_allocator(allocator)
         , m_image_handle(image_handle)
         , m_backing_memory(std::move(image_memory))
         , m_exportable_fence(exportable_fence)
         , m_wait_on_present_fence(wait_on_present_fence)
      {
      }

      layer::device_private_data *m_device_data;
      const util::allocator m_allocator;
      VkImage m_image_handle;
      util::unique_ptr<image_backing_memory> m_backing_memory;
      bool m_exportable_fence;
      bool m_wait_on_present_fence;
   };

   /**
    * @brief Create a swapchain image.
    *
    * @param create_args Arguments that describe how to create the swapchain image.
    * @return VkResult on failure or the created swapchain image on success.
    */
   static std::variant<VkResult, swapchain_image> create(create_args &create_args);

   /**
    * @brief Get the image backing memory object
    *
    * @tparam T The type of the backing memory object
    * @return The image backing memory object
    */
   template <typename T>
   T *get_backing_memory()
   {
      return static_cast<T *>(m_image_memory.get());
   }

   /**
    * @brief Method to release a swapchain image and its resources
    */
   void destroy();

   VkSemaphore m_present_semaphore;
   VkSemaphore m_present_fence_wait_semaphore;
   util::unique_ptr<fence_sync> m_present_fence;
   bool m_wait_on_present_fence;
   VkImage m_image;
   status m_status;

   util::allocator m_allocator;
   const layer::device_private_data *m_device_data;

   util::unique_ptr<image_backing_memory> m_image_memory;
   util::unique_ptr<swapchain_image_data> m_data;
};

/**
 * @brief Base class describing swapchain image backing memory creator
 */
class image_backing_memory_creator
{
public:
   virtual ~image_backing_memory_creator() = default;

   /**
    * @brief Create a image backing memory object
    *
    * @param allocator Allocator to use for creating the backing memory object
    * @return Image backing memory object or nullptr on failure
    */
   virtual util::unique_ptr<image_backing_memory> create_image_backing_memory(util::allocator &allocator) = 0;
};

}