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
 * @brief Contains the base class declarations for wsi extension.
 */

#pragma once
#include <cstring>
#include <algorithm>

#include "util/custom_allocator.hpp"

namespace wsi
{

#define WSI_DEFINE_EXTENSION(x)           \
   static constexpr char ext_name[] = #x; \
   const char *get_name() const override  \
   {                                      \
      return ext_name;                    \
   }

/**
 * @brief WSI extension class
 *
 * Base wsi_extension class, for each new extension we want to support
 * we will have to create a new class that will inherit this one.
 * Extensions that will need backend specific implementations/data
 * should have a specific pure virtual interface for backend implementation.
 *
 * @note All of the derived classes are required to use "WSI_DEFINE_EXTENSION"
 * macro within the body of the class as it will enable the derived classes to be
 * deduced through templates.
 */
class wsi_ext
{
public:
   virtual ~wsi_ext() = default;

   /**
    * @brief Get the name of the extension.
    *
    * This API is used to get the name of the extension which was
    * set when the object is created through the constructor.
    *
    * @return Name of the extension.
    */
   virtual const char *get_name() const = 0;

   /**
    * @brief Checks if the extension is of same type as this one.
    *
    * @param extension The extension to check.
    */
   bool is_same_type(const wsi_ext &extension) const
   {
      /* The functionality of this relies that the address of the pointers returned by
       * get_name() function is the same. As long as the derived extension is using
       * "WSI_DEFINE_EXTENSION" macro, then this should work. */
      return extension.get_name() == get_name();
   }

   /**
    * @brief Checks if the extension is of same type as this one.
    *
    * @tparam T The extension to check.
    * @return true if both extensions are of same type, false otherwise.
    */
   template <typename T>
   bool is_same_type() const
   {
      /* The functionality of this relies that the address of the pointers returned by
       * get_name() function & T::ext_name is the same. As long as the derived extension
       * is using "WSI_DEFINE_EXTENSION" macro, then this should work. */
      return T::ext_name == get_name();
   }
};

/**
 * @brief Swapchain extensions class
 *
 * Implementations which are specific to extensions are placed in specific classes that
 * are inherited from the class wsi_ext. wsi_ext_maintainer class helps in handling the
 * vector which contains unique_ptrs to those extension specific classes.
 */
class wsi_ext_maintainer
{
private:
   /**
    * @brief Keeps the pointer to extensions that are enabled.
    */
   util::vector<util::unique_ptr<wsi_ext>> m_enabled_extensions;

public:
   /**
    * @brief Constructor for the wsi_ext_maintainer class.
    */
   wsi_ext_maintainer(const util::allocator &allocator);

   /**
    * @brief Template API to get the pointer to the extension mentioned by the typename.
    *
    * @return Pointer to the extension.
    */
   template <typename T>
   T *get_extension()
   {
      auto it = std::find_if(m_enabled_extensions.begin(), m_enabled_extensions.end(),
                             [](util::unique_ptr<wsi_ext> &ext) { return ext->is_same_type<T>(); });

      return it != m_enabled_extensions.end() ? static_cast<T *>((*it).get()) : nullptr;
   }

   /**
    * @brief Adds an extension to the extensions list.
    *
    * @param extension The unique_ptr to the extension to be added.
    *
    * @return True when the extension is added successfully.
    */
   bool add_extension(util::unique_ptr<wsi_ext> extension);
};
} /* namespace wsi */
