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

#include "fastslide/runtime/global_cache_manager.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/lru_tile_cache.h"

namespace fastslide::runtime {
namespace {

std::shared_ptr<CachedTileData> Tile(size_t bytes) {
  return std::make_shared<CachedTileData>(
      std::vector<uint8_t>(bytes, 1),
      aifocore::Size<uint32_t, 2>{static_cast<uint32_t>(bytes), 1}, 1);
}

TileKey Key(uint32_t x) {
  return TileKey("slide", 0, x, 0);
}

// The manager is a process-wide singleton: give every test its own cache and
// leave a fresh default one behind.
class GlobalCacheManagerTest : public ::testing::Test {
 protected:
  void TearDown() override {
    GlobalCacheManager::Instance().SetCache(
        LRUTileCache::Create(static_cast<size_t>(1) << 30).value());
  }
};

// Readers hold the cache they were given (use_global_cache). Resizing used to
// swap in a new cache, leaving them on the old one while the manager reported
// the new, empty one.
TEST_F(GlobalCacheManagerTest, ResizesTheCacheReadersAlreadyHold) {
  auto& manager = GlobalCacheManager::Instance();
  manager.SetCache(LRUTileCache::Create(1000).value());
  const std::shared_ptr<ITileCache> held = manager.GetCache();
  for (uint32_t x = 0; x < 4; ++x) {
    held->Put(Key(x), Tile(100));
  }

  ASSERT_TRUE(manager.SetCapacityBytes(250).ok());

  EXPECT_EQ(manager.GetCache().get(), held.get());
  EXPECT_EQ(held->GetCapacityBytes(), 250U);
  EXPECT_EQ(manager.GetCapacityBytes(), 250U);
  // The two most recently stored tiles still fit; the older two were evicted.
  EXPECT_EQ(held->GetSize(), 2U);
  EXPECT_NE(held->Get(Key(3)), nullptr);
  EXPECT_EQ(held->Get(Key(0)), nullptr);
  // The manager's stats describe the cache the reader uses.
  EXPECT_EQ(manager.GetStats().hits, held->GetStats().hits);
  EXPECT_EQ(manager.GetStats().misses, held->GetStats().misses);
}

TEST_F(GlobalCacheManagerTest, GrowingKeepsTheCachedTiles) {
  auto& manager = GlobalCacheManager::Instance();
  manager.SetCache(LRUTileCache::Create(1000).value());
  const std::shared_ptr<ITileCache> held = manager.GetCache();
  held->Put(Key(0), Tile(100));
  held->Put(Key(1), Tile(100));

  ASSERT_TRUE(manager.SetCapacityBytes(4000).ok());

  EXPECT_EQ(manager.GetCache().get(), held.get());
  EXPECT_EQ(held->GetCapacityBytes(), 4000U);
  EXPECT_EQ(held->GetSize(), 2U);
}

TEST_F(GlobalCacheManagerTest, RejectsZeroCapacity) {
  auto& manager = GlobalCacheManager::Instance();
  manager.SetCache(LRUTileCache::Create(1000).value());

  EXPECT_FALSE(manager.SetCapacityBytes(0).ok());
  EXPECT_EQ(manager.GetCapacityBytes(), 1000U);
}

// A custom cache that keeps ITileCache's default SetCapacityBytes.
class FixedSizeCache : public ITileCache {
 public:
  std::shared_ptr<CachedTileData> Get(const TileKey&) override {
    return nullptr;
  }

  void Put(const TileKey&, std::shared_ptr<CachedTileData>) override {}

  void Clear() override {}

  [[nodiscard]] size_t GetSize() const override { return 0; }

  [[nodiscard]] size_t GetCapacityBytes() const override { return 42; }

  [[nodiscard]] size_t GetMemoryUsage() const override { return 0; }

  [[nodiscard]] Stats GetStats() const override { return Stats{}; }
};

TEST_F(GlobalCacheManagerTest, ReportsACustomCacheThatCannotBeResized) {
  auto& manager = GlobalCacheManager::Instance();
  const auto custom = std::make_shared<FixedSizeCache>();
  manager.SetCache(custom);

  const auto status = manager.SetCapacityBytes(500);

  EXPECT_EQ(status.code(), aifocore::StatusCode::kUnimplemented);
  // The cache readers hold stays the manager's cache.
  EXPECT_EQ(manager.GetCache().get(), custom.get());
}

}  // namespace
}  // namespace fastslide::runtime
