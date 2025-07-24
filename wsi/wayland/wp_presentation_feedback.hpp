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

/** @file
 * @brief Contains functions for handling the wp_presentation_feedback Wayland protocol
 */

#pragma once

#include <vulkan/vulkan.h>
#include <presentation-time-client-protocol.h>
#include <wsi/extensions/present_timing.hpp>
#include "wl_object_owner.hpp"

namespace wsi
{
namespace wayland
{

class wsi_ext_present_id_wayland;
class wsi_ext_present_timing_wayland;

/**
 * @brief Registers the listeners for wp_presentation_feedback
 * @param wp_presentation_feedback - wp_presentation_feedback interface
 * @param data                     - Data to pass to the callbacks
 * @return VK_SUCCESS on success, error otherwise.
 */
VkResult register_wp_presentation_feedback_listener(struct wp_presentation_feedback *wp_presentation_feedback,
                                                    void *data);

/**
 * @brief Class to hold a presentation feedback and associated attributes.
 */
class presentation_feedback
{
public:
   presentation_feedback(struct wp_presentation_feedback *feedback, wsi_ext_present_timing_wayland *ext_present_timing,
                         uint32_t image_index)
      : m_feedback(feedback)
      , m_ext_present_id(nullptr)
      , m_present_id(0)
      , m_ext_present_timing(ext_present_timing)
      , m_image_index(image_index)
   {
   }
   presentation_feedback(struct wp_presentation_feedback *feedback, wsi_ext_present_id_wayland *ext_present_id,
                         uint64_t present_id)
      : m_feedback(feedback)
      , m_ext_present_id(ext_present_id)
      , m_present_id(present_id)
      , m_ext_present_timing(nullptr)
      , m_image_index(0)
   {
   }

   ~presentation_feedback() = default;

   presentation_feedback(const presentation_feedback &feedback_obj) = delete;
   presentation_feedback &operator=(const presentation_feedback &feedback_obj) = delete;

   presentation_feedback(presentation_feedback &&feedback_obj)
      : m_feedback(std::move(feedback_obj.m_feedback))
      , m_ext_present_id(feedback_obj.m_ext_present_id)
      , m_present_id(feedback_obj.m_present_id)
      , m_ext_present_timing(feedback_obj.m_ext_present_timing)
      , m_image_index(feedback_obj.m_image_index)
   {
      feedback_obj.reset();
   }

   presentation_feedback &operator=(presentation_feedback &&feedback_obj)
   {
      if (this != &feedback_obj)
      {
         if (m_feedback != nullptr)
         {
            wp_presentation_feedback_destroy(m_feedback.get());
         }
         m_present_id = feedback_obj.m_present_id;
         m_feedback = std::move(feedback_obj.m_feedback);
         m_ext_present_id = feedback_obj.m_ext_present_id;
         m_ext_present_timing = feedback_obj.m_ext_present_timing;
         m_image_index = feedback_obj.m_image_index;
         feedback_obj.reset();
      }
      return *this;
   }

   void reset()
   {
      m_feedback = nullptr;
      m_ext_present_id = nullptr;
      m_present_id = 0;
      m_ext_present_timing = nullptr;
      m_image_index = 0;
   }

   uint64_t get_present_id() const
   {
      return m_present_id;
   }

   uint32_t get_image_index() const
   {
      return m_image_index;
   }

   struct wp_presentation_feedback *feedback()
   {
      return m_feedback.get();
   }

   wsi_ext_present_id_wayland *ext_present_id()
   {
      return m_ext_present_id;
   }

   wsi_ext_present_timing_wayland *ext_present_timing()
   {
      return m_ext_present_timing;
   }

private:
   wayland_owner<struct wp_presentation_feedback> m_feedback;
   wsi_ext_present_id_wayland *m_ext_present_id;
   uint64_t m_present_id;
   wsi_ext_present_timing_wayland *m_ext_present_timing;
   uint32_t m_image_index;
};

} // namespace wayland
} // namespace wsi
