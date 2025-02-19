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

/**
 * @file wsi_extension.hpp
 *
 * @brief Contains the base class definition for wsi extension.
 */

#include "wsi_extension.hpp"

#include <util/log.hpp>

namespace wsi
{

wsi_ext_maintainer::wsi_ext_maintainer(const util::allocator &allocator)
   : m_enabled_extensions(allocator)
{
}

bool wsi_ext_maintainer::add_extension(util::unique_ptr<wsi_ext> extension)
{
   if (extension)
   {
      auto it = std::find_if(m_enabled_extensions.begin(), m_enabled_extensions.end(),
                             [&extension](util::unique_ptr<wsi_ext> &ext) { return ext->is_same_type(*extension); });

      if (it != m_enabled_extensions.end())
      {
         WSI_LOG_WARNING("Adding a duplicate extension (%s) to the extension list.", extension->get_name());
         assert(false && "Adding a duplicate extension to the extension list.");

         /* Replace the extension. Preferably this should never happen at runtime. */
         *it = std::move(extension);
         return true;
      }

      return m_enabled_extensions.try_push_back(std::move(extension));
   }
   return false;
}

} /* namespace wsi */
