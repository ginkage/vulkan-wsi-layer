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
 * @file custom_mutex.cpp
 * @brief Implementation of the exception‑neutral mutex helpers declared in
 *        @ref custom_mutex.hpp.
 */

#include "custom_mutex.hpp"
#include <cstring>

namespace util
{

bool mutex::lock() noexcept
{
   try
   {
      m_mtx.lock();
      return true;
   }
   catch (const std::system_error &)
   {
      WSI_LOG_WARNING("Failed to lock mutex: error %d (%s)", errno, std::strerror(errno));
      return false;
   }
}

bool mutex::try_lock() noexcept
{
   try
   {
      return m_mtx.try_lock();
   }
   catch (const std::system_error &)
   {
      return false;
   }
}

void mutex::unlock() noexcept
{
   m_mtx.unlock();
}

std::mutex &mutex::native() noexcept
{
   return m_mtx;
}

bool recursive_mutex::lock() noexcept
{
   try
   {
      m_mtx.lock();
      return true;
   }
   catch (const std::system_error &)
   {
      WSI_LOG_WARNING("Failed to lock recursive_mutex: error %d (%s)", errno, std::strerror(errno));
      return false;
   }
}

bool recursive_mutex::try_lock() noexcept
{
   try
   {
      return m_mtx.try_lock();
   }
   catch (const std::system_error &)
   {
      return false;
   }
}

void recursive_mutex::unlock() noexcept
{
   m_mtx.unlock();
}

std::recursive_mutex &recursive_mutex::native() noexcept
{
   return m_mtx;
}

} /* namespace util */
