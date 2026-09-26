/*
 * Copyright (c) 2026 Arm Limited.
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
 * @file dmabuf_export_api.cpp
 *
 * @brief Emulation of dma-buf memory export for drivers that can only import dma-bufs.
 *
 * The Mali driver imports dma-bufs but cannot export its memory as one - it reports DMA_BUF as not exportable,
 * and vkGetMemoryFdKHR returns VK_SUCCESS with fd -1. Mesa's zink needs dma-buf export to share buffers with a
 * compositor or the display, so for allocations that ask for it, allocate the memory from a dma-buf heap and
 * import it instead, and hand out duplicates of that dma-buf.
 */

#include "dmabuf_export_api.hpp"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "private_data.hpp"
#include "util/helpers.hpp"
#include "util/log.hpp"

namespace
{
#define DMABUF_EXPORT_STR2(x) #x
#define DMABUF_EXPORT_STR(x) DMABUF_EXPORT_STR2(x)
#ifdef WSIALLOC_MEMORY_HEAP_NAME
constexpr const char *heap_path = "/dev/dma_heap/" DMABUF_EXPORT_STR(WSIALLOC_MEMORY_HEAP_NAME);
#else
constexpr const char *heap_path = "/dev/dma_heap/system";
#endif

/* CPU mappings of an uncached heap's buffers are uncached, so coherent whatever the memory type says. */
const bool heap_is_uncached = std::strstr(heap_path, "uncached") != nullptr;

int heap_fd()
{
   static const int fd = open(heap_path, O_RDONLY | O_CLOEXEC);
   return fd;
}

int allocate_dmabuf(VkDeviceSize size)
{
   const long page_size = sysconf(_SC_PAGESIZE);
   const VkDeviceSize page = page_size > 0 ? static_cast<VkDeviceSize>(page_size) : 4096;
   dma_heap_allocation_data data = {};
   data.len = (size + page - 1) / page * page;
   data.fd_flags = O_RDWR | O_CLOEXEC;
   if (ioctl(heap_fd(), DMA_HEAP_IOCTL_ALLOC, &data) != 0)
   {
      return -1;
   }
   return static_cast<int>(data.fd);
}

/* The memory type to import a dma-buf allocated for @p allocate_info with, or -1 if none will do. The driver may
 * accept imported dma-bufs only for other types than it offers for the resource (the Mali driver: only its
 * HOST_CACHED type, while applications tend to pick the HOST_COHERENT one), so take any type of the same heap it
 * accepts that has all the requested properties - with an uncached heap, coherence comes for free - and that a
 * dedicated image or buffer accepts too. */
int import_memory_type(layer::device_private_data &device_data, const VkMemoryAllocateInfo &allocate_info,
                       uint32_t dmabuf_type_bits)
{
   if (dmabuf_type_bits & (1u << allocate_info.memoryTypeIndex))
   {
      return static_cast<int>(allocate_info.memoryTypeIndex);
   }

   VkPhysicalDeviceMemoryProperties2 memory_properties = {};
   memory_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
   device_data.instance_data.disp.GetPhysicalDeviceMemoryProperties2KHR(device_data.physical_device,
                                                                        &memory_properties);
   const auto &types = memory_properties.memoryProperties.memoryTypes;
   if (allocate_info.memoryTypeIndex >= memory_properties.memoryProperties.memoryTypeCount)
   {
      return -1;
   }
   VkMemoryPropertyFlags wanted = types[allocate_info.memoryTypeIndex].propertyFlags;
   if (heap_is_uncached)
   {
      wanted &= ~static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   }

   uint32_t allowed = dmabuf_type_bits;
   const auto *dedicated = util::find_extension<VkMemoryDedicatedAllocateInfo>(
      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, allocate_info.pNext);
   if (dedicated != nullptr && dedicated->image != VK_NULL_HANDLE)
   {
      VkMemoryRequirements requirements;
      device_data.disp.GetImageMemoryRequirements(device_data.device, dedicated->image, &requirements);
      allowed &= requirements.memoryTypeBits;
   }
   else if (dedicated != nullptr && dedicated->buffer != VK_NULL_HANDLE)
   {
      VkMemoryRequirements requirements;
      device_data.disp.GetBufferMemoryRequirements(device_data.device, dedicated->buffer, &requirements);
      allowed &= requirements.memoryTypeBits;
   }

   for (uint32_t type = 0; type < memory_properties.memoryProperties.memoryTypeCount; type++)
   {
      if ((allowed & (1u << type)) && types[type].heapIndex == types[allocate_info.memoryTypeIndex].heapIndex &&
          (types[type].propertyFlags & wanted) == wanted)
      {
         return static_cast<int>(type);
      }
   }
   return -1;
}

/* Report DMA_BUF as exportable where the driver can import it, since the layer can then export it too. */
void make_dmabuf_exportable(VkPhysicalDevice physical_device, VkExternalMemoryProperties &properties)
{
   constexpr VkExternalMemoryHandleTypeFlags dmabuf = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
   if ((properties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) &&
       !(properties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) &&
       layer::dmabuf_export_emulated(physical_device))
   {
      properties.externalMemoryFeatures |= VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;
      properties.exportFromImportedHandleTypes |= dmabuf;
      properties.compatibleHandleTypes |= dmabuf;
   }
}
} /* namespace */

namespace layer
{
bool dmabuf_export_emulated(VkPhysicalDevice physical_device)
{
   static const bool enabled = []() {
      const char *env = std::getenv("WSI_EMULATE_DMABUF_EXPORT");
      return env == nullptr || std::strcmp(env, "0") != 0;
   }();
   if (!enabled || heap_fd() < 0)
   {
      return false;
   }

   VkPhysicalDeviceExternalBufferInfo buffer_info = {};
   buffer_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
   buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
   buffer_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
   VkExternalBufferProperties buffer_properties = {};
   buffer_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
   instance_private_data::get(physical_device)
      .disp.GetPhysicalDeviceExternalBufferPropertiesKHR(physical_device, &buffer_info, &buffer_properties);

   const VkExternalMemoryFeatureFlags features = buffer_properties.externalMemoryProperties.externalMemoryFeatures;
   return (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) &&
          !(features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
}
} /* namespace layer */

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                           const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   const auto *export_info = util::find_extension<VkExportMemoryAllocateInfo>(
      VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, pAllocateInfo->pNext);
   if (export_info == nullptr || !(export_info->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT))
   {
      return device_data.disp.AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
   }

   /* Memory imported from a dma-buf (such as Mesa's render-only scanout buffers, allocated on the display device)
    * must keep that dma-buf as its storage: the driver takes ownership of the fd, so keep a duplicate to export. */
   const auto *import_info_in =
      util::find_extension<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, pAllocateInfo->pNext);
   if (import_info_in != nullptr)
   {
      const int exported =
         import_info_in->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT && import_info_in->fd >= 0 ?
            fcntl(import_info_in->fd, F_DUPFD_CLOEXEC, 0) :
            -1;
      const VkResult result = device_data.disp.AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
      if (exported >= 0)
      {
         util::unique_lock<util::mutex> lock(device_data.dmabuf_exports_lock);
         if (result != VK_SUCCESS || !lock ||
             !device_data.dmabuf_exports.try_insert({ *pMemory, exported }).has_value())
         {
            close(exported);
         }
      }
      return result;
   }

   /* Should anything below fail, allocate as asked: the application then gets the driver's own behaviour. */
   const int dmabuf = allocate_dmabuf(pAllocateInfo->allocationSize);
   if (dmabuf < 0)
   {
      WSI_LOG_WARNING("Failed to allocate an exportable dma-buf of %llu bytes",
                      static_cast<unsigned long long>(pAllocateInfo->allocationSize));
      return device_data.disp.AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
   }

   VkMemoryFdPropertiesKHR fd_properties = {};
   fd_properties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
   const int memory_type =
      device_data.disp.GetMemoryFdPropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dmabuf,
                                                &fd_properties) == VK_SUCCESS ?
         import_memory_type(device_data, *pAllocateInfo, fd_properties.memoryTypeBits) :
         -1;
   /* A successful import takes ownership of the fd it is given, so import a duplicate and keep the original for
    * vkGetMemoryFdKHR. */
   const int import_fd = memory_type >= 0 ? fcntl(dmabuf, F_DUPFD_CLOEXEC, 0) : -1;
   if (import_fd < 0)
   {
      close(dmabuf);
      return device_data.disp.AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
   }

   VkImportMemoryFdInfoKHR import_info = {};
   import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
   import_info.pNext = pAllocateInfo->pNext;
   import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
   import_info.fd = import_fd;
   VkMemoryAllocateInfo import_allocate_info = *pAllocateInfo;
   import_allocate_info.pNext = &import_info;
   import_allocate_info.memoryTypeIndex = static_cast<uint32_t>(memory_type);

   VkResult result = device_data.disp.AllocateMemory(device, &import_allocate_info, pAllocator, pMemory);
   if (result != VK_SUCCESS)
   {
      close(import_fd);
      close(dmabuf);
      return device_data.disp.AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
   }

   util::unique_lock<util::mutex> lock(device_data.dmabuf_exports_lock);
   if (!lock || !device_data.dmabuf_exports.try_insert({ *pMemory, dmabuf }).has_value())
   {
      device_data.disp.FreeMemory(device, *pMemory, pAllocator);
      *pMemory = VK_NULL_HANDLE;
      close(dmabuf);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   return VK_SUCCESS;
}

VWL_VKAPI_CALL(void)
wsi_layer_vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   if (memory != VK_NULL_HANDLE)
   {
      util::unique_lock<util::mutex> lock(device_data.dmabuf_exports_lock);
      if (lock)
      {
         auto it = device_data.dmabuf_exports.find(memory);
         if (it != device_data.dmabuf_exports.end())
         {
            close(it->second);
            device_data.dmabuf_exports.erase(it);
         }
      }
   }
   device_data.disp.FreeMemory(device, memory, pAllocator);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR *pGetFdInfo, int *pFd) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   /* zink exports OPAQUE_FD to get a KMS handle (drmPrimeFDToHandle), as on Mesa's own drivers an opaque fd is
    * the dma-buf. Hand out the dma-buf for that too: the Mali driver's own opaque fd is -1 for this memory. */
   if (pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT ||
       pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
   {
      util::unique_lock<util::mutex> lock(device_data.dmabuf_exports_lock);
      if (!lock)
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      auto it = device_data.dmabuf_exports.find(pGetFdInfo->memory);
      if (it != device_data.dmabuf_exports.end())
      {
         *pFd = fcntl(it->second, F_DUPFD_CLOEXEC, 0);
         return *pFd >= 0 ? VK_SUCCESS : VK_ERROR_TOO_MANY_OBJECTS;
      }
   }

   /* The Mali driver reports success for memory it cannot export, with fd -1. */
   const VkResult result = device_data.disp.GetMemoryFdKHR(device, pGetFdInfo, pFd);
   return (result == VK_SUCCESS && *pFd < 0) ? VK_ERROR_TOO_MANY_OBJECTS : result;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                                    const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
                                                    VkImageFormatProperties2 *pImageFormatProperties) VWL_API_POST
{
   const VkResult result =
      layer::instance_private_data::get(physicalDevice)
         .disp.GetPhysicalDeviceImageFormatProperties2KHR(physicalDevice, pImageFormatInfo, pImageFormatProperties);
   const auto *external_info = util::find_extension<VkPhysicalDeviceExternalImageFormatInfo>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO, pImageFormatInfo->pNext);
   auto *external_properties = util::find_extension<VkExternalImageFormatProperties>(
      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES, pImageFormatProperties->pNext);
   if (result == VK_SUCCESS && external_info != nullptr && external_properties != nullptr &&
       external_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
   {
      make_dmabuf_exportable(physicalDevice, external_properties->externalMemoryProperties);
   }
   return result;
}

VWL_VKAPI_CALL(void)
wsi_layer_vkGetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties) VWL_API_POST
{
   layer::instance_private_data::get(physicalDevice)
      .disp.GetPhysicalDeviceExternalBufferPropertiesKHR(physicalDevice, pExternalBufferInfo,
                                                         pExternalBufferProperties);
   if (pExternalBufferInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)
   {
      make_dmabuf_exportable(physicalDevice, pExternalBufferProperties->externalMemoryProperties);
   }
}
