// Copyright 2025 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_THREAD_POOL_SINGLETON_H_
#define AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_THREAD_POOL_SINGLETON_H_

#include <cstddef>

#include "aifocore/utilities/bs_thread_pool.h"

namespace aifocore {

/// @brief Global thread pool manager for aifocore libraries
///
/// Provides a shared thread pool instance that can be used by all libraries
/// and operations. This enables efficient parallelism without creating multiple
/// thread pools per component.
///
/// Thread-safe singleton with configurable thread count.
///
/// ## Thread Count Configuration
///
/// The thread count can be controlled via environment variable (OpenMP-style):
/// - `NUM_THREADS=1` : Single-threaded mode (no parallelism)
/// - `NUM_THREADS=4` : Use 4 threads
/// - `NUM_THREADS=0` or unset : Use hardware_concurrency()
///
/// Alternatively, call SetThreadCount() before first use.
class ThreadPoolManager {
 public:
  /// @brief Get the global thread pool instance
  ///
  /// Returns a reference to the singleton thread pool. The pool is created
  /// on first access with a default thread count determined by:
  /// 1. NUM_THREADS environment variable (if set)
  /// 2. Hardware concurrency (number of logical cores) otherwise
  ///
  /// @return Reference to the global BS::light_thread_pool
  static BS::light_thread_pool& GetInstance();

  /// @brief Set the number of threads in the pool
  ///
  /// Resets the thread pool with the specified number of threads. Can be
  /// called before or after GetInstance(). If called after tasks have been
  /// submitted, waits for all existing tasks to complete before resetting.
  ///
  /// @param count Number of threads to use (0 = use hardware_concurrency)
  static void SetThreadCount(std::size_t count);

 private:
  ThreadPoolManager() = delete;
  ~ThreadPoolManager() = delete;
  ThreadPoolManager(const ThreadPoolManager&) = delete;
  ThreadPoolManager& operator=(const ThreadPoolManager&) = delete;

  /// @brief Get or create the thread pool instance
  /// @param count Thread count (0 = hardware concurrency)
  /// @return Reference to the thread pool
  static BS::light_thread_pool& GetPool(std::size_t count = 0);
};

}  // namespace aifocore

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_THREAD_POOL_SINGLETON_H_
