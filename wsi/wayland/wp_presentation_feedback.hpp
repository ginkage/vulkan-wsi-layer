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
#include "wl_object_owner.hpp"

namespace wsi
{
namespace wayland
{

class wsi_ext_present_id_wayland;

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
   presentation_feedback(uint64_t present_id, struct wp_presentation_feedback *feedback,
                         wsi_ext_present_id_wayland *ext)
      : m_present_id(present_id)
      , m_feedback(feedback)
      , m_ext(ext)
   {
   }

   ~presentation_feedback() = default;

   presentation_feedback(const presentation_feedback &feedback_obj) = delete;
   presentation_feedback &operator=(const presentation_feedback &feedback_obj) = delete;

   presentation_feedback(presentation_feedback &&feedback_obj)
      : m_present_id(feedback_obj.m_present_id)
      , m_feedback(std::move(feedback_obj.m_feedback))
      , m_ext(feedback_obj.m_ext)
   {
      feedback_obj.reset();
   }

   presentation_feedback &operator=(presentation_feedback &&feedback_obj)
   {
      if (this == &feedback_obj)
      {
         return *this;
      }
      if (m_feedback != nullptr)
      {
         wp_presentation_feedback_destroy(m_feedback.get());
      }
      m_present_id = feedback_obj.m_present_id;
      m_feedback = std::move(feedback_obj.m_feedback);
      m_ext = feedback_obj.m_ext;
      feedback_obj.reset();
   }

   void reset()
   {
      m_present_id = 0;
      m_feedback = nullptr;
      m_ext = nullptr;
   }

   uint64_t present_id()
   {
      return m_present_id;
   }

   struct wp_presentation_feedback *feedback()
   {
      return m_feedback.get();
   }

   wsi_ext_present_id_wayland *ext()
   {
      return m_ext;
   }

private:
   uint64_t m_present_id;
   wayland_owner<struct wp_presentation_feedback> m_feedback;
   wsi_ext_present_id_wayland *m_ext;
};

} // namespace wayland
} // namespace wsi
