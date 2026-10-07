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
 * @file low_address_map_api.cpp
 *
 * @brief Mapped pointers below 4 GiB for 32-bit applications run by a 64-bit process.
 *
 * Wine's WoW64 runs 32-bit Windows applications in a 64-bit process, so every pointer it hands them must fit in 32
 * bits. Without VK_EXT_map_memory_placed, which kbase cannot support (a SAME_VA allocation's CPU address is its GPU
 * address, so kbase refuses to place or move CPU mappings), Wine can only use what vkMapMemory returns, and the Mali
 * driver maps memory high. With WSI_LOW_ADDRESS_MAP=1 the layer returns a low pointer instead:
 *
 * - an alias: a second CPU mapping of the same pages below 4 GiB, from the low32 alias ioctl that the patched kbase
 *   in mali-vulkan-icd-wrapper adds (patches/kernel). Zero-copy, and coherent like the original mapping;
 * - otherwise a shadow: low anonymous memory that the layer copies to the real mapping on flush, before each queue
 *   submission and on unmap, and refreshes from it on invalidate. GPU writes to coherent memory only reach a shadow
 *   when the application invalidates.
 *
 * Ported from mali-vulkan-icd-wrapper (MIT), https://github.com/zeyadadev/mali-vulkan-icd-wrapper.
 */

#include "low_address_map_api.hpp"

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>

#include "private_data.hpp"

namespace
{

/* From the low32 alias patch to kbase's include/uapi/gpu/arm/valhall/mali_kbase_ioctl.h. */
constexpr unsigned kbase_ioctl_type = 0x80;
constexpr unsigned kbase_ioctl_extra_type = kbase_ioctl_type + 2;

struct kbase_ioctl_mem_free
{
   uint64_t gpu_addr;
};

struct kbase_ioctl_mem_low32_alias_create
{
   uint64_t user_addr;
   uint64_t size;
   uint64_t cookie;
};

#define KBASE_IOCTL_MEM_FREE _IOW(kbase_ioctl_type, 7, struct kbase_ioctl_mem_free)
#define KBASE_IOCTL_MEM_LOW32_ALIAS_CREATE _IOWR(kbase_ioctl_extra_type, 0, struct kbase_ioctl_mem_low32_alias_create)

constexpr uint64_t low_address_limit = 0x100000000ull;

/* Where to look for room for shadows: above the area 32-bit processes start with, below the top of the 32-bit
 * address space. */
constexpr uintptr_t shadow_search_start = 0x10000000u;
constexpr uintptr_t shadow_search_end = 0xf0000000u;
constexpr uintptr_t shadow_search_step = 0x100000u;

struct low_address_mapping
{
   /* The driver's mapping, and the offset into the memory object it starts at. */
   void *real;
   VkDeviceSize offset;
   size_t size;
   /* The pointer the application got. */
   void *low;
   /* What to munmap when done. */
   void *unmap_base;
   size_t unmap_size;
   /* A copy kept in sync by the layer, rather than an alias of the same pages. */
   bool shadow;
};

struct memory_key
{
   VkDevice device;
   VkDeviceMemory memory;

   bool operator==(const memory_key &other) const
   {
      return device == other.device && memory == other.memory;
   }
};

struct memory_key_hash
{
   size_t operator()(const memory_key &key) const
   {
      return std::hash<void *>{}(reinterpret_cast<void *>(key.device)) ^
             (std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(key.memory)) << 1);
   }
};

std::mutex mappings_lock;
std::unordered_map<memory_key, low_address_mapping, memory_key_hash> mappings;

size_t page_size()
{
   static const size_t size = []() {
      const long value = sysconf(_SC_PAGESIZE);
      return value > 0 ? static_cast<size_t>(value) : static_cast<size_t>(4096);
   }();
   return size;
}

bool fits_32_bits(const void *ptr, size_t size)
{
   return reinterpret_cast<uintptr_t>(ptr) + size <= low_address_limit;
}

/* Tell the user once per process which way the layer provides low pointers, as the shadow path costs a copy of
 * every shadow on each queue submission. */
void report_mode_once(bool shadow)
{
   static std::once_flag alias_reported;
   static std::once_flag shadow_reported;
   std::call_once(shadow ? shadow_reported : alias_reported, [shadow]() {
      std::fprintf(stderr, "wsi: low-address mapping uses %s\n",
                   shadow ? "shadow copies (kernel without the kbase low32 alias patch)" : "kbase low32 aliases");
   });
}

/* The size of the driver's mapping from @p ptr on: the rest of the VMA that holds it. */
size_t mapping_extent(const void *ptr)
{
   FILE *maps = std::fopen("/proc/self/maps", "re");
   if (maps == nullptr)
   {
      return 0;
   }

   const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
   size_t extent = 0;
   char line[512];
   while (std::fgets(line, sizeof(line), maps) != nullptr)
   {
      uintptr_t start = 0;
      uintptr_t end = 0;
      if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &start, &end) == 2 && address >= start && address < end)
      {
         extent = end - address;
         break;
      }
   }
   std::fclose(maps);
   return extent;
}

bool is_mali_device(int fd)
{
   char path[64];
   char target[64];
   std::snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
   const ssize_t length = readlink(path, target, sizeof(target) - 1);
   if (length <= 0)
   {
      return false;
   }
   target[length] = '\0';
   return std::strcmp(target, "/dev/mali0") == 0;
}

/* Ask kbase for a second mapping of the pages behind @p real below 4 GiB. Fails on kernels without the patch. */
bool create_alias(void *real, size_t size, low_address_mapping &mapping)
{
   const uintptr_t address = reinterpret_cast<uintptr_t>(real);
   const uintptr_t aligned = address & ~static_cast<uintptr_t>(page_size() - 1);
   const size_t page_offset = address - aligned;
   const size_t alias_size = (page_offset + size + page_size() - 1) & ~(page_size() - 1);

   DIR *fds = opendir("/proc/self/fd");
   if (fds == nullptr)
   {
      return false;
   }

   bool created = false;
   for (dirent *entry = readdir(fds); entry != nullptr && !created; entry = readdir(fds))
   {
      char *end = nullptr;
      const long fd = std::strtol(entry->d_name, &end, 10);
      if (end == entry->d_name || *end != '\0' || fd == dirfd(fds) || !is_mali_device(static_cast<int>(fd)))
      {
         continue;
      }

      kbase_ioctl_mem_low32_alias_create request = {};
      request.user_addr = aligned;
      request.size = alias_size;
      if (ioctl(static_cast<int>(fd), KBASE_IOCTL_MEM_LOW32_ALIAS_CREATE, &request) != 0)
      {
         continue;
      }

      void *base = mmap(nullptr, alias_size, PROT_READ | PROT_WRITE, MAP_SHARED, static_cast<int>(fd),
                        static_cast<off_t>(request.cookie));
      if (base == MAP_FAILED)
      {
         kbase_ioctl_mem_free free_request = { request.cookie };
         ioctl(static_cast<int>(fd), KBASE_IOCTL_MEM_FREE, &free_request);
         continue;
      }
      if (!fits_32_bits(base, alias_size))
      {
         munmap(base, alias_size);
         continue;
      }

      mapping.low = static_cast<uint8_t *>(base) + page_offset;
      mapping.unmap_base = base;
      mapping.unmap_size = alias_size;
      mapping.shadow = false;
      created = true;
   }
   closedir(fds);
   return created;
}

/* Anonymous memory below 4 GiB to shadow the real mapping with. */
bool create_shadow(void *real, size_t size, low_address_mapping &mapping)
{
   const size_t shadow_size = (size + page_size() - 1) & ~(page_size() - 1);
   for (uintptr_t address = shadow_search_start;
        address < shadow_search_end && address + shadow_size <= low_address_limit; address += shadow_search_step)
   {
      void *shadow = mmap(reinterpret_cast<void *>(address), shadow_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
      if (shadow == MAP_FAILED)
      {
         if (errno != EEXIST && errno != EINVAL && errno != ENOMEM && errno != EBUSY)
         {
            return false;
         }
         continue;
      }
      if (shadow != reinterpret_cast<void *>(address))
      {
         /* Kernels before 4.17 treat the flag as a hint. */
         munmap(shadow, shadow_size);
         continue;
      }

      std::memcpy(shadow, real, size);
      mapping.low = shadow;
      mapping.unmap_base = shadow;
      mapping.unmap_size = shadow_size;
      mapping.shadow = true;
      return true;
   }
   return false;
}

/* Replace the driver's high pointer in @p ppData with a low one, if it is high and one can be made. */
void apply_low_address(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, void **ppData)
{
   if (ppData == nullptr || *ppData == nullptr)
   {
      return;
   }

   const size_t extent = mapping_extent(*ppData);
   const size_t mapped_size = size == VK_WHOLE_SIZE ? extent : std::min(static_cast<size_t>(size), extent);
   if (mapped_size == 0 || fits_32_bits(*ppData, mapped_size))
   {
      return;
   }

   low_address_mapping mapping = {};
   mapping.real = *ppData;
   mapping.offset = offset;
   mapping.size = mapped_size;
   if (!create_alias(mapping.real, mapping.size, mapping) && !create_shadow(mapping.real, mapping.size, mapping))
   {
      WSI_LOG_WARNING("No room below 4 GiB for a %zu byte mapping", mapped_size);
      return;
   }
   report_mode_once(mapping.shadow);

   std::lock_guard<std::mutex> lock(mappings_lock);
   auto [it, inserted] = mappings.insert({ { device, memory }, mapping });
   if (!inserted)
   {
      munmap(it->second.unmap_base, it->second.unmap_size);
      it->second = mapping;
   }
   *ppData = mapping.low;
}

/* Take the low mapping of @p memory out of the table, if it has one. */
bool take_mapping(VkDevice device, VkDeviceMemory memory, low_address_mapping &mapping)
{
   std::lock_guard<std::mutex> lock(mappings_lock);
   auto it = mappings.find({ device, memory });
   if (it == mappings.end())
   {
      return false;
   }
   mapping = it->second;
   mappings.erase(it);
   return true;
}

/* Write back a shadow that is going away, and release the low mapping. */
void release_mapping(const low_address_mapping &mapping, bool write_back)
{
   if (mapping.shadow && write_back)
   {
      std::memcpy(mapping.real, mapping.low, mapping.size);
   }
   munmap(mapping.unmap_base, mapping.unmap_size);
}

/* The bytes of @p range that @p mapping covers, as an offset from the start of the mapping and a size. */
bool range_in_mapping(const low_address_mapping &mapping, const VkMappedMemoryRange &range, size_t &offset,
                      size_t &size)
{
   if (range.offset < mapping.offset || range.offset - mapping.offset >= mapping.size)
   {
      return false;
   }
   offset = static_cast<size_t>(range.offset - mapping.offset);
   size = range.size == VK_WHOLE_SIZE ? mapping.size - offset :
                                        std::min(static_cast<size_t>(range.size), mapping.size - offset);
   return true;
}

enum class sync_direction
{
   to_real,
   to_shadow,
};

void sync_ranges(VkDevice device, uint32_t range_count, const VkMappedMemoryRange *ranges, sync_direction direction)
{
   std::lock_guard<std::mutex> lock(mappings_lock);
   for (uint32_t i = 0; i < range_count; i++)
   {
      auto it = mappings.find({ device, ranges[i].memory });
      size_t offset = 0;
      size_t size = 0;
      if (it == mappings.end() || !it->second.shadow || !range_in_mapping(it->second, ranges[i], offset, size))
      {
         continue;
      }

      auto *real = static_cast<uint8_t *>(it->second.real) + offset;
      auto *low = static_cast<uint8_t *>(it->second.low) + offset;
      if (direction == sync_direction::to_real)
      {
         std::memcpy(real, low, size);
      }
      else
      {
         std::memcpy(low, real, size);
      }
   }
}

/* Make what the application wrote to its shadows visible to the work it is about to submit. */
void sync_device_shadows(VkDevice device)
{
   std::lock_guard<std::mutex> lock(mappings_lock);
   for (const auto &[key, mapping] : mappings)
   {
      if (key.device == device && mapping.shadow)
      {
         std::memcpy(mapping.real, mapping.low, mapping.size);
      }
   }
}

} /* namespace */

namespace layer
{

bool low_address_map_enabled()
{
   static const bool enabled = []() {
      const char *env = std::getenv("WSI_LOW_ADDRESS_MAP");
      return env != nullptr && std::strcmp(env, "0") != 0;
   }();
   return enabled;
}

void low_address_map_destroy_device(VkDevice device)
{
   std::lock_guard<std::mutex> lock(mappings_lock);
   for (auto it = mappings.begin(); it != mappings.end();)
   {
      if (it->first.device == device)
      {
         munmap(it->second.unmap_base, it->second.unmap_size);
         it = mappings.erase(it);
      }
      else
      {
         ++it;
      }
   }
}

void low_address_map_free_memory(VkDevice device, VkDeviceMemory memory)
{
   low_address_mapping mapping = {};
   if (take_mapping(device, memory, mapping))
   {
      /* The memory is going away with its mapping, so there is nothing to write back. */
      release_mapping(mapping, false);
   }
}

} /* namespace layer */

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                      VkMemoryMapFlags flags, void **ppData) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   const VkResult result = device_data.disp.MapMemory(device, memory, offset, size, flags, ppData);
   if (result == VK_SUCCESS)
   {
      apply_low_address(device, memory, offset, size, ppData);
   }
   return result;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkMapMemory2(VkDevice device, const VkMemoryMapInfo *pMemoryMapInfo, void **ppData) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(device);
   const VkResult result = device_data.disp.MapMemory2KHR(device, pMemoryMapInfo, ppData);
   if (result == VK_SUCCESS)
   {
      apply_low_address(device, pMemoryMapInfo->memory, pMemoryMapInfo->offset, pMemoryMapInfo->size, ppData);
   }
   return result;
}

VWL_VKAPI_CALL(void) wsi_layer_vkUnmapMemory(VkDevice device, VkDeviceMemory memory) VWL_API_POST
{
   low_address_mapping mapping = {};
   if (take_mapping(device, memory, mapping))
   {
      release_mapping(mapping, true);
   }
   layer::device_private_data::get(device).disp.UnmapMemory(device, memory);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo *pMemoryUnmapInfo) VWL_API_POST
{
   low_address_mapping mapping = {};
   if (take_mapping(device, pMemoryUnmapInfo->memory, mapping))
   {
      release_mapping(mapping, true);
   }
   return layer::device_private_data::get(device).disp.UnmapMemory2KHR(device, pMemoryUnmapInfo);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                    const VkMappedMemoryRange *pMemoryRanges) VWL_API_POST
{
   sync_ranges(device, memoryRangeCount, pMemoryRanges, sync_direction::to_real);
   return layer::device_private_data::get(device).disp.FlushMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                         const VkMappedMemoryRange *pMemoryRanges) VWL_API_POST
{
   const VkResult result = layer::device_private_data::get(device).disp.InvalidateMappedMemoryRanges(
      device, memoryRangeCount, pMemoryRanges);
   if (result == VK_SUCCESS)
   {
      sync_ranges(device, memoryRangeCount, pMemoryRanges, sync_direction::to_shadow);
   }
   return result;
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(queue);
   sync_device_shadows(device_data.device);
   return device_data.disp.QueueSubmit(queue, submitCount, pSubmits, fence);
}

VWL_VKAPI_CALL(VkResult)
wsi_layer_vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence) VWL_API_POST
{
   auto &device_data = layer::device_private_data::get(queue);
   sync_device_shadows(device_data.device);
   return device_data.disp.QueueSubmit2KHR(queue, submitCount, pSubmits, fence);
}
