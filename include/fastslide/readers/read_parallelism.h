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

#include <cstddef>
#include <utility>

#include "aifocore/utilities/thread_pool_singleton.h"
#include "fastslide/threading.h"

namespace fastslide {
namespace readers {

/// @brief Run `fn(i)` for every `i` in `[0, count)` and wait for all of them.
///
/// The executors run the tile operations of a read through this. In
/// `ReadParallelism::kThreadPool` mode (see fastslide/threading.h) the
/// operations go to the shared thread pool, except a single operation, which
/// runs on the calling thread (a pool hand-off only adds latency there). In
/// `kCallingThread` mode they all run on the calling thread. `fn` must be
/// thread-safe and must not throw.
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
