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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TILE_BUFFER_POOL_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TILE_BUFFER_POOL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "fastslide/runtime/cache_interface.h"

namespace fastslide {
namespace readers {

/// @brief Recycles the pixel buffers of decoded tiles, per thread.
///
/// A decoded tile owns its pixel buffer. When the last handle to a tile made
/// by `NewPooledTile` is dropped, the buffer goes back to the pool of the
/// thread that dropped it, and the next decode on that thread can `Take` it.
/// A read without a tile cache, which decodes, paints and drops its tile on
/// one thread, then reuses one allocation, already the right size, instead of
/// allocating and zero-filling a new one per tile.
///
/// The pool is small and bounded: a thread keeps at most `kMaxBuffers`
/// buffers, none larger than `kMaxBufferBytes`. A thread gets a pool when it
/// first calls `Take`, so threads that only drop tiles keep nothing.
class TileBufferPool {
 public:
  static constexpr size_t kMaxBuffers = 2;
  static constexpr size_t kMaxBufferBytes = static_cast<size_t>(16) << 20;

  /// @brief A buffer from this thread's pool (keeping its size and capacity),
  /// or an empty one.
  [[nodiscard]] static std::vector<uint8_t> Take();

  /// @brief Return a buffer to this thread's pool, or free it if the thread
  /// has no pool, the pool is full or the buffer is too large.
  static void Recycle(std::vector<uint8_t>&& buffer);
};

/// @brief Make a shareable tile whose buffer returns to the pool of the
/// thread that drops the last handle to it.
[[nodiscard]] std::shared_ptr<runtime::CachedTileData> NewPooledTile(
    runtime::CachedTileData&& tile);

}  // namespace readers
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_TILE_BUFFER_POOL_H_
