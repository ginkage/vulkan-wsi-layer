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
 * @file custom_mutex.hpp
 * @brief Exception‑neutral mutex wrappers and RAII guards for the Vulkan WSI layer.
 *
 * Provides the following helpers. All locking is exception‑neutral: the
 * wrappers catch any `std::system_error` from the STL and expose boolean
 * success/failure. Immediate guards use blocking `lock()` by default;
 * `try_lock()` remains available for deferred cases.
 *   • util::mutex            — wraps `std::mutex`
 *   • util::recursive_mutex  — wraps `std::recursive_mutex`
 *   • util::unique_lock      — flexible RAII guard (defer/adopt/retry)
 */

#pragma once

#include <mutex>
#include <system_error>
#include <utility>
#include <cassert>
#include "helpers.hpp"

namespace util
{

/**
 * @class util::mutex
 * @brief Exception‑neutral wrapper around @c std::mutex.
 *
 * Provides only a non‑throwing @c try_lock() plus @c unlock().  Any
 * @c std::system_error raised by the STL is caught internally and reported as
 * a simple boolean failure.
 */
class mutex : private noncopyable
{
public:
   mutex() = default;
   ~mutex() = default;

   /**
    * @brief Block until the mutex is acquired.
    * @retval true  Lock acquired successfully.
    * @retval false Underlying OS error prevented locking.
    * Never throws; any @c std::system_error is caught internally.
    */
   bool lock() noexcept;

   /**
    * @brief Attempt to acquire the mutex without blocking.
    * @retval true  Lock acquired successfully.
    * @retval false The mutex was already owned or an OS error occurred.
    *
    * Never throws; any @c std::system_error is caught internally.
    */
   bool try_lock() noexcept;

   /**
    * @brief Release the mutex.
    *
    * Behaviour is undefined if the current thread does not own the lock.
    * This function is @c noexcept because the guard classes guarantee they
    * call it only when ownership is held.
    */
   void unlock() noexcept;

   /**
    * @brief Access the wrapped STL mutex.
    * @warning Bypassing the wrapper forfeits the exception‑neutral contract.
    */
   std::mutex &native() noexcept;

private:
   std::mutex m_mtx;
};

/**
 * @class util::recursive_mutex
 * @brief Re‑entrant variant of @ref util::mutex.
 *
 * Retains the same exception‑neutral contract while permitting the same
 * thread to acquire the lock multiple times.
 */
class recursive_mutex : private noncopyable
{
public:
   recursive_mutex() = default;
   ~recursive_mutex() = default;

   /**
    * @brief Block until the mutex is acquired.
    * @retval true  Lock acquired successfully.
    * @retval false Underlying OS error prevented locking.
    * Never throws; any @c std::system_error is caught internally.
    */
   bool lock() noexcept;

   /**
    * @brief Non‑blocking attempt to acquire the mutex.
    * @return See util::mutex::try_lock().
    */
   bool try_lock() noexcept;

   /**
    * @brief Release one ownership level of the recursive mutex.
    */
   void unlock() noexcept;

   /**
    * @brief Access the underlying `std::recursive_mutex`.
    * @warning Direct use bypasses the exception‑neutral guarantee.
    */
   std::recursive_mutex &native() noexcept;

private:
   std::recursive_mutex m_mtx;
};

/**
 * @class util::unique_lock
 * @tparam Mutex  Mutex‑like type (defaults to @ref util::mutex).
 * @brief Flexible RAII guard supporting defer‑lock, adopt‑lock, unlock, and retry.
 *
 * All lock attempts are non‑blocking and exception‑neutral; failure is
 * indicated by @c false, never by throwing.
 */
template <typename Mutex = util::mutex>
class unique_lock : private noncopyable
{
public:
   explicit unique_lock(Mutex &m) noexcept
      : m_mtx(&m)
   {
      m_owns = m_mtx->lock();
   }

   unique_lock(Mutex &m, std::defer_lock_t) noexcept
      : m_mtx(&m)
   {
   }
   unique_lock(Mutex &m, std::adopt_lock_t) noexcept
      : m_mtx(&m)
      , m_owns(true)
   {
   }

   ~unique_lock() noexcept
   {
      if (m_owns)
      {
         m_mtx->unlock();
      }
   }

   /** Block until the mutex is acquired (for defer‑lock cases). */
   bool lock() noexcept
   {
      assert(m_mtx && !m_owns && "unique_lock::lock: already owns or no mutex");
      m_owns = m_mtx->lock();
      return m_owns;
   }

   /* Retry after defer‑lock */
   bool try_lock() noexcept
   {
      assert(m_mtx && !m_owns && "unique_lock::try_lock: already owns or no mutex");
      m_owns = m_mtx->try_lock();
      return m_owns;
   }

   void unlock() noexcept
   {
      if (m_owns)
      {
         m_mtx->unlock();
         m_owns = false;
      }
   }

   bool owns_lock() const noexcept
   {
      return m_owns;
   }
   explicit operator bool() const noexcept
   {
      return owns_lock();
   }

   /** Disown without unlocking – caller becomes responsible. */
   Mutex *release() noexcept
   {
      m_owns = false;
      return std::exchange(m_mtx, nullptr);
   }

   /** Return a reference to the wrapped std::mutex (requires Mutex::native()). */
   auto &native_mutex() noexcept
   {
      return m_mtx->native();
   }

private:
   Mutex *m_mtx{ nullptr };
   bool m_owns{ false };
};

} /* namespace util */
