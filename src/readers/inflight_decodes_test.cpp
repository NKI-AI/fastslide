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

#include "fastslide/readers/inflight_decodes.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/runtime/lru_tile_cache.h"

namespace fastslide {
namespace {

struct FakeContext {
  std::shared_ptr<runtime::ITileCache> cache;

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache != nullptr; }
};

// Slow decoder of a 2x2 RGB tile whose pixels encode the tile x; counts
// decodes and can be made to fail.
class SlowExecutor : public CachedTileExecutor<SlowExecutor> {
 public:
  using CachedTileExecutor<SlowExecutor>::ReadWithCacheDecoded;

  static inline std::atomic<int> decodes{0};
  static inline std::atomic<bool> fail{false};

  static runtime::TileKey MakeCacheKey(const core::TileReadOp& op,
                                       const FakeContext&) {
    return runtime::TileKey("slow", 0, op.tile_coord.x, 0);
  }

  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& op, const FakeContext&) {
    decodes.fetch_add(1);
    // Long enough for every thread to miss while the first one decodes.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (fail.load()) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kDataLoss, "broken");
    }
    auto& buffer = GetBuffers().tile_buffer;
    buffer.assign(2 * 2 * 3, static_cast<uint8_t>(op.tile_coord.x));
    return DecodedTileData{buffer, 2, 2, 3};
  }
};

core::TileReadOp OpAt(uint32_t x) {
  core::TileReadOp op;
  op.tile_coord.x = x;
  op.tile_coord.y = 0;
  return op;
}

// Reads tile `x` from `threads` threads at once; returns how many succeeded.
int ReadConcurrently(const FakeContext& context, uint32_t x, int threads) {
  std::atomic<int> ok{0};
  std::atomic<int> wrong{0};
  std::vector<std::thread> pool;
  pool.reserve(threads);
  for (int i = 0; i < threads; ++i) {
    pool.emplace_back([&] {
      auto tile = SlowExecutor::ReadWithCacheDecoded(OpAt(x), context);
      if (!tile.ok()) {
        return;
      }
      ok.fetch_add(1);
      if (tile->data.size() != 12 || tile->data[0] != x) {
        wrong.fetch_add(1);
      }
    });
  }
  for (auto& t : pool) {
    t.join();
  }
  EXPECT_EQ(wrong.load(), 0);
  return ok.load();
}

TEST(InflightDecodesTest, ConcurrentMissesOnOneTileDecodeItOnce) {
  SlowExecutor::decodes = 0;
  SlowExecutor::fail = false;
  const FakeContext context{std::make_shared<runtime::LRUTileCache>(1 << 20)};

  EXPECT_EQ(ReadConcurrently(context, 3, 16), 16);
  EXPECT_EQ(SlowExecutor::decodes.load(), 1);
  EXPECT_EQ(readers::InflightDecodes::Instance().Size(), 0u);
}

TEST(InflightDecodesTest, DifferentTilesDecodeIndependently) {
  SlowExecutor::decodes = 0;
  SlowExecutor::fail = false;
  const FakeContext context{std::make_shared<runtime::LRUTileCache>(1 << 20)};

  std::thread a([&] { EXPECT_EQ(ReadConcurrently(context, 10, 4), 4); });
  std::thread b([&] { EXPECT_EQ(ReadConcurrently(context, 11, 4), 4); });
  a.join();
  b.join();
  EXPECT_EQ(SlowExecutor::decodes.load(), 2);
  EXPECT_EQ(readers::InflightDecodes::Instance().Size(), 0u);
}

TEST(InflightDecodesTest, AFailedDecodeWakesEveryWaiter) {
  SlowExecutor::decodes = 0;
  SlowExecutor::fail = true;
  const FakeContext context{std::make_shared<runtime::LRUTileCache>(1 << 20)};

  // Every reader gets the error (waiters retry the decode themselves) and
  // nobody is left waiting.
  EXPECT_EQ(ReadConcurrently(context, 5, 8), 0);
  EXPECT_GE(SlowExecutor::decodes.load(), 2);
  EXPECT_EQ(readers::InflightDecodes::Instance().Size(), 0u);
  SlowExecutor::fail = false;
}

TEST(InflightDecodesTest, ASingleMissSkipsTheTable) {
  SlowExecutor::decodes = 0;
  SlowExecutor::fail = false;
  const FakeContext context{std::make_shared<runtime::LRUTileCache>(1 << 20)};

  auto tile = SlowExecutor::ReadWithCacheDecoded(OpAt(7), context);
  ASSERT_TRUE(tile.ok());
  EXPECT_EQ(SlowExecutor::decodes.load(), 1);
  // Cached: no second decode.
  ASSERT_TRUE(SlowExecutor::ReadWithCacheDecoded(OpAt(7), context).ok());
  EXPECT_EQ(SlowExecutor::decodes.load(), 1);
  EXPECT_EQ(readers::InflightDecodes::Instance().Size(), 0u);
}

}  // namespace
}  // namespace fastslide
