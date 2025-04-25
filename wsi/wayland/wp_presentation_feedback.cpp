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

#include "wp_presentation_feedback.hpp"
#include "present_id_wayland.hpp"

namespace wsi
{
namespace wayland
{

VWL_CAPI_CALL(void)
wp_presentation_feedback_presented(void *data, struct wp_presentation_feedback *, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, uint32_t, uint32_t)
{
   auto feedback_obj = reinterpret_cast<wsi::wayland::presentation_feedback *>(data);
   if (feedback_obj->ext() != nullptr)
   {
      feedback_obj->ext()->mark_delivered(feedback_obj->present_id());
      feedback_obj->ext()->remove_from_pending_present_feedback_list(feedback_obj->present_id());
   }
}

static const wp_presentation_feedback_listener presentation_listener = {
   .sync_output = NULL,
   .presented = wp_presentation_feedback_presented,
   .discarded = NULL,
};

VkResult register_wp_presentation_feedback_listener(struct wp_presentation_feedback *wp_presentation_feedback,
                                                    void *data)
{
   int res = wp_presentation_feedback_add_listener(wp_presentation_feedback, &presentation_listener, data);
   if (res < 0)
   {
      WSI_LOG_ERROR("Failed to add wp_presentation_feedback listener.");
      return VK_ERROR_INITIALIZATION_FAILED;
   }
   return VK_SUCCESS;
}

} // namespace wayland
} // namespace wsi
