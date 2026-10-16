// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READ_PARALLELISM_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READ_PARALLELISM_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "aifocore/utilities/thread_pool_singleton.h"

namespace fastslide {
namespace readers {

/// @brief Where the tile operations of a single read run.
enum class ReadParallelism : std::uint8_t {
  /// Spread the operations over the shared `aifocore::ThreadPoolManager`
  /// pool; the calling thread waits. Lowest latency for one read at a time.
  kThreadPool = 0,
  /// Run the operations one after the other on the calling thread. Best
  /// throughput when the caller already runs many reads concurrently (one per
  /// request or worker): no hand-off to the pool, and the pool size no longer
  /// caps how many reads progress at once.
  kCallingThread = 1,
};

namespace detail {

/// Process-wide mode. Initialised from `FASTSLIDE_READ_PARALLELISM`
/// (`calling_thread` or `thread_pool`, default `thread_pool`).
inline std::atomic<ReadParallelism>& ReadParallelismSetting() {
  static std::atomic<ReadParallelism> setting{[] {
    const char* env = std::getenv("FASTSLIDE_READ_PARALLELISM");
    if (env != nullptr && std::strcmp(env, "calling_thread") == 0) {
      return ReadParallelism::kCallingThread;
    }
    return ReadParallelism::kThreadPool;
  }()};
  return setting;
}

}  // namespace detail

/// @brief Set where the tile operations of each read run (process-wide).
inline void SetReadParallelism(ReadParallelism mode) {
  detail::ReadParallelismSetting().store(mode, std::memory_order_relaxed);
}

/// @brief Current read parallelism mode.
[[nodiscard]] inline ReadParallelism GetReadParallelism() {
  return detail::ReadParallelismSetting().load(std::memory_order_relaxed);
}

/// @brief Run `fn(i)` for every `i` in `[0, count)` and wait for all of them.
///
/// Uses the shared thread pool in `ReadParallelism::kThreadPool` mode, except
/// for a single operation, which always runs on the calling thread (a pool
/// hand-off only adds latency there). `fn` must be thread-safe and must not
/// throw.
template <typename Fn>
void ForEachTileOp(std::size_t count, Fn&& fn) {
  if (count == 0) {
    return;
  }
  if (count == 1 || GetReadParallelism() == ReadParallelism::kCallingThread) {
    for (std::size_t i = 0; i < count; ++i) {
      fn(i);
    }
    return;
  }
  auto& pool = aifocore::ThreadPoolManager::GetInstance();
  pool.submit_sequence(std::size_t{0}, count, std::forward<Fn>(fn)).wait();
}

}  // namespace readers
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READ_PARALLELISM_H_
