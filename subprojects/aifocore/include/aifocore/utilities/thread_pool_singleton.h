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
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

#include "aifocore/utilities/bs_thread_pool.h"

namespace aifocore {

/// @brief Thread pool wrapper that executes tasks inline when submitted from
/// worker threads
///
/// Prevents deadlocks by detecting when a task is submitted from a worker
/// thread of the same pool and executing it immediately instead of queuing it.
/// This is critical for preventing thread pool exhaustion when tasks
/// recursively submit more tasks (e.g., fimage calling fastslide which submits
/// tile reading tasks).
class InlineThreadPool : public BS::light_thread_pool {
 public:
  using BS::light_thread_pool::light_thread_pool;

  /// @brief Submit blocks with inline execution detection
  ///
  /// This is critical because BS::light_thread_pool::submit_blocks() internally
  /// calls BS::light_thread_pool::submit_task() using non-virtual dispatch, so
  /// overriding submit_task() alone is insufficient to affect submit_blocks().
  template <typename T1, typename T2,
            typename T = BS::common_index_type_t<T1, T2>, typename F,
            typename R = std::invoke_result_t<std::decay_t<F>, T, T>>
  [[nodiscard]] BS::multi_future<R> submit_blocks(
      const T1 first_index, const T2 index_after_last, F&& block,
      const std::size_t num_blocks = 0, const BS::priority_t priority = 0) {
    if (BS::this_thread::get_pool() == static_cast<void*>(this)) {
      if (static_cast<T>(index_after_last) <= static_cast<T>(first_index)) {
        return {};
      }
      const std::shared_ptr<std::decay_t<F>> block_ptr =
          std::make_shared<std::decay_t<F>>(std::forward<F>(block));
      const BS::blocks blks(static_cast<T>(first_index),
                            static_cast<T>(index_after_last),
                            num_blocks ? num_blocks : this->get_thread_count());
      BS::multi_future<R> future;
      future.reserve(blks.get_num_blocks());
      for (std::size_t blk = 0; blk < blks.get_num_blocks(); ++blk) {
        std::promise<R> promise;
        auto f = promise.get_future();
        if constexpr (std::is_void_v<R>) {
          (*block_ptr)(blks.start(blk), blks.end(blk));
          promise.set_value();
        } else {
          promise.set_value((*block_ptr)(blks.start(blk), blks.end(blk)));
        }
        future.push_back(std::move(f));
      }
      return future;
    }
    return BS::light_thread_pool::submit_blocks(first_index, index_after_last,
                                                std::forward<F>(block),
                                                num_blocks, priority);
  }

  /// @brief Submit a sequence with inline execution detection
  ///
  /// Same rationale as submit_blocks():
  /// BS::light_thread_pool::submit_sequence() uses non-virtual dispatch to
  /// submit_task().
  template <typename T1, typename T2,
            typename T = BS::common_index_type_t<T1, T2>, typename F,
            typename R = std::invoke_result_t<std::decay_t<F>, T>>
  [[nodiscard]] BS::multi_future<R> submit_sequence(
      const T1 first_index, const T2 index_after_last, F&& sequence,
      const BS::priority_t priority = 0) {
    if (BS::this_thread::get_pool() == static_cast<void*>(this)) {
      if (static_cast<T>(index_after_last) <= static_cast<T>(first_index)) {
        return {};
      }
      const std::shared_ptr<std::decay_t<F>> sequence_ptr =
          std::make_shared<std::decay_t<F>>(std::forward<F>(sequence));
      BS::multi_future<R> future;
      future.reserve(static_cast<std::size_t>(static_cast<T>(index_after_last) -
                                              static_cast<T>(first_index)));
      for (T i = static_cast<T>(first_index);
           i < static_cast<T>(index_after_last); ++i) {
        std::promise<R> promise;
        auto f = promise.get_future();
        if constexpr (std::is_void_v<R>) {
          (*sequence_ptr)(i);
          promise.set_value();
        } else {
          promise.set_value((*sequence_ptr)(i));
        }
        future.push_back(std::move(f));
      }
      return future;
    }
    return BS::light_thread_pool::submit_sequence(
        first_index, index_after_last, std::forward<F>(sequence), priority);
  }

  /// @brief Submit task with inline execution detection
  /// @param task Function to execute
  /// @param priority Task priority (only for queued tasks)
  /// @note Executes inline if called from a worker thread of this pool
  template <typename F>
  void detach_task(F&& task, const BS::priority_t priority = 0) {
    if (BS::this_thread::get_pool() == static_cast<void*>(this)) {
      task();  // Execute inline if called from worker thread
      return;
    }
    BS::light_thread_pool::detach_task(std::forward<F>(task), priority);
  }

  /// @brief Submit task with future, inline execution detection
  /// @param task Function to execute
  /// @param priority Task priority (only for queued tasks)
  /// @return Future for the task result
  /// @note Executes inline if called from a worker thread of this pool
  template <typename F, typename R = std::invoke_result_t<std::decay_t<F>>>
  [[nodiscard]] std::future<R> submit_task(F&& task,
                                           const BS::priority_t priority = 0) {
    if (BS::this_thread::get_pool() == static_cast<void*>(this)) {
      // Execute inline and wrap result in ready future
      std::promise<R> promise;
      std::future<R> future = promise.get_future();
      if constexpr (std::is_void_v<R>) {
        task();
        promise.set_value();
      } else {
        promise.set_value(task());
      }
      return future;
    }
    return BS::light_thread_pool::submit_task(std::forward<F>(task), priority);
  }

  /// @brief Parallelize loop with inline execution detection
  /// @param first_index First loop index
  /// @param index_after_last Index after last loop index
  /// @param loop Function to call for each index
  /// @param num_blocks Number of blocks to split into (0 = use thread count)
  /// @param priority Task priority (only for queued tasks)
  /// @note Executes sequentially inline if called from a worker thread of this
  /// pool
  template <typename T1, typename T2,
            typename T = BS::common_index_type_t<T1, T2>, typename F>
  void detach_loop(const T1 first_index, const T2 index_after_last, F&& loop,
                   const std::size_t num_blocks = 0,
                   const BS::priority_t priority = 0) {
    if (BS::this_thread::get_pool() == static_cast<void*>(this)) {
      // Execute inline sequentially
      for (T i = static_cast<T>(first_index);
           i < static_cast<T>(index_after_last); ++i) {
        loop(i);
      }
      return;
    }
    BS::light_thread_pool::detach_loop(first_index, index_after_last,
                                       std::forward<F>(loop), num_blocks,
                                       priority);
  }

  /// @brief Parallelize loop with future, inline execution detection
  /// @param first_index First loop index
  /// @param index_after_last Index after last loop index
  /// @param loop Function to call for each index
  /// @param num_blocks Number of blocks to split into (0 = use thread count)
  /// @param priority Task priority (only for queued tasks)
  /// @return Multi-future for all blocks
  /// @note Executes sequentially inline if called from a worker thread of this
  /// pool
  template <typename T1, typename T2,
            typename T = BS::common_index_type_t<T1, T2>, typename F>
  [[nodiscard]] BS::multi_future<void> submit_loop(
      const T1 first_index, const T2 index_after_last, F&& loop,
      const std::size_t num_blocks = 0, const BS::priority_t priority = 0) {
    if (BS::this_thread::get_pool() == static_cast<void*>(this)) {
      // Execute inline sequentially and return ready future
      for (T i = static_cast<T>(first_index);
           i < static_cast<T>(index_after_last); ++i) {
        loop(i);
      }
      BS::multi_future<void> result;
      std::promise<void> promise;
      promise.set_value();
      result.push_back(promise.get_future());
      return result;
    }
    return BS::light_thread_pool::submit_loop(first_index, index_after_last,
                                              std::forward<F>(loop), num_blocks,
                                              priority);
  }
};

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
  /// @brief Get the global thread pool instance with inline execution
  ///
  /// Returns a reference to the singleton thread pool. The pool is created
  /// on first access with a default thread count determined by:
  /// 1. NUM_THREADS environment variable (if set)
  /// 2. Hardware concurrency (number of logical cores) otherwise
  ///
  /// The returned pool automatically executes tasks inline when submitted
  /// from worker threads, preventing deadlocks from nested task submission.
  ///
  /// @return Reference to the global InlineThreadPool
  static InlineThreadPool& GetInstance();

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
  static InlineThreadPool& GetPool(std::size_t count = 0);
};

}  // namespace aifocore

#endif  // AIFO_AIFOCORE_INCLUDE_AIFOCORE_UTILITIES_THREAD_POOL_SINGLETON_H_
