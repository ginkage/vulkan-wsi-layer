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
#include "present_timing_handler.hpp"

namespace wsi
{
namespace wayland
{

VWL_CAPI_CALL(void)
wp_presentation_feedback_sync_output(void *, struct wp_presentation_feedback *, struct wl_output *)
{
   /* Not relevant */
}

VWL_CAPI_CALL(void)
wp_presentation_feedback_presented(void *data, struct wp_presentation_feedback *, uint32_t tv_sec_hi,
                                   uint32_t tv_sec_lo, uint32_t tv_nsec, uint32_t, uint32_t, uint32_t, uint32_t)
{
   auto feedback_obj = reinterpret_cast<wsi::wayland::presentation_feedback *>(data);
   if (feedback_obj->ext_present_timing() != nullptr)
   {
      uint64_t timestamp_seconds = (static_cast<uint64_t>(tv_sec_hi) << 32) | tv_sec_lo;
      double timestamp_limit = static_cast<double>(UINT64_MAX - tv_nsec) / 1e9;
      if (static_cast<double>(timestamp_seconds) > timestamp_limit)
      {
         WSI_LOG_ERROR("timestamp_seconds overflow, capping to safe maximum");
         timestamp_seconds = static_cast<uint64_t>(std::floor(timestamp_limit));
      }
      uint64_t timestamp_ns = static_cast<uint64_t>(timestamp_seconds * 1e9) + tv_nsec;
      feedback_obj->ext_present_timing()->pixelout_callback(feedback_obj->get_image_index(), timestamp_ns);
      feedback_obj->ext_present_timing()->remove_from_pending_present_feedback_list(feedback_obj->get_image_index());
   }
   else if (feedback_obj->ext_present_id() != nullptr)
   {
      feedback_obj->ext_present_id()->mark_delivered(feedback_obj->get_present_id());
      feedback_obj->ext_present_id()->remove_from_pending_present_feedback_list(feedback_obj->get_present_id());
   }
}

VWL_CAPI_CALL(void)
wp_presentation_feedback_discarded(void *data, struct wp_presentation_feedback *)
{
   /* If the presentation request has been discarded, we still want to notify that the image has reached the compositor
    * as otherwise, any functions waiting on the present ID will never be notified. There is nothing more we can do
    * with this request as it has been discarded. */
   auto feedback_obj = reinterpret_cast<wsi::wayland::presentation_feedback *>(data);
   if (feedback_obj->ext_present_id() != nullptr)
   {
      feedback_obj->ext_present_id()->mark_delivered(feedback_obj->get_present_id());
      feedback_obj->ext_present_id()->remove_from_pending_present_feedback_list(feedback_obj->get_present_id());
   }
   if (feedback_obj->ext_present_timing() != nullptr)
   {
      feedback_obj->ext_present_timing()->pixelout_callback(feedback_obj->get_image_index(), 0);
      feedback_obj->ext_present_timing()->remove_from_pending_present_feedback_list(feedback_obj->get_image_index());
   }
}

static const wp_presentation_feedback_listener presentation_listener = {
   .sync_output = wp_presentation_feedback_sync_output,
   .presented = wp_presentation_feedback_presented,
   .discarded = wp_presentation_feedback_discarded,
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
