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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_THREADING_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_THREADING_H_

/**
 * @file threading.h
 * @brief How reads use threads (process-wide settings).
 *
 * A read is split into tile operations (decode one stored tile, copy the part
 * that overlaps the region). By default the operations of one read are spread
 * over a shared, process-wide thread pool and the calling thread waits for
 * them; a read of a single tile runs on the calling thread.
 *
 * Two settings control this, both process-wide:
 *
 * - `SetReadParallelism`: `kThreadPool` (default) or `kCallingThread`, which
 *   runs every operation of a read on the thread that called `ReadRegion`.
 *   Use `kCallingThread` when the application already runs about as many
 *   reads at once as there are cores (tile servers, threaded data loaders):
 *   no hand-off to the pool, less CPU per read, and the pool size no longer
 *   caps how many reads progress. With few concurrent reads it is slower,
 *   because a read no longer uses several cores.
 * - `SetThreadPoolSize`: threads in the shared pool (used in `kThreadPool`
 *   mode). It defaults to the `NUM_THREADS` environment variable or the
 *   number of hardware threads, which in a container is the host's, not the
 *   CPU quota.
 *
 * `FASTSLIDE_READ_PARALLELISM=calling_thread` (or `thread_pool`) sets the
 * mode at startup. Both settings apply to reads that start after the call.
 */

#include <cstddef>
#include <cstdint>

namespace fastslide {

/// @brief Where the tile operations of a read run.
enum class ReadParallelism : std::uint8_t {
  /// Spread over the shared thread pool; the calling thread waits (default).
  /// Lowest latency when few reads run at once.
  kThreadPool = 0,
  /// Run one after the other on the calling thread. Best throughput when the
  /// application runs about as many reads at once as there are cores.
  kCallingThread = 1,
};

/// @brief Set where the tile operations of each read run (process-wide).
void SetReadParallelism(ReadParallelism mode);

/// @brief Current read parallelism. Initialised from
/// `FASTSLIDE_READ_PARALLELISM` (`calling_thread` or `thread_pool`).
[[nodiscard]] ReadParallelism GetReadParallelism();

/// @brief Resize the shared thread pool (process-wide).
///
/// Waits for the tasks that are running, then replaces the pool's threads;
/// queued tasks run on the new threads. Do not call it from inside a read.
/// @param num_threads Number of threads; 0 means the number of hardware
///        threads.
void SetThreadPoolSize(std::size_t num_threads);

/// @brief Number of threads in the shared thread pool.
[[nodiscard]] std::size_t GetThreadPoolSize();

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_THREADING_H_
