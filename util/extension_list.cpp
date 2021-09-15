/*
 * Copyright (c) 2019, 2021 Arm Limited.
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

#include "extension_list.hpp"
#include "util/custom_allocator.hpp"
#include <layer/private_data.hpp>
#include <string.h>
#include <cassert>

namespace util
{

extension_list::extension_list(const util::allocator& allocator)
   : m_alloc{allocator}
   , m_ext_props(allocator)
{
}

VkResult extension_list::add(const char *const *extensions, uint32_t count)
{
   auto initial_size = m_ext_props.size();
   if (!m_ext_props.try_resize(initial_size + count))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (uint32_t i = 0; i < count; i++)
   {
      auto &dst = m_ext_props[initial_size + i];
      strncpy(dst.extensionName, extensions[i], sizeof(dst.extensionName));

      if (strlen(extensions[i]) >= sizeof(dst.extensionName))
      {
         dst.extensionName[sizeof(dst.extensionName) - 1] = '\0';
      }
   }
   return VK_SUCCESS;
}

VkResult extension_list::add(VkExtensionProperties ext_prop)
{
   if (!contains(ext_prop.extensionName))
   {
      if (!m_ext_props.try_push_back(ext_prop))
      {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
   return VK_SUCCESS;
}

VkResult extension_list::add(const VkExtensionProperties *props, uint32_t count)
{
   auto initial_size = m_ext_props.size();
   if (!m_ext_props.try_resize(initial_size + count))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (uint32_t i = 0; i < count; i++)
   {
      m_ext_props[initial_size + i] = props[i];
   }
   return VK_SUCCESS;
}

VkResult extension_list::add(const extension_list &ext_list)
{
   util::vector<const char *> ext_vect{m_alloc};
   VkResult result = ext_list.get_extension_strings(ext_vect);
   if (result != VK_SUCCESS)
   {
      return result;
   }
   return add(ext_vect.data(), ext_vect.size());
}

VkResult extension_list::get_extension_strings(util::vector<const char*> &out) const
{
   size_t old_size = out.size();
   size_t new_size = old_size + m_ext_props.size();
   if (!out.try_resize(new_size))
   {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (size_t i = old_size; i < new_size; i++)
   {
      out[i] = m_ext_props[i - old_size].extensionName;
   }
   return VK_SUCCESS;
}

bool extension_list::contains(const extension_list &req) const
{
   for (const auto &req_ext : req.m_ext_props)
   {
      if (!contains(req_ext.extensionName))
      {
         return false;
      }
   }
   return true;
}

bool extension_list::contains(const char *extension_name) const
{
   for (const auto &p : m_ext_props)
   {
      if (strcmp(p.extensionName, extension_name) == 0)
      {
         return true;
      }
   }
   return false;
}

void extension_list::remove(const char *ext)
{
   m_ext_props.erase(std::remove_if(m_ext_props.begin(), m_ext_props.end(), [&ext](VkExtensionProperties ext_prop) {
      return (strcmp(ext_prop.extensionName, ext) == 0);
   }));
}
} // namespace util
