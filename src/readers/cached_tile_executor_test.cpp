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

#include "fastslide/readers/cached_tile_executor.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fastslide/runtime/lru_tile_cache.h"

namespace fastslide {
namespace {

// Stand-in for an exec context: what CachedTileExecutor asks of a reader.
struct FakeContext {
  std::shared_ptr<runtime::ITileCache> cache;

  [[nodiscard]] std::shared_ptr<runtime::ITileCache> GetCache() const {
    return cache;
  }

  [[nodiscard]] bool IsCacheEnabled() const { return cache != nullptr; }
};

// Decodes a 2x2 RGB tile whose pixels encode the tile index, and counts
// decodes.
class FakeExecutor : public CachedTileExecutor<FakeExecutor> {
 public:
  using CachedTileExecutor<FakeExecutor>::ReadTile;

  static inline int decodes = 0;

  static runtime::TileKey MakeCacheKey(const core::TileReadOp& op,
                                       const FakeContext&) {
    return runtime::TileKey("fake", 0, op.tile_coord.x, op.tile_coord.y);
  }

  static aifocore::Result<CachedTileData> DecodeTile(const core::TileReadOp& op,
                                                     const FakeContext&) {
    ++decodes;
    return CachedTileData(
        std::vector<uint8_t>(2 * 2 * 3, static_cast<uint8_t>(op.tile_coord.x)),
        {2, 2}, 3);
  }
};

// Decodes into a buffer it owns (`DecodeTile`), and remembers where its last
// tile's pixels live.
class OwningExecutor : public CachedTileExecutor<OwningExecutor> {
 public:
  using CachedTileExecutor<OwningExecutor>::ReadTile;

  static inline const uint8_t* last_pixels = nullptr;

  static runtime::TileKey MakeCacheKey(const core::TileReadOp& op,
                                       const FakeContext&) {
    return runtime::TileKey("owning", 0, op.tile_coord.x, op.tile_coord.y);
  }

  static aifocore::Result<CachedTileData> DecodeTile(const core::TileReadOp& op,
                                                     const FakeContext&) {
    std::vector<uint8_t> pixels(2 * 2 * 3,
                                static_cast<uint8_t>(op.tile_coord.x));
    last_pixels = pixels.data();
    return CachedTileData(std::move(pixels), {2, 2}, 3);
  }
};

core::TileReadOp OpAt(uint32_t x) {
  core::TileReadOp op;
  op.tile_coord.x = x;
  op.tile_coord.y = 0;
  return op;
}

TEST(CachedTileExecutorTest, ReadTileHandsOutTheCachedTileOnAHit) {
  FakeExecutor::decodes = 0;
  const FakeContext context{std::make_shared<runtime::LRUTileCache>(1 << 20)};

  auto first = FakeExecutor::ReadTile(OpAt(7), context);
  ASSERT_TRUE(first.ok()) << first.status().ToString();
  auto second = FakeExecutor::ReadTile(OpAt(7), context);
  ASSERT_TRUE(second.ok()) << second.status().ToString();

  EXPECT_EQ(FakeExecutor::decodes, 1);
  // The hit returns the object stored by the miss: no copy.
  EXPECT_EQ(first->get(), second->get());
  EXPECT_EQ((*first)->data, std::vector<uint8_t>(12, 7));
  EXPECT_EQ((*first)->size[0], 2u);
  EXPECT_EQ((*first)->size[1], 2u);
  EXPECT_EQ((*first)->channels, 3u);
}

TEST(CachedTileExecutorTest, ReadTileWithoutACacheGivesIndependentTiles) {
  FakeExecutor::decodes = 0;
  const FakeContext context{nullptr};  // no cache: every read decodes

  auto tile_a = FakeExecutor::ReadTile(OpAt(1), context);
  ASSERT_TRUE(tile_a.ok());
  // Reading another tile on this thread must not change tile_a.
  auto tile_b = FakeExecutor::ReadTile(OpAt(2), context);
  ASSERT_TRUE(tile_b.ok());

  EXPECT_EQ(FakeExecutor::decodes, 2);
  EXPECT_EQ((*tile_a)->data, std::vector<uint8_t>(12, 1));
  EXPECT_EQ((*tile_b)->data, std::vector<uint8_t>(12, 2));
}

// A reader that decodes into its own buffer hands that buffer to the cache and
// to the caller: the decoded pixels are never copied.
TEST(CachedTileExecutorTest, OwnedDecodeIsCachedAndReturnedWithoutACopy) {
  auto cache = std::make_shared<runtime::LRUTileCache>(1 << 20);
  const FakeContext context{cache};

  auto tile = OwningExecutor::ReadTile(OpAt(3), context);
  ASSERT_TRUE(tile.ok()) << tile.status().ToString();
  EXPECT_EQ((*tile)->data.data(), OwningExecutor::last_pixels);
  EXPECT_EQ(cache->Get(OwningExecutor::MakeCacheKey(OpAt(3), context)).get(),
            tile->get());
  EXPECT_EQ((*tile)->data, std::vector<uint8_t>(12, 3));
}

TEST(CachedTileExecutorTest, OwnedDecodeWithoutACacheIsNotCopied) {
  const FakeContext context{nullptr};
  auto tile = OwningExecutor::ReadTile(OpAt(4), context);
  ASSERT_TRUE(tile.ok());
  EXPECT_EQ((*tile)->data.data(), OwningExecutor::last_pixels);
  EXPECT_EQ((*tile)->data, std::vector<uint8_t>(12, 4));
}

}  // namespace
}  // namespace fastslide
