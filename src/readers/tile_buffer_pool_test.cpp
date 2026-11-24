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

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace fastslide {
namespace readers {
namespace {

using runtime::CachedTileData;

// Empties this thread's pool, so each test starts from a known state.
void DrainPool() {
  while (TileBufferPool::Take().capacity() != 0) {}
}

std::shared_ptr<CachedTileData> TileOf(std::vector<uint8_t> pixels) {
  return NewPooledTile(CachedTileData(std::move(pixels), {4, 4}, 3));
}

TEST(TileBufferPoolTest, DroppedTileReturnsItsBufferToTheNextDecode) {
  DrainPool();
  auto tile = TileOf(std::vector<uint8_t>(48, 9));
  const uint8_t* pixels = tile->data.data();
  tile.reset();

  std::vector<uint8_t> reused = TileBufferPool::Take();
  // The same allocation, with its size kept: resizing it to the same size
  // neither reallocates nor zero-fills.
  EXPECT_EQ(reused.data(), pixels);
  EXPECT_EQ(reused.size(), 48U);
  EXPECT_EQ(reused[0], 9);
  EXPECT_TRUE(TileBufferPool::Take().empty());
}

TEST(TileBufferPoolTest, KeepsAtMostMaxBuffers) {
  DrainPool();
  for (size_t i = 0; i < TileBufferPool::kMaxBuffers + 3; ++i) {
    TileBufferPool::Recycle(std::vector<uint8_t>(16));
  }
  size_t kept = 0;
  while (TileBufferPool::Take().capacity() != 0) {
    ++kept;
  }
  EXPECT_EQ(kept, TileBufferPool::kMaxBuffers);
}

TEST(TileBufferPoolTest, DoesNotKeepLargeBuffers) {
  DrainPool();
  TileBufferPool::Recycle(
      std::vector<uint8_t>(TileBufferPool::kMaxBufferBytes + 1));
  EXPECT_EQ(TileBufferPool::Take().capacity(), 0U);
}

TEST(TileBufferPoolTest, ThreadWithoutAPoolFreesTheBuffer) {
  DrainPool();
  auto tile = TileOf(std::vector<uint8_t>(48, 1));
  // Dropped on a thread that never took a buffer: freed, not pooled there,
  // and not returned to this thread's pool either.
  std::thread([t = std::move(tile)]() mutable { t.reset(); }).join();
  EXPECT_EQ(TileBufferPool::Take().capacity(), 0U);
}

TEST(TileBufferPoolTest, TileDroppedDuringThreadExitIsFreedSafely) {
  // A thread-local that is destroyed after the thread's pool still holds a
  // tile: dropping it then must not touch the destroyed pool.
  std::thread([] {
    // Thread-locals are destroyed in reverse order of construction: `held`
    // is constructed first, so it is destroyed after the pool.
    thread_local std::shared_ptr<CachedTileData> held;
    static_cast<void>(TileBufferPool::Take());  // create this thread's pool
    // A pooled buffer, so the destroyed pool has storage that a late recycle
    // would write into.
    TileBufferPool::Recycle(std::vector<uint8_t>(16));
    held = TileOf(std::vector<uint8_t>(48, 2));
  }).join();
  SUCCEED();
}

}  // namespace
}  // namespace readers
}  // namespace fastslide
