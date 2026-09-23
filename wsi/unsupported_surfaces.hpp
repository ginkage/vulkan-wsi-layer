/*
 * Copyright (c) 2024-2026 Arm Limited.
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

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan.h>
#include <array>
/* Define the patch version directly as macros */
#define WSI_LAYER_VK_PATCH 356

/* Convert macros to string */
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#if VK_HEADER_VERSION > WSI_LAYER_VK_PATCH
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#pragma-messages"
#endif
#pragma message("The Vulkan header version is newer than the currently supported version.")
#pragma message("Current Vulkan header version: " TOSTRING(VK_HEADER_VERSION))
#pragma message("Supported Vulkan API version: " TOSTRING(WSI_LAYER_VK_PATCH))
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif

namespace wsi
{
/* A list of platform-specific unsupported surface extensions
   Not using the extension macros and symbols due to missing definitions for native platform symbols. */
static constexpr std::array unsupported_surfaces_ext_array = {
   "VK_KHR_win32_surface",    "VK_EXT_metal_surface", "VK_KHR_android_surface",
#if !BUILD_WSI_X11
   "VK_KHR_xcb_surface",      "VK_KHR_xlib_surface",
#endif
#if !BUILD_WSI_HEADLESS
   "VK_EXT_headless_surface",
#endif
#if !BUILD_WSI_WAYLAND
   "VK_KHR_wayland_surface",
#endif
   /* VK_KHR_display is deliberately not listed when the display backend is not built. The Mali ICD offers
    * it, so applications that present to X11 or Wayland often enable it too, and listing it would withdraw
    * swapchain maintenance1 and present wait from them. The trade-off: an application presenting through
    * the ICD's own display swapchain may now use those features on a swapchain the layer passes through,
    * which the ICD does not implement. That needs DRM master, so never happens under a compositor. */
};

} // namespace wsi
