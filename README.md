# Vulkan® Window System Integration Layer

## Introduction

This project is a Vulkan® layer which implements some of the Vulkan® window system
integration extensions such as `VK_KHR_swapchain`. The layer is designed to be
GPU vendor agnostic when used as part of the Vulkan® ICD/loader architecture.

Our vision for the project is to become the de facto implementation for Vulkan®
window system integration extensions so that they need not be implemented in the
ICD; instead, the implementation of these extensions are shared across vendors
for mutual benefit.

The project currently implements support for `VK_EXT_headless_surface` and
its dependencies. Experimental support for `VK_KHR_wayland_surface` can be
enabled via a build option [as explained below](#building-with-wayland-support).
Support for X11 (`VK_KHR_xcb_surface` and `VK_KHR_xlib_surface`), presenting via
DRI3 + Present or MIT-SHM, can likewise be enabled — see
[Building with X11 support](#building-with-x11-support).

### Implemented Vulkan® extensions

The Vulkan® WSI Layer in addition to the window system integration extensions
implements the following extensions:
* Instance extensions
  * VK_KHR_get_surface_capabilities2
  * VK_EXT_surface_maintenance1
  * VK_KHR_surface_maintenance1
* Device extensions
  * VK_KHR_shared_presentable_image
  * VK_EXT_image_compression_control_swapchain
  * VK_KHR_present_id
  * VK_KHR_present_id2
  * VK_KHR_present_wait
  * VK_KHR_present_wait2
  * VK_EXT_swapchain_maintenance1
  * VK_KHR_swapchain_maintenance1
  * VK_EXT_present_mode_fifo_latest_ready (For Headless and Wayland only)
  * VK_KHR_present_mode_fifo_latest_ready (For Headless and Wayland only)
  * VK_KHR_swapchain_mutable_format (For Headless and Wayland only)
  * VK_EXT_present_timing (For Headless and Wayland only)

## Building

### Dependencies

* [CMake](https://cmake.org) version 3.4.3 or above.
* C++17 compiler.
* Vulkan® loader and associated headers with support for the
  `VK_EXT_headless_surface` extension and for the Vulkan 1.1, or later API.

The Vulkan WSI Layer uses Vulkan extensions to communicate with the Vulkan ICDs.
The ICDs installed in the system are required to support the following extensions:
* Instance extensions:
  * VK_KHR_get_physical_device_properties_2
  * VK_KHR_external_fence_capabilities
  * VK_KHR_external_semaphore_capabilities
  * VK_KHR_external_memory_capabilities
* Device extensions (required when Headless/Wayland support is enabled):
  * VK_KHR_calibrated_timestamps
* Device extensions (only required when Wayland support is enabled):
  * VK_EXT_image_drm_format_modifier
  * VK_KHR_image_format_list
  * VK_EXT_external_memory_dma_buf
  * VK_KHR_external_memory_fd
  * VK_KHR_external_fence_fd
* Any dependencies of the above extensions

### Vulkan Header Version

The Vulkan WSI Layer has been validated against Vulkan header version 1.4.325.

If you are using a Vulkan header version newer than this, a warning will appear during compilation.

### Building the Vulkan® loader

This step is not necessary if your system already has a loader and associated
headers with support for the `VK_EXT_headless_surface` extension. We include
these instructions for completeness.

```
git clone https://github.com/KhronosGroup/Vulkan-Loader.git
mkdir Vulkan-Loader/build
cd Vulkan-Loader/build
../scripts/update_deps.py
cmake -C helper.cmake ..
make
make install
```

### Building with headless support

The layer requires a version of the loader and headers that includes support for
the `VK_EXT_headless_surface` extension. By default, the build system will use
the system Vulkan® headers as reported by `pkg-config`. This may be overriden by
specifying `VULKAN_CXX_INCLUDE` in the CMake configuration, for example:

```
cmake . -DVULKAN_CXX_INCLUDE="path/to/vulkan-headers"
```

If the loader and associated headers already meet the requirements of the layer
then the build steps are straightforward:

```
cmake . -Bbuild
make -C build
```

### Building with Wayland support

In order to build with Wayland support the `BUILD_WSI_WAYLAND` build option
must be used, the `SELECT_EXTERNAL_ALLOCATOR` option has to be set to
a graphics memory allocator (currently only dma_buf_heaps is supported) and
the `KERNEL_HEADER_DIR` option must be defined as the directory that includes the kernel headers.
source.

```
cmake . -DVULKAN_CXX_INCLUDE="path/to/vulkan-header" \
        -DBUILD_WSI_HEADLESS=0 \
        -DBUILD_WSI_WAYLAND=1 \
        -DSELECT_EXTERNAL_ALLOCATOR=dma_buf_heaps \
        -DKERNEL_HEADER_DIR="path/to/linux-kernel-headers"
```

In the command line above, `-DBUILD_WSI_HEADLESS=0` is used to disable support
for `VK_EXT_headless_surface`, which is otherwise enabled by default.

Note that a custom graphics memory allocator implementation can be provided
using the `EXTERNAL_WSIALLOC_LIBRARY` option. For example,

```
cmake . -DVULKAN_CXX_INCLUDE="path/to/vulkan-header" \
        -DBUILD_WSI_WAYLAND=1 \
        -DEXTERNAL_WSIALLOC_LIBRARY="path/to/custom/libwsialloc" \
        -DKERNEL_HEADER_DIR="path/to/linux-kernel-headers"
```

The `EXTERNAL_WSIALLOC_LIBRARY` option allows to specify the path to a library
containing the implementation of the graphics memory allocator API, as
described in [the wsialloc.h header file](util/wsialloc/wsialloc.h).
The allocator is not only responsible for allocating graphics buffers, but is
also responsible for selecting a suitable format that can be
efficiently shared between the different devices in the system, e.g. GPU,
display. It is therefore an important point of integration. It is expected
that each system will need a tailored implementation, although the layer
provides a generic dma_buf_heaps implementation that may work in
systems that support linear formats. This is selected by
the `-DSELECT_EXTERNAL_ALLOCATOR=dma_buf_heaps` option, as shown above.

### Wayland support with FIFO presentation mode

The WSI Layer has 2 FIFO implementations for the Wayland backend. One that
blocks in vkQueuePresent and one that uses a presentation thread. This is due
to the fact that the FIFO implementation that utilises the presentation thread
in the Wayland backend is not strictly conformant to the Vulkan specification,
however it has a much better performance due to not needing to block in vkQueuePresent.

By default, the WSI Layer uses the queue present blocking FIFO implementation
when using Wayland swapchains. This can be switched to instead use the presentation
thread implementation by including the build option `ENABLE_WAYLAND_FIFO_PRESENTATION_THREAD`,
along with the other build options mentioned in "Building with Wayland support"
section.

### Building with X11 support

X11 support (`VK_KHR_xcb_surface` and `VK_KHR_xlib_surface`) is enabled with the
`BUILD_WSI_X11` build option. Like the Wayland backend it uses a graphics memory
allocator (`SELECT_EXTERNAL_ALLOCATOR=dma_buf_heaps`), and it requires the
xcb/Xlib development packages (`libxcb`, `libxcb-shm`, `libxcb-sync`,
`libxcb-dri3`, `libxcb-present`, `libxcb-randr`, `libx11`, `libx11-xcb`,
`libxrandr`).

```
cmake . -Bbuild \
        -DVULKAN_CXX_INCLUDE="path/to/vulkan-headers" \
        -DBUILD_WSI_X11=1 \
        -DSELECT_EXTERNAL_ALLOCATOR=dma_buf_heaps \
        -DWSIALLOC_MEMORY_HEAP_NAME=system-uncached
```

The X11 backend presents through the X server (it does not page-flip directly).
At swapchain creation it picks one of two paths:

* **DRI3 + Present** — used when the X server advertises DRI3 and Present. The
  rendered dma-buf is wrapped as an X pixmap (`xcb_dri3_pixmap_from_buffers`) and
  presented with `xcb_present_pixmap`, with no CPU copy. The importable format
  modifiers are obtained from the X server via DRI3 1.2
  (`xcb_dri3_get_supported_modifiers`), so this path does not depend on the DRM
  display topology.
* **MIT-SHM** — fallback when DRI3/Present is unavailable, when DRI3 setup fails,
  or when forced with `WSI_X11_FORCE_SHM`. The image is copied into an X
  shared-memory segment and blitted.

The default present path is **paced zero-copy** (FIFO). See
[Environment variables](#environment-variables) to select the GPU-copy strategy
or to allow non-FIFO (MAILBOX) present modes.

### Building with frame instrumentation support

The layer can be built to pass frame boundary information down to other
layers or ICD by making use of the [VK_EXT_frame_boundary extension](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_frame_boundary.html).

By enabling this feature, if application is not making use of the
VK_EXT_frame_boundary extension, the layer will generate and pass down
frame boundary events which enables the ability to instrument present submissions
for applications that do not make use of this extension.

In order to enable this feature `-DENABLE_INSTRUMENTATION=1` option can
be passed at build time.

### Build with wait for present timing query results

The option `-DENABLE_WAIT_FOR_QUERY_RESULT=1` provides a way to wait for
present timing queue operations end bit timestamps to be available when
queried.

### Debug builds

The layer can be built with different values of the CMAKE_BUILD_TYPE variable.
When CMAKE_BUILD_TYPE is set to Debug, additional debugging functionality is enabled.
For example, internal values stored inside the layer's different objects can be retrieved.
These functions can be linked at runtime using dynamic loading mechanisms, such as dlsym(),
with the provided layer shared library.
The debug interface provides functions including:
 * vk_wsi_layer_debug_get_sc_image_drm_mod

## Installation

Copy the shared library `libVkLayer_window_system_integration.so` and JSON
configuration `VkLayer_window_system_integration.json` into a Vulkan®
[implicit layer directory](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md#linux-layer-discovery).

## Environment variables

The layer reads the following environment variables at runtime:

| Variable | Effect |
|----------|--------|
| `DISABLE_WSI_LAYER=1` | Disable the layer for the process. As an implicit layer it is otherwise loaded for every Vulkan application. |
| `VULKAN_WSI_DEBUG_LEVEL=<n>` | Log verbosity threshold: `1` = errors (default), `2` = + warnings, `3` = + info. |
| `WSI_X11_FORCE_SHM=1` | X11: force the MIT-SHM present path instead of DRI3 + Present. |
| `WSI_X11_DRI3_COPY=1` | X11 DRI3: present with GPU-copy (`XCB_PRESENT_OPTION_COPY`, the X server blits the pixmap) instead of the default zero-copy (`XCB_PRESENT_OPTION_NONE`). |
| `WSI_ALLOW_NON_FIFO_PRESENT_MODE=1` | Honour the application's requested present mode instead of forcing FIFO. Required to reach MAILBOX / unpaced presentation; off by default because non-FIFO modes show visual artifacts on some stacks. |
| `WSI_DISPLAY_DRI_DEV=<path>` | `display` backend only: the DRM device node to use (otherwise auto-detected). |

On X11 the out-of-the-box behaviour is **paced zero-copy**: present mode is forced
to FIFO and the strategy defaults to zero-copy. Pacing follows the present mode
(FIFO → paced, MAILBOX → unpaced) once `WSI_ALLOW_NON_FIFO_PRESENT_MODE` is set,
and the strategy is chosen with `WSI_X11_DRI3_COPY`, giving four combinations
(paced/unpaced × zero-copy/GPU-copy). Note that some present modes other than
FIFO can show visual artifacts, which is why FIFO is the default.

## Contributing

We are open for contributions.

 * The software is provided under the MIT license. Contributions to this project
   are accepted under the same license.
 * Please also ensure that each commit in the series has at least one
   `Signed-off-by:` line, using your real name and email address. The names in
   the `Signed-off-by:` and `Author:` lines must match. If anyone else
   contributes to the commit, they must also add their own `Signed-off-by:`
   line. By adding this line the contributor certifies the contribution is made
   under the terms of the [Developer Certificate of Origin (DCO)](DCO.txt).
 * Questions, bug reports, et cetera are raised and discussed on the issues page.
 * Please make merge requests into the main branch.
 * Code should be formatted with clang-format using the project's .clang-format
   configuration.

We use [pre-commit](https://pre-commit.com/) for local git hooks to help ensure
code quality and standardization. To install the hooks run the following
commands in the root of the repository:

    $ pip install pre-commit
    $ pre-commit install

Contributors are expected to abide by the
[freedesktop.org code of conduct](https://www.freedesktop.org/wiki/CodeOfConduct/).

### Implement a new WSI backend

Instructions on how to implement a WSI backend can be found in the
[README](wsi/README.md) in the wsi folder.

## Trace

When using other layers to trace content with the WSI Layer, special attention
should be paid to the order of the layers by the Vulkan® loader. The Vulkan WSI
Layer should be placed after the trace layer as it implements entrypoints that
may not be implemented by the ICD.

One way to avoid these kinds of issues is by using an implicit
[meta-layer](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md#meta-layers)
which will define the order of the layers and the WSI Layer should be placed at
the bottom of the list.

## Khronos® Conformance

This software is based on a published Khronos® Specification and is expected to
pass the relevant parts of the Khronos® Conformance Testing Process when used as
part of a conformant Vulkan® implementation.
