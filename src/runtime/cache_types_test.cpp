// Copyright 2025 Jonas Teuwen. All Rights Reserved.
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

/// @file cache_types_test.cpp
/// @brief Tests for cache domain types (TileKey, CachedTileData) and stub implementations
///
/// This file tests:
/// 1. TileKey construction, equality, and field differentiation
/// 2. CachedTileData construction and memory usage
/// 3. Stub ITileCache implementations for dependency injection testing:
///    - SimpleCacheStub: Basic map-based cache (no eviction)
///    - AlwaysMissCacheStub: Always returns nullptr (tests cache-bypass paths)
///    - AlwaysHitCacheStub: Always returns tile (tests warm-cache paths)
///
/// These stubs can be reused in other tests to isolate cache behavior from
/// reader logic.

#include "fastslide/runtime/cache_interface.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fastslide {
namespace runtime {

// ============================================================================
// Stub ITileCache Implementations
// ============================================================================
// These stubs are used throughout the test suite to inject cache behavior
// without requiring a full LRU cache implementation.

/// @brief Simple in-memory cache (no eviction) for testing
class SimpleCacheStub : public ITileCache {
 public:
  std::shared_ptr<CachedTileData> Get(const TileKey& key) override {
    auto it = tiles_.find(KeyToString(key));
    if (it != tiles_.end()) {
      hits_++;
      return it->second;
    }
    misses_++;
    return nullptr;
  }

  void Put(const TileKey& key, std::shared_ptr<CachedTileData> tile) override {
    if (!tile)
      return;  // Ignore null tiles
    tiles_[KeyToString(key)] = tile;
  }

  void Clear() override {
    tiles_.clear();
    hits_ = 0;
    misses_ = 0;
  }

  size_t GetSize() const override { return tiles_.size(); }

  size_t GetCapacity() const override { return 1000; }

  size_t GetMemoryUsage() const override {
    size_t total = 0;
    for (const auto& [key, tile] : tiles_) {
      total += tile->GetMemoryUsage();
    }
    return total;
  }

  Stats GetStats() const override {
    Stats stats;
    stats.capacity = GetCapacity();
    stats.size = GetSize();
    stats.hits = hits_;
    stats.misses = misses_;
    stats.hit_ratio = (hits_ + misses_) > 0
                          ? static_cast<double>(hits_) / (hits_ + misses_)
                          : 0.0;
    stats.memory_usage_bytes = GetMemoryUsage();
    return stats;
  }

 private:
  std::string KeyToString(const TileKey& key) const {
    return key.filename + "_" + std::to_string(key.level) + "_" +
           std::to_string(key.tile_x) + "_" + std::to_string(key.tile_y);
  }

  std::unordered_map<std::string, std::shared_ptr<CachedTileData>> tiles_;
  mutable size_t hits_ = 0;
  mutable size_t misses_ = 0;
};

/// @brief Always-miss cache stub for testing cache-bypass paths
class AlwaysMissCacheStub : public ITileCache {
 public:
  std::shared_ptr<CachedTileData> Get(const TileKey& key) override {
    misses_++;
    return nullptr;
  }

  void Put(const TileKey& key, std::shared_ptr<CachedTileData> tile) override {
    put_count_++;
  }

  void Clear() override {
    misses_ = 0;
    put_count_ = 0;
  }

  size_t GetSize() const override { return 0; }

  size_t GetCapacity() const override { return 100; }

  size_t GetMemoryUsage() const override { return 0; }

  Stats GetStats() const override {
    Stats stats;
    stats.capacity = 100;
    stats.size = 0;
    stats.hits = 0;
    stats.misses = misses_;
    stats.hit_ratio = 0.0;
    stats.memory_usage_bytes = 0;
    return stats;
  }

  size_t GetMissCount() const { return misses_; }

  size_t GetPutCount() const { return put_count_; }

 private:
  mutable size_t misses_ = 0;
  size_t put_count_ = 0;
};

/// @brief Always-hit cache stub (returns same tile for all keys)
class AlwaysHitCacheStub : public ITileCache {
 public:
  AlwaysHitCacheStub() {
    // Create a dummy tile
    std::vector<uint8_t> data(256 * 256 * 3, 128);
    dummy_tile_ = std::make_shared<CachedTileData>(
        std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);
  }

  std::shared_ptr<CachedTileData> Get(const TileKey& key) override {
    hits_++;
    return dummy_tile_;
  }

  void Put(const TileKey& key, std::shared_ptr<CachedTileData> tile) override {
    put_count_++;
  }

  void Clear() override { hits_ = 0; }

  size_t GetSize() const override { return 1; }

  size_t GetCapacity() const override { return 100; }

  size_t GetMemoryUsage() const override {
    return dummy_tile_ ? dummy_tile_->GetMemoryUsage() : 0;
  }

  Stats GetStats() const override {
    Stats stats;
    stats.capacity = 100;
    stats.size = 1;
    stats.hits = hits_;
    stats.misses = 0;
    stats.hit_ratio = 1.0;
    stats.memory_usage_bytes = GetMemoryUsage();
    return stats;
  }

  size_t GetHitCount() const { return hits_; }

 private:
  std::shared_ptr<CachedTileData> dummy_tile_;
  mutable size_t hits_ = 0;
  size_t put_count_ = 0;
};

// ============================================================================
// TileKey Tests
// ============================================================================

TEST(TileKeyTest, DefaultConstruction) {
  TileKey key;

  EXPECT_TRUE(key.filename.empty());
  EXPECT_EQ(key.level, 0);
  EXPECT_EQ(key.tile_x, 0);
  EXPECT_EQ(key.tile_y, 0);
}

TEST(TileKeyTest, ConstructionWithValues) {
  TileKey key{"test.mrxs", 2, 100, 200};

  EXPECT_EQ(key.filename, "test.mrxs");
  EXPECT_EQ(key.level, 2);
  EXPECT_EQ(key.tile_x, 100);
  EXPECT_EQ(key.tile_y, 200);
}

TEST(TileKeyTest, Equality) {
  TileKey key1{"test.mrxs", 0, 10, 20};
  TileKey key2{"test.mrxs", 0, 10, 20};
  TileKey key3{"test.mrxs", 0, 10, 21};  // Different y

  EXPECT_TRUE(key1 == key2);
  EXPECT_FALSE(key1 == key3);
  EXPECT_FALSE(key1 != key2);
  EXPECT_TRUE(key1 != key3);
}

TEST(TileKeyTest, DifferentFields) {
  TileKey base{"file.mrxs", 0, 10, 20};

  TileKey diff_file{"other.mrxs", 0, 10, 20};
  TileKey diff_level{"file.mrxs", 1, 10, 20};
  TileKey diff_x{"file.mrxs", 0, 11, 20};
  TileKey diff_y{"file.mrxs", 0, 10, 21};

  EXPECT_NE(base, diff_file);
  EXPECT_NE(base, diff_level);
  EXPECT_NE(base, diff_x);
  EXPECT_NE(base, diff_y);
}

// ============================================================================
// CachedTileData Tests
// ============================================================================

TEST(CachedTileDataTest, DefaultConstruction) {
  CachedTileData tile;

  EXPECT_TRUE(tile.data.empty());
  EXPECT_EQ(tile.size[0], 0);
  EXPECT_EQ(tile.size[1], 0);
  EXPECT_EQ(tile.channels, 0);
  EXPECT_EQ(tile.GetMemoryUsage(), 0);
}

TEST(CachedTileDataTest, ConstructionWithData) {
  std::vector<uint8_t> data(256 * 256 * 3, 42);
  CachedTileData tile(std::move(data), aifocore::Size<uint32_t, 2>{256, 256},
                      3);

  EXPECT_EQ(tile.data.size(), 256 * 256 * 3);
  EXPECT_EQ(tile.size[0], 256);
  EXPECT_EQ(tile.size[1], 256);
  EXPECT_EQ(tile.channels, 3);
  EXPECT_EQ(tile.GetMemoryUsage(), 256 * 256 * 3);
  EXPECT_EQ(tile.data[0], 42);
}

TEST(CachedTileDataTest, LargeTile) {
  std::vector<uint8_t> data(1024 * 1024 * 16, 255);
  CachedTileData tile(std::move(data), aifocore::Size<uint32_t, 2>{1024, 1024},
                      16);

  EXPECT_EQ(tile.GetMemoryUsage(), 1024 * 1024 * 16);
  EXPECT_EQ(tile.channels, 16);
}

TEST(CachedTileDataTest, EmptyTile) {
  std::vector<uint8_t> data;
  CachedTileData tile(std::move(data), aifocore::Size<uint32_t, 2>{0, 0}, 0);

  EXPECT_TRUE(tile.data.empty());
  EXPECT_EQ(tile.GetMemoryUsage(), 0);
}

// ============================================================================
// SimpleCacheStub Tests
// ============================================================================

TEST(SimpleCacheStubTest, BasicOperations) {
  SimpleCacheStub cache;

  TileKey key{"test.mrxs", 0, 10, 20};
  std::vector<uint8_t> data(256 * 256 * 3, 128);
  auto tile = std::make_shared<CachedTileData>(
      std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);

  // Initially empty
  EXPECT_EQ(cache.GetSize(), 0);
  EXPECT_EQ(cache.Get(key), nullptr);

  // Put tile
  cache.Put(key, tile);
  EXPECT_EQ(cache.GetSize(), 1);

  // Get tile
  auto retrieved = cache.Get(key);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->size[0], 256);
  EXPECT_EQ(retrieved->data.size(), 256 * 256 * 3);
}

TEST(SimpleCacheStubTest, Statistics) {
  SimpleCacheStub cache;

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};

  std::vector<uint8_t> data(256 * 256 * 3);
  auto tile = std::make_shared<CachedTileData>(
      std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);

  cache.Put(key1, tile);

  // Hit
  cache.Get(key1);

  // Miss
  cache.Get(key2);

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.hits, 1);
  EXPECT_EQ(stats.misses, 1);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 0.5);
}

TEST(SimpleCacheStubTest, NoEviction) {
  SimpleCacheStub cache;

  // Add many tiles (no eviction in this stub)
  for (uint32_t i = 0; i < 2000; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    std::vector<uint8_t> data(100);
    auto tile = std::make_shared<CachedTileData>(
        std::move(data), aifocore::Size<uint32_t, 2>{10, 10}, 1);
    cache.Put(key, tile);
  }

  // All tiles should be present (no LRU eviction)
  EXPECT_EQ(cache.GetSize(), 2000);
}

// ============================================================================
// AlwaysMissCacheStub Tests
// ============================================================================

TEST(AlwaysMissCacheStubTest, AlwaysReturnsNull) {
  AlwaysMissCacheStub cache;

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};

  std::vector<uint8_t> data(256 * 256 * 3);
  auto tile = std::make_shared<CachedTileData>(
      std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);

  // Put tiles
  cache.Put(key1, tile);
  cache.Put(key2, tile);

  EXPECT_EQ(cache.GetPutCount(), 2);

  // Get always returns null
  EXPECT_EQ(cache.Get(key1), nullptr);
  EXPECT_EQ(cache.Get(key2), nullptr);

  EXPECT_EQ(cache.GetMissCount(), 2);
}

TEST(AlwaysMissCacheStubTest, SimulatesCacheBypass) {
  AlwaysMissCacheStub cache;

  // Simulate reader behavior when cache always misses
  std::vector<TileKey> keys;
  for (uint32_t i = 0; i < 10; ++i) {
    keys.push_back(TileKey{"bypass.mrxs", 0, i, 0});
  }

  // Reader checks cache, always misses
  for (const auto& key : keys) {
    auto tile = cache.Get(key);
    if (!tile) {
      // Simulate decoding and putting in cache
      std::vector<uint8_t> decoded(256 * 256 * 3, 42);
      auto new_tile = std::make_shared<CachedTileData>(
          std::move(decoded), aifocore::Size<uint32_t, 2>{256, 256}, 3);
      cache.Put(key, new_tile);
    }
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.misses, 10);
  EXPECT_EQ(stats.hits, 0);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 0.0);
  EXPECT_EQ(cache.GetPutCount(), 10);
}

// ============================================================================
// AlwaysHitCacheStub Tests
// ============================================================================

TEST(AlwaysHitCacheStubTest, AlwaysReturnsTile) {
  AlwaysHitCacheStub cache;

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};
  TileKey key3{"other.mrxs", 1, 50, 100};

  // All gets return tile (even without Put)
  EXPECT_NE(cache.Get(key1), nullptr);
  EXPECT_NE(cache.Get(key2), nullptr);
  EXPECT_NE(cache.Get(key3), nullptr);

  EXPECT_EQ(cache.GetHitCount(), 3);
}

TEST(AlwaysHitCacheStubTest, SimulatesWarmCache) {
  AlwaysHitCacheStub cache;

  // Simulate reader behavior when cache is fully warmed
  for (uint32_t i = 0; i < 100; ++i) {
    TileKey key{"warm.mrxs", 0, i, 0};
    auto tile = cache.Get(key);

    ASSERT_NE(tile, nullptr);
    // Reader would use tile directly (no decoding needed)
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.hits, 100);
  EXPECT_EQ(stats.misses, 0);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 1.0);
}

// ============================================================================
// Cross-Cache Behavior Tests
// ============================================================================

TEST(CacheInterfaceTest, CompareStubImplementations) {
  SimpleCacheStub simple_cache;
  AlwaysMissCacheStub miss_cache;
  AlwaysHitCacheStub hit_cache;

  TileKey key{"compare.mrxs", 0, 0, 0};
  std::vector<uint8_t> data(256 * 256 * 3, 42);
  auto tile = std::make_shared<CachedTileData>(
      std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);

  // Put in all caches
  simple_cache.Put(key, tile);
  miss_cache.Put(key, tile);
  hit_cache.Put(key, tile);

  // Get from all caches
  auto simple_result = simple_cache.Get(key);
  auto miss_result = miss_cache.Get(key);
  auto hit_result = hit_cache.Get(key);

  // Different behaviors
  EXPECT_NE(simple_result, nullptr);  // Simple cache returns the tile
  EXPECT_EQ(miss_result, nullptr);    // Miss cache always returns null
  EXPECT_NE(hit_result, nullptr);     // Hit cache always returns tile
}

TEST(CacheInterfaceTest, PolymorphicUsage) {
  std::vector<std::shared_ptr<ITileCache>> caches;

  caches.push_back(std::make_shared<SimpleCacheStub>());
  caches.push_back(std::make_shared<AlwaysMissCacheStub>());
  caches.push_back(std::make_shared<AlwaysHitCacheStub>());

  // All caches support the same interface
  for (auto& cache : caches) {
    EXPECT_GT(cache->GetCapacity(), 0);  // Has some capacity

    TileKey key{"poly.mrxs", 0, 0, 0};
    std::vector<uint8_t> data(256 * 256 * 3);
    auto tile = std::make_shared<CachedTileData>(
        std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);

    cache->Put(key, tile);

    auto stats = cache->GetStats();
    EXPECT_GT(stats.capacity, 0);
  }
}

// ============================================================================
// Memory Usage Tests
// ============================================================================

TEST(CachedTileDataTest, MemoryUsageAccuracy) {
  std::vector<std::pair<uint32_t, uint32_t>> tile_sizes = {
      {256, 256},    // 192 KB
      {512, 512},    // 768 KB
      {1024, 1024},  // 3 MB
      {128, 128},    // 48 KB
  };

  for (const auto& [width, height] : tile_sizes) {
    std::vector<uint8_t> data(width * height * 3, 42);
    size_t expected_bytes = data.size();

    CachedTileData tile(std::move(data),
                        aifocore::Size<uint32_t, 2>{width, height}, 3);

    EXPECT_EQ(tile.GetMemoryUsage(), expected_bytes);
  }
}

TEST(CachedTileDataTest, SpectralImagingTile) {
  // 16-channel spectral tile
  uint32_t width = 512;
  uint32_t height = 512;
  uint32_t channels = 16;

  std::vector<uint8_t> data(width * height * channels, 128);
  CachedTileData tile(std::move(data),
                      aifocore::Size<uint32_t, 2>{width, height}, channels);

  EXPECT_EQ(tile.GetMemoryUsage(), width * height * channels);
  EXPECT_EQ(tile.channels, 16);
}

// ============================================================================
// Stub Cache Usage Patterns
// ============================================================================

TEST(CacheStubTest, TestCacheMissPath) {
  AlwaysMissCacheStub cache;

  // Simulate reader accessing tiles
  int decode_count = 0;

  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};

    auto cached = cache.Get(key);
    if (!cached) {
      // Decode path (always taken with AlwaysMissCacheStub)
      decode_count++;

      std::vector<uint8_t> decoded(256 * 256 * 3);
      auto tile = std::make_shared<CachedTileData>(
          std::move(decoded), aifocore::Size<uint32_t, 2>{256, 256}, 3);
      cache.Put(key, tile);
    }
  }

  EXPECT_EQ(decode_count, 10);  // All tiles decoded
  EXPECT_EQ(cache.GetMissCount(), 10);
}

TEST(CacheStubTest, TestCacheHitPath) {
  AlwaysHitCacheStub cache;

  // Simulate reader accessing tiles
  int decode_count = 0;

  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};

    auto cached = cache.Get(key);
    if (!cached) {
      // Decode path (never taken with AlwaysHitCacheStub)
      decode_count++;
    }
  }

  EXPECT_EQ(decode_count, 0);  // No tiles decoded
  EXPECT_EQ(cache.GetHitCount(), 10);
}

TEST(CacheStubTest, MixedCacheBehavior) {
  SimpleCacheStub cache;

  // Add some tiles
  std::vector<uint8_t> data(256 * 256 * 3, 42);
  auto tile = std::make_shared<CachedTileData>(
      std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);

  for (uint32_t i = 0; i < 5; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, tile);
  }

  // Access pattern: some hits, some misses
  int hits = 0;
  int misses = 0;

  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    auto result = cache.Get(key);

    if (result) {
      hits++;
    } else {
      misses++;
    }
  }

  EXPECT_EQ(hits, 5);    // First 5 tiles cached
  EXPECT_EQ(misses, 5);  // Last 5 not in cache
}

}  // namespace runtime
}  // namespace fastslide
