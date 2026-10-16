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

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "fastslide/readers/cached_tile_executor.h"
#include "fastslide/runtime/lru_tile_cache.h"

namespace fastslide {
namespace {

constexpr size_t kTileBytes = 2 * 2 * 3;

// Stand-in for an exec context: what CachedTileExecutor asks of a reader.
struct FakeContext {
  std::shared_ptr<runtime::ITileCache> cache;

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache != nullptr; }
};

// Decodes a 2x2 RGB tile whose pixels encode the tile index into the
// thread-local tile buffer, like the TIFF-based executors.
class FakeExecutor : public CachedTileExecutor<FakeExecutor> {
 public:
  using CachedTileExecutor<FakeExecutor>::ReadWithCache;
  using CachedTileExecutor<FakeExecutor>::ReadWithCacheDecoded;

  // Capacity to reserve in the decode buffer before decoding.
  static inline size_t reserve = 0;

  static std::vector<uint8_t>& TileBuffer() { return GetBuffers().tile_buffer; }

  static runtime::TileKey MakeCacheKey(const core::TileReadOp& op,
                                       const FakeContext&) {
    return runtime::TileKey("fake", 0, op.tile_coord.x, op.tile_coord.y);
  }

  static aifocore::Result<DecodedTileData> ReadTileFromDisk(
      const core::TileReadOp& op, const FakeContext&) {
    auto& buffer = GetBuffers().tile_buffer;
    buffer.reserve(reserve);
    buffer.assign(kTileBytes, static_cast<uint8_t>(op.tile_coord.x));
    return DecodedTileData{buffer, 2, 2, 3};
  }
};

core::TileReadOp OpAt(uint32_t x) {
  core::TileReadOp op;
  op.tile_coord.x = x;
  op.tile_coord.y = 0;
  return op;
}

runtime::TileKey KeyAt(uint32_t x) {
  return runtime::TileKey("fake", 0, x, 0);
}

TEST(CachedTileInsertTest, MissMovesTheDecodeBufferIntoTheCache) {
  FakeExecutor::reserve = 0;
  FakeExecutor::TileBuffer() = {};
  auto cache = std::make_shared<runtime::LRUTileCache>(1 << 20);
  const FakeContext context{cache};

  auto span = FakeExecutor::ReadWithCache(OpAt(3), context);
  ASSERT_TRUE(span.ok()) << span.status().ToString();

  auto cached = cache->Get(KeyAt(3));
  ASSERT_NE(cached, nullptr);
  // The entry holds the bytes the read returned: no copy was made.
  EXPECT_EQ(cached->data.data(), span->data());
  EXPECT_EQ(cached->data, std::vector<uint8_t>(kTileBytes, 3));
  EXPECT_EQ(cached->GetMemoryUsage(), kTileBytes);
  EXPECT_TRUE(FakeExecutor::TileBuffer().empty());

  // The next decode gets a buffer of its own and leaves the entry intact.
  auto other = FakeExecutor::ReadWithCacheDecoded(OpAt(4), context);
  ASSERT_TRUE(other.ok());
  EXPECT_EQ(cached->data, std::vector<uint8_t>(kTileBytes, 3));
  EXPECT_EQ(std::vector<uint8_t>(other->data.begin(), other->data.end()),
            std::vector<uint8_t>(kTileBytes, 4));
}

TEST(CachedTileInsertTest, ReturnedTileOutlivesAnEntryTheCacheDrops) {
  FakeExecutor::reserve = 0;
  FakeExecutor::TileBuffer() = {};
  // Smaller than one tile: Put drops every entry at once.
  const FakeContext context{std::make_shared<runtime::LRUTileCache>(4)};

  auto span = FakeExecutor::ReadWithCache(OpAt(5), context);
  ASSERT_TRUE(span.ok());
  EXPECT_EQ(std::vector<uint8_t>(span->begin(), span->end()),
            std::vector<uint8_t>(kTileBytes, 5));

  auto decoded = FakeExecutor::ReadWithCacheDecoded(OpAt(6), context);
  ASSERT_TRUE(decoded.ok());
  EXPECT_EQ(std::vector<uint8_t>(decoded->data.begin(), decoded->data.end()),
            std::vector<uint8_t>(kTileBytes, 6));
}

TEST(CachedTileInsertTest, OversizedDecodeBufferIsCopiedAndKept) {
  // A buffer grown by a much larger tile is not handed to the cache, whose
  // accounting only sees the tile's bytes.
  FakeExecutor::reserve = 64 * kTileBytes;
  FakeExecutor::TileBuffer() = {};
  auto cache = std::make_shared<runtime::LRUTileCache>(1 << 20);
  const FakeContext context{cache};

  auto span = FakeExecutor::ReadWithCache(OpAt(7), context);
  ASSERT_TRUE(span.ok());

  auto cached = cache->Get(KeyAt(7));
  ASSERT_NE(cached, nullptr);
  EXPECT_NE(cached->data.data(), FakeExecutor::TileBuffer().data());
  EXPECT_EQ(cached->data, std::vector<uint8_t>(kTileBytes, 7));
  EXPECT_GE(FakeExecutor::TileBuffer().capacity(), 64 * kTileBytes);
  FakeExecutor::reserve = 0;
}

TEST(CachedTileInsertTest, EvictedTileIsFreedOnceNobodyHoldsIt) {
  runtime::LRUTileCache cache(kTileBytes);
  auto first =
      std::make_shared<CachedTileData>(std::vector<uint8_t>(kTileBytes, 1),
                                       aifocore::Size<uint32_t, 2>{2, 2}, 3);
  std::weak_ptr<CachedTileData> watch = first;
  cache.Put(KeyAt(1), std::move(first));

  cache.Put(KeyAt(2), std::make_shared<CachedTileData>(
                          std::vector<uint8_t>(kTileBytes, 2),
                          aifocore::Size<uint32_t, 2>{2, 2}, 3));

  EXPECT_TRUE(watch.expired());
  EXPECT_EQ(cache.Get(KeyAt(1)), nullptr);
  ASSERT_NE(cache.Get(KeyAt(2)), nullptr);
  EXPECT_EQ(cache.GetMemoryUsage(), kTileBytes);
}

}  // namespace
}  // namespace fastslide
