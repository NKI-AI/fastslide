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

#include "fastslide/readers/tile_buffer_pool.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace fastslide {
namespace readers {

namespace {

struct Pool;

// This thread's pool, or null before its first `Take` and after the pool is
// destroyed at thread exit. Trivially destructible, so `Recycle` can read it
// safely from any destructor that runs while the thread exits (for example,
// one that drops the last handle to a tile).
thread_local Pool* this_thread_pool = nullptr;

struct Pool {
  std::vector<std::vector<uint8_t>> buffers;

  Pool() { this_thread_pool = this; }

  ~Pool() { this_thread_pool = nullptr; }

  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;
};

Pool& ThisThreadPool() {
  thread_local Pool pool;
  return pool;
}

}  // namespace

std::vector<uint8_t> TileBufferPool::Take() {
  auto& buffers = ThisThreadPool().buffers;
  if (buffers.empty()) {
    return {};
  }
  std::vector<uint8_t> buffer = std::move(buffers.back());
  buffers.pop_back();
  return buffer;
}

void TileBufferPool::Recycle(std::vector<uint8_t>&& buffer) {
  Pool* pool = this_thread_pool;
  if (pool == nullptr || buffer.capacity() == 0 ||
      buffer.capacity() > kMaxBufferBytes ||
      pool->buffers.size() >= kMaxBuffers) {
    return;  // the caller's buffer is freed as usual
  }
  pool->buffers.push_back(std::move(buffer));
}

std::shared_ptr<runtime::CachedTileData> NewPooledTile(
    runtime::CachedTileData&& tile) {
  return std::shared_ptr<runtime::CachedTileData>(
      new runtime::CachedTileData(std::move(tile)),
      [](runtime::CachedTileData* dropped) {
        TileBufferPool::Recycle(std::move(dropped->data));
        delete dropped;
      });
}

}  // namespace readers
}  // namespace fastslide
