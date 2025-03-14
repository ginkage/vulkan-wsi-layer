/*
 * Copyright (c) 2017-2025 Arm Limited.
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
 * @file swapchain_base.hpp
 *
 * @brief Contains the class definition for a base swapchain.
 */

#pragma once

#include <pthread.h>
#include <semaphore.h>
#include <vulkan/vulkan.h>
#include <thread>
#include <array>

#include <util/custom_allocator.hpp>
#include <util/helpers.hpp>
#include <util/ring_buffer.hpp>
#include <util/timed_semaphore.hpp>
#include <util/log.hpp>
#include <layer/private_data.hpp>

#include "surface_properties.hpp"
#include "synchronization.hpp"

#include "extensions/frame_boundary.hpp"
#include "extensions/wsi_extension.hpp"
#include "util/macros.hpp"

namespace wsi
{

using util::MAX_PLANES;
struct swapchain_image
{
   enum status
   {
      INVALID,
      ACQUIRED,
      PENDING,
      PRESENTED,
      FREE,
      UNALLOCATED,
   };

   /* Implementation specific data */
   void *data{ nullptr };

   VkImage image{ VK_NULL_HANDLE };
   status status{ swapchain_image::INVALID };
   VkSemaphore present_semaphore{ VK_NULL_HANDLE };
   VkSemaphore present_fence_wait{ VK_NULL_HANDLE };
};

struct pending_present_request
{
   /* The index of the pending image to use for present. */
   uint32_t image_index;

   /**
    * Present ID assigned to the present submission.
    * If 0, no present ID has been assigned to this request.
    */
   uint64_t present_id;
};

struct swapchain_presentation_parameters
{
   /* Fence supplied by the application with VkSwapchainPresentFenceInfoEXT. */
   VkFence present_fence{ VK_NULL_HANDLE };

   /* Whether the swapchain needs to switch to a different presentation mode. */
   VkBool32 switch_presentation_mode{ false };

   /* The presentation mode to switch to. */
   VkPresentModeKHR present_mode;

   /*
    * Flag that indicates whether the presentation
    * request will wait on the image's present_semaphore
    * and not the semaphores that come with
    * present_info.
    */
   VkBool32 use_image_present_semaphore{ true };

   /* Contains details about the pending present request */
   pending_present_request pending_present{};

   /**
    * Flag that indicates whether a frame boundary should be passed
    * to underlying layers/ICD if the feature is enabled.
    */
   VkBool32 handle_present_frame_boundary_event{ true };

#if VULKAN_WSI_LAYER_EXPERIMENTAL
   /**
    * The present timing info.
    */
   VkPresentTimingInfoEXT m_present_timing_info;
#endif
};

/**
 * @brief Base swapchain class
 *
 * - the swapchain implementations inherit from this class.
 * - the VkSwapchain will hold a pointer to this class.
 * - much of the swapchain implementation is done by this class, as the only things needed
 *   in the implementation are how to create a presentable image and how to present an image.
 */
class swapchain_base
{
public:
   swapchain_base(layer::device_private_data &dev_data, const VkAllocationCallbacks *allocator);

   virtual ~swapchain_base()
   {
      /* nop */
   }

   /**
    * @brief Create swapchain.
    *
    * Perform all swapchain initialization, create presentable images etc.
    */
   VkResult init(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info);

   /**
    * @brief Acquires a free image.
    *
    * Current implementation blocks until a free image is available.
    *
    * @param timeout Unused since we block until a free image is available.
    *
    * @param semaphore A semaphore signaled once an image is acquired.
    *
    * @param fence A fence signaled once an image is acquired.
    *
    * @param pImageIndex The index of the acquired image.
    *
    * @return VK_SUCCESS on completion.
    */
   VkResult acquire_next_image(uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *image_index);

   /**
    * @brief Gets the number of swapchain images or a number of at most
    * m_num_swapchain_images images.
    *
    * @param pSwapchainImageCount Used to return number of images in
    * the swapchain if second parameter is nullptr or represents the
    * number of images to be returned in second parameter.
    *
    * @param pSwapchainImage Array of VkImage handles.
    *
    * @return If number of requested images is less than the number of available
    * images in the swapchain returns VK_INCOMPLETE otherwise VK_SUCCESS.
    */
   VkResult get_swapchain_images(uint32_t *swapchain_image_count, VkImage *swapchain_image);

   /**
    * @brief Submits a present request for the supplied image.
    *
    * @param queue The queue to which the submission will be made to.
    *
    * @param present_info Information about the swapchain and image to be presented.
    *
    * @param presentation_parameters Presentation parameters.
    *
    * @return If queue submission fails returns error of vkQueueSubmit, if the
    * swapchain has a descendant who started presenting returns VK_ERROR_OUT_OF_DATE_KHR,
    * otherwise returns VK_SUCCESS.
    */
   VkResult queue_present(VkQueue queue, const VkPresentInfoKHR *present_info,
                          const swapchain_presentation_parameters &presentation_parameters);

   /**
    * @brief Get the allocator
    *
    * @return const util::allocator The allocator used in the swapchain
    */
   const util::allocator &get_allocator() const
   {
      return m_allocator;
   }

   /**
    * @brief Creates a VkImage handle.
    *
    * It is used to bind images to memory from the swapchain. It is called if a
    * VkImageSwapchainCreateInfoKHR struct has been provided in the vkCreateImage
    * function. All images created by the swapchain will use the same VkImageCreateInfo,
    * initialized in create_and_bind_swapchain_image().
    *
    * @param[out] image             Handle to the image.
    *
    * @return If image creation is successful returns VK_SUCCESS, otherwise
    * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_OUT_OF_HOST_MEMORY
    * depending on the error that occured.
    */
   VkResult create_aliased_image_handle(VkImage *image);

   /**
    * @brief Bind image to a swapchain
    *
    * It is used to bind images to memory from the swapchain. It is called if a
    * VkBindImageMemorySwapchainInfoKHR struct has been provided in the vkBindImageMemory2
    * function.
    *
    * @param device              is the logical device that owns the images and memory.
    * @param bind_image_mem_info details the image we want to bind.
    * @param bind_sc_info        describes the swapchain memory to bind to.
    *
    * @return VK_SUCCESS on success, otherwise on failure VK_ERROR_OUT_OF_HOST_MEMORY or VK_ERROR_OUT_OF_DEVICE_MEMORY
    * can be returned.
    */
   virtual VkResult bind_swapchain_image(VkDevice &device, const VkBindImageMemoryInfo *bind_image_mem_info,
                                         const VkBindImageMemorySwapchainInfoKHR *bind_sc_info) = 0;

   /**
    * @brief Get image's present semaphore
    *
    * @param image_index Image's index
    *
    * @return the image's present_semaphore
    */
   VkSemaphore get_image_present_semaphore(uint32_t image_index)
   {
      return m_swapchain_images[image_index].present_semaphore;
   }

   /**
    * @brief Get the swapchain status.
    *
    * @return VK_SUCCESS
    */
   VkResult get_swapchain_status();

   /**
    * @brief Release all images not belonging to the device
    * by making them available to be acquired again
    *
    * @param image_count Amount of images in the indices array
    * @param indices Array of image indices
    */
   void release_images(uint32_t image_count, const uint32_t *indices);

   /**
    * @brief Check if bind is allowed for a swapchain image.
    *
    * @param image_index The image's index.
    *
    * @return VK_SUCCESS on success, an error code otherwise.
    */
   VkResult is_bind_allowed(uint32_t image_index) const;

   /**
    * @brief Get the swapchain extension's pointer.
    *
    * @return Returns the pointer to the swapchain extensions of type T.
    */
   template <typename T>
   T *get_swapchain_extension(bool required_to_be_present = false)
   {
      auto ext = m_extensions.get_extension<T>();
      if (!ext && required_to_be_present)
      {
         WSI_LOG_ERROR("Extension required (%s) but missing.", typeid(T).name());
         assert(false && "Extension required but missing");
      }

      return ext;
   }

   /**
    * @brief Add a swapchain extension to the list of available swapchain extensions.
    *
    * @param extension unique_ptr to the extension to be added.
    *
    * @return Returns true on success and false otherwise.
    */

   bool add_swapchain_extension(util::unique_ptr<wsi_ext> extension);

protected:
   layer::device_private_data &m_device_data;

   /**
    * @brief Handle to the page flip thread.
    */
   std::thread m_page_flip_thread;

   /**
    * @brief Whether the page flip thread has to continue running or terminate.
    */
   bool m_page_flip_thread_run;

   /**
    * @brief A semaphore to be signalled once a page flip event occurs.
    */
   util::timed_semaphore m_page_flip_semaphore;

   /**
    * @brief A semaphore to be signalled once the swapchain has one frame on screen.
    */
   sem_t m_start_present_semaphore;

   /**
    * @brief A mutex to protect access to the statuses of the swapchain's images and
    * any code paths that rely on this information. We use a recursive mutex as some
    * functions such as 'destroy_image' both change an image's status and are called
    * conditionally based on an image's status in some cases. A recursive mutex allows
    * these functions to be called both with and without the mutex already locked in the
    * same thread.
    */
   std::recursive_mutex m_image_status_mutex;

   /**
    * @brief Defines if the pthread_t and sem_t members of the class are defined.
    *
    * As they are opaque types theer's no known invalid value that we ca initialize to,
    * and therefore determine if we need to cleanup.
    */
   bool m_thread_sem_defined;

   /**
    * @brief A flag to track if it is the first present for the chain.
    */
   bool m_first_present;

   /**
    * @brief In order to present the images in a FIFO order we implement
    * a ring buffer to hold the images queued for presentation. Since the
    * two pointers (head and tail) are used by different
    * threads and we do not allow the application to acquire more images
    * than we have we eliminate race conditions.
    */
   util::ring_buffer<pending_present_request, wsi::surface_properties::MAX_SWAPCHAIN_IMAGE_COUNT> m_pending_buffer_pool;

   /**
    * @brief User provided memory allocation callbacks.
    */
   const util::allocator m_allocator;

   /**
    * @brief Vector of images in the swapchain.
    */
   util::vector<swapchain_image> m_swapchain_images;

   /**
    * @brief Handle to the surface object this swapchain will present images to.
    */
   VkSurfaceKHR m_surface;

   /**
    * @brief Present mode currently being used for this swapchain
    */
   VkPresentModeKHR m_present_mode;

   /**
    * @brief Possible presentation modes this swapchain is allowed to present with VkSwapchainPresentModesCreateInfoEXT
    */
   util::vector<VkPresentModeKHR> m_present_modes;

   /**
    * @brief Descendant of this swapchain.
    * Used to check whether or not a descendant of this swapchain has started
    * presenting images to the surface already. If it has, any calls to queuePresent
    * for this swapchain will return VK_ERROR_OUT_OF_DATE_KHR.
    */
   VkSwapchainKHR m_descendant;

   /**
    * @brief Ancestor of this swapchain.
    * Used to check whether the ancestor swapchain has completed all of its
    * pending page flips (this is required before this swapchain presents for the
    * first time.
    */
   VkSwapchainKHR m_ancestor;

   /**
    *  @brief Handle to the logical device the swapchain is created for.
    */
   VkDevice m_device;

   /**
    *  @brief Handle to the queue used for signalling submissions
    */
   VkQueue m_queue;

   /**
    * @brief Image creation info used for all swapchain images.
    */
   VkImageCreateInfo m_image_create_info;

   /**
    * @brief Return the VkAllocationCallbacks passed in this object constructor.
    */
   const VkAllocationCallbacks *get_allocation_callbacks()
   {
      return m_allocator.get_original_callbacks();
   }

   /**
    * @brief Method to wait on all pending buffers to be displayed.
    */
   void wait_for_pending_buffers();

   /**
    * @brief Remove cached ancestor.
    */
   void clear_ancestor();

   /**
    * @brief Remove cached descendant.
    */
   void clear_descendant();

   /**
    * @brief Deprecate this swapchain.
    *
    * If an application replaces an old swapchain with a new one, the older swapchain
    * needs to be deprecated. This method releases all the FREE images and sets the
    * descendant of the swapchain. We do not need to care about images in other states
    * at this point since they will be released by the page flip thread.
    *
    * @param descendant Handle to the descendant swapchain.
    */
   void deprecate(VkSwapchainKHR descendant);

   /**
    * @brief Platform specific initialization
    *
    * @param      device                  VkDevice object.
    * @param      swapchain_create_info   Pointer to the swapchain create info struct.
    * @param[out] use_presentation_thread Flag indicating if image presentation
    *                                     must happen in a separate thread.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   virtual VkResult init_platform(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info,
                                  bool &use_presentation_thread) = 0;

   /**
    * @brief Adds required extensions to the extension list of the swapchain
    *
    * @param device Vulkan device
    * @param swapchain_create_info Swapchain create info
    * @return VK_SUCCESS on success, other result codes on failure
    */
   virtual VkResult add_required_extensions(VkDevice device, const VkSwapchainCreateInfoKHR *swapchain_create_info)
   {
      UNUSED(device);
      UNUSED(swapchain_create_info);

      /* No extensions required by base class implementation */
      return VK_SUCCESS;
   }

   /**
    * @brief Base swapchain teardown.
    *
    * Even though the inheritance gives us a nice way to defer display specific allocation
    * and presentation outside of the base class, it however robs the children classes - which
    * also happen to do some of their state setting - the oppurtunity to do the last clean up
    * call, as the base class' destructor is called at the end. This method provides a way to do it.
    * The destructor is a virtual function and much of the swapchain teardown happens in this method
    * which gets called from the child's destructor.
    */
   void teardown();

   /**
    * @brief Allocates and binds a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    * @param image             Handle to the image.
    *
    * @return Returns VK_SUCCESS on success, otherwise an appropriate error code.
    */
   virtual VkResult allocate_and_bind_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image) = 0;

   /**
    * @brief Creates a new swapchain image.
    *
    * @param image_create_info Data to be used to create the image.
    * @param image             Handle to the image.
    *
    * @return If image creation is successful returns VK_SUCCESS, otherwise
    * will return VK_ERROR_OUT_OF_DEVICE_MEMORY or VK_ERROR_INITIALIZATION_FAILED
    * depending on the error that occurred.
    */
   virtual VkResult create_swapchain_image(VkImageCreateInfo image_create_info, swapchain_image &image) = 0;

   /**
    * @brief Method to present and image
    *
    * It sends the next image for presentation to the presentation engine.
    *
    * @param pending_present Information on the pending present request.
    */
   virtual void present_image(const pending_present_request &pending_present) = 0;

   /**
    * @brief Transition a presented image to free.
    *
    * Called by swapchain implementation when a new image has been presented.
    *
    * @param presented_index Index of the image to be marked as free.
    */
   void unpresent_image(uint32_t presented_index);

   /**
    * @brief Method to release a swapchain image
    *
    * Releases resources stored in the data member of a swapchain_image.
    *
    * @param image Handle to the image about to be released.
    */
   virtual void destroy_image([[maybe_unused]] swapchain_image &image){};

   /**
    * @brief Hook for any actions to free up a buffer for acquire
    *
    * If specific actions are required by the windowing system to query whether a buffer
    * is still used by it, this function should be implemented by the WSI backend's
    * swapchain implementation.
    *
    * @param[in,out] timeout time to wait, in nanoseconds. 0 doesn't block,
    *                        UINT64_MAX waits indefinately. The timeout should
    *                        be updated if a sleep is required - this can
    *                        be set to 0 if the semaphore is now not expected
    *                        block.
    */
   virtual VkResult get_free_buffer(uint64_t *timeout)
   {
      UNUSED(timeout);
      return VK_SUCCESS;
   }

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
   virtual VkResult image_set_present_payload(swapchain_image &image, VkQueue queue,
                                              const queue_submit_semaphores &semaphores,
                                              const void *submission_pnext = nullptr) = 0;

   /**
    * @brief Waits for the present payload of an image if necessary.
    *
    * If the page flip thread needs to wait for the image present synchronization payload the WSI implemention can block
    * and wait in this call. Otherwise the function should return successfully without blocking.
    *
    * @param[in] image   The swapchain image for which the function may need to wait until the presentat payload has
    *                    finished.
    * @param     timeout Timeout for any wait in nanoseconds.
    *
    * @return VK_SUCCESS if waiting was successful or unnecessary. An error code otherwise.
    */
   virtual VkResult image_wait_present(swapchain_image &image, uint64_t timeout) = 0;

   /**
    * @brief Returns true if an error has occurred.
    */
   bool error_has_occured() const
   {
      return m_error_state != VK_SUCCESS;
   }

   VkResult get_error_state() const
   {
      return m_error_state;
   }

   /*
    * @brief Set the error state.
    *
    * The error state should be set when a failure that should be communicated
    * to the application occurs during the page flipping thread.
    *
    * @param state Error code to be returned from acquire_next_image.
    */
   void set_error_state(VkResult state)
   {
      m_error_state = state;
   }

private:
   std::mutex m_image_acquire_lock;
   /**
    * @brief In case we encounter threading or drm errors we need a way to
    * notify the user of the failure. While no error has occurred its value
    * is VK_SUCCESS. When an error occurs, its value is set to the
    * appropriate error code and it is returned to the user through the next
    * acquire_next_image call.
    */
   VkResult m_error_state;

   /**
    * @brief Wait for a buffer to become free.
    */
   VkResult wait_for_free_buffer(uint64_t timeout);

   /**
    * @brief A semaphore to be signalled once a free image becomes available.
    *
    * Uses a custom semaphore implementation that uses a condition variable.
    * it is slower, but has a safe timedwait implementation.
    *
    * This is kept private as waiting should be done via wait_for_free_buffer().
    */
   util::timed_semaphore m_free_image_semaphore;

   /**
    * @brief Per swapchain thread function that handles page flipping.
    *
    * This thread should be running for the lifetime of the swapchain.
    * The thread simply calls the implementation's present_image() method.
    * There are 3 main cases we cover here:
    *
    * 1. On the first present of the swapchain if the swapchain has
    *    an ancestor we must wait for it to finish presenting.
    * 2. The normal use case where we do page flipping, in this
    *    case change the currently PRESENTED image with the oldest
    *    PENDING image.
    * 3. If the enqueued image is marked as FREE it means the
    *    descendant of the swapchain has started presenting so we
    *    should release the image and continue.
    *
    * The function always waits on the page_flip_semaphore of the
    * swapchain. Once it passes that we must wait for the fence of the
    * oldest pending image to be signalled, this means that the gpu has
    * finished rendering to it and we can present it. From there on the
    * logic splits into the above 3 cases and if an image has been
    * presented then the old one is marked as FREE and the free_image
    * semaphore of the swapchain will be posted.
    **/
   void page_flip_thread();

   /**
    * @brief Call the swapchain implementation specific present_image function.
    *
    * In addition to calling the present_image function it also handles the
    * communication with the ancestor before the first presentation.
    *
    * @param pending_present_request Submission information for the present request.
    */
   void call_present(const pending_present_request &pending_present);

   /**
    * @brief Return true if the descendant has started presenting.
    */
   bool has_descendant_started_presenting();

   /**
    * @brief Initialize the page flipping thread.
    *
    * @return VK_SUCCESS if the initialization was successful or an error code otherwise.
    */
   VkResult init_page_flip_thread();

   /**
    * @brief Notify the presentation engine with the next image to be presented.
    *
    * Appends the next image to the ring buffer and notifies the page flipping
    * thread if it is enabled or directly calls the WSI backend implementation to
    * present the image.
    *
    * @param pending_present_request Submission information for the present request.
    *
    * @return VK_SUCCESS on success or an error code otherwise.
    */
   VkResult notify_presentation_engine(const pending_present_request &submit_info);

   /**
    * @brief A flag to track if swapchain has started presenting.
    */
   bool m_started_presenting;

   /**
    * @brief Holds the swapchain extensions and related functionalities.
    */
   wsi_ext_maintainer m_extensions;
};

} /* namespace wsi */
