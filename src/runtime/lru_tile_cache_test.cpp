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

#include "fastslide/runtime/lru_tile_cache.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide {
namespace runtime {

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Create test tile data
std::shared_ptr<CachedTileData> CreateTestTile(uint32_t width, uint32_t height,
                                               uint32_t channels) {
  std::vector<uint8_t> data(width * height * channels, 42);
  return std::make_shared<CachedTileData>(
      std::move(data), aifocore::Size<uint32_t, 2>{width, height}, channels);
}

// ============================================================================
// LRUTileCache Construction Tests
// ============================================================================

TEST(LRUTileCacheTest, CreateWithDefaultCapacity) {
  auto result = LRUTileCache::Create();
  ASSERT_TRUE(result.ok()) << result.status();

  auto cache = result.value();
  EXPECT_EQ(cache->GetCapacity(), 1000);
  EXPECT_EQ(cache->GetSize(), 0);
}

TEST(LRUTileCacheTest, CreateWithCustomCapacity) {
  auto result = LRUTileCache::Create(500);
  ASSERT_TRUE(result.ok()) << result.status();

  auto cache = result.value();
  EXPECT_EQ(cache->GetCapacity(), 500);
  EXPECT_EQ(cache->GetSize(), 0);
}

TEST(LRUTileCacheTest, DirectConstruction) {
  LRUTileCache cache(250);

  EXPECT_EQ(cache.GetCapacity(), 250);
  EXPECT_EQ(cache.GetSize(), 0);
}

// ============================================================================
// Basic Cache Operations Tests
// ============================================================================

TEST(LRUTileCacheTest, PutAndGet) {
  LRUTileCache cache(100);

  TileKey key{"test.mrxs", 0, 10, 20};
  auto tile = CreateTestTile(256, 256, 3);

  cache.Put(key, tile);

  auto result = cache.Get(key);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->size[0], 256);
  EXPECT_EQ(result->size[1], 256);
  EXPECT_EQ(result->channels, 3);
}

TEST(LRUTileCacheTest, GetNonExistentKey) {
  LRUTileCache cache(100);

  TileKey key{"test.mrxs", 0, 10, 20};
  auto result = cache.Get(key);

  EXPECT_EQ(result, nullptr);
}

TEST(LRUTileCacheTest, PutMultipleTiles) {
  LRUTileCache cache(100);

  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    auto tile = CreateTestTile(256, 256, 3);
    cache.Put(key, tile);
  }

  EXPECT_EQ(cache.GetSize(), 10);

  // Verify all tiles are retrievable
  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    auto result = cache.Get(key);
    EXPECT_NE(result, nullptr);
  }
}

TEST(LRUTileCacheTest, OverwriteExistingKey) {
  LRUTileCache cache(100);

  TileKey key{"test.mrxs", 0, 10, 20};

  auto tile1 = CreateTestTile(256, 256, 3);
  cache.Put(key, tile1);

  auto tile2 = CreateTestTile(512, 512, 4);
  cache.Put(key, tile2);

  // Should have new tile
  auto result = cache.Get(key);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->size[0], 512);
  EXPECT_EQ(result->size[1], 512);
  EXPECT_EQ(result->channels, 4);

  // Size should still be 1 (not 2)
  EXPECT_EQ(cache.GetSize(), 1);
}

// ============================================================================
// LRU Eviction Tests
// ============================================================================

TEST(LRUTileCacheTest, EvictLeastRecentlyUsed) {
  LRUTileCache cache(3);  // Small capacity

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};
  TileKey key3{"test.mrxs", 0, 2, 0};
  TileKey key4{"test.mrxs", 0, 3, 0};

  // Fill cache to capacity
  cache.Put(key1, CreateTestTile(256, 256, 3));
  cache.Put(key2, CreateTestTile(256, 256, 3));
  cache.Put(key3, CreateTestTile(256, 256, 3));

  EXPECT_EQ(cache.GetSize(), 3);

  // Add one more (should evict key1)
  cache.Put(key4, CreateTestTile(256, 256, 3));

  EXPECT_EQ(cache.GetSize(), 3);
  EXPECT_EQ(cache.Get(key1), nullptr);  // Evicted
  EXPECT_NE(cache.Get(key2), nullptr);
  EXPECT_NE(cache.Get(key3), nullptr);
  EXPECT_NE(cache.Get(key4), nullptr);
}

TEST(LRUTileCacheTest, AccessUpdatesLRU) {
  LRUTileCache cache(3);

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};
  TileKey key3{"test.mrxs", 0, 2, 0};
  TileKey key4{"test.mrxs", 0, 3, 0};

  cache.Put(key1, CreateTestTile(256, 256, 3));
  cache.Put(key2, CreateTestTile(256, 256, 3));
  cache.Put(key3, CreateTestTile(256, 256, 3));

  // Access key1 to make it most recent
  cache.Get(key1);

  // Add key4 (should evict key2, the least recently used)
  cache.Put(key4, CreateTestTile(256, 256, 3));

  EXPECT_NE(cache.Get(key1), nullptr);  // Still present
  EXPECT_EQ(cache.Get(key2), nullptr);  // Evicted
  EXPECT_NE(cache.Get(key3), nullptr);
  EXPECT_NE(cache.Get(key4), nullptr);
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST(LRUTileCacheTest, ClearEmptyCache) {
  LRUTileCache cache(100);

  cache.Clear();

  EXPECT_EQ(cache.GetSize(), 0);
  EXPECT_EQ(cache.GetCapacity(), 100);  // Capacity unchanged
}

TEST(LRUTileCacheTest, ClearPopulatedCache) {
  LRUTileCache cache(100);

  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  EXPECT_EQ(cache.GetSize(), 10);

  cache.Clear();

  EXPECT_EQ(cache.GetSize(), 0);

  // Verify all keys are gone
  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    EXPECT_EQ(cache.Get(key), nullptr);
  }
}

// ============================================================================
// Capacity Resizing Tests
// ============================================================================

TEST(LRUTileCacheTest, SetCapacityLarger) {
  LRUTileCache cache(10);

  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  EXPECT_EQ(cache.GetSize(), 10);

  auto status = cache.SetCapacity(20);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(cache.GetCapacity(), 20);
  EXPECT_EQ(cache.GetSize(), 0);  // Cleared during resize
}

TEST(LRUTileCacheTest, SetCapacitySmaller) {
  LRUTileCache cache(100);

  for (uint32_t i = 0; i < 50; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  EXPECT_EQ(cache.GetSize(), 50);

  auto status = cache.SetCapacity(10);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(cache.GetCapacity(), 10);
  EXPECT_EQ(cache.GetSize(), 0);  // Cleared during resize
}

TEST(LRUTileCacheTest, SetCapacityZero) {
  LRUTileCache cache(100);

  auto status = cache.SetCapacity(0);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);

  // Capacity unchanged
  EXPECT_EQ(cache.GetCapacity(), 100);
}

TEST(LRUTileCacheTest, SetCapacitySameValue) {
  LRUTileCache cache(100);

  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  auto status = cache.SetCapacity(100);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(cache.GetCapacity(), 100);
  EXPECT_EQ(cache.GetSize(), 0);  // Still cleared
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST(LRUTileCacheTest, GetStatsEmpty) {
  LRUTileCache cache(100);

  auto stats = cache.GetStats();

  EXPECT_EQ(stats.capacity, 100);
  EXPECT_EQ(stats.size, 0);
  EXPECT_EQ(stats.hits, 0);
  EXPECT_EQ(stats.misses, 0);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 0.0);
  EXPECT_EQ(stats.memory_usage_bytes, 0);
}

TEST(LRUTileCacheTest, GetStatsWithTiles) {
  LRUTileCache cache(100);

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};

  cache.Put(key1, CreateTestTile(256, 256, 3));
  cache.Put(key2, CreateTestTile(256, 256, 3));

  auto stats = cache.GetStats();

  EXPECT_EQ(stats.capacity, 100);
  EXPECT_EQ(stats.size, 2);
  EXPECT_GT(stats.memory_usage_bytes, 0);
}

TEST(LRUTileCacheTest, HitRatioTracking) {
  LRUTileCache cache(100);

  TileKey key{"test.mrxs", 0, 0, 0};
  cache.Put(key, CreateTestTile(256, 256, 3));

  // Hit
  cache.Get(key);

  // Miss
  TileKey missing_key{"test.mrxs", 0, 1, 0};
  cache.Get(missing_key);

  auto stats = cache.GetStats();

  EXPECT_EQ(stats.hits, 1);
  EXPECT_EQ(stats.misses, 1);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 0.5);
}

TEST(LRUTileCacheTest, MemoryUsageCalculation) {
  LRUTileCache cache(100);

  TileKey key{"test.mrxs", 0, 0, 0};
  auto tile = CreateTestTile(256, 256, 3);
  size_t expected_size = tile->data.size();

  cache.Put(key, tile);

  EXPECT_EQ(cache.GetMemoryUsage(), expected_size);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(LRUTileCacheTest, ConcurrentPuts) {
  LRUTileCache cache(1000);

  std::vector<std::thread> threads;
  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&cache, t]() {
      for (uint32_t i = 0; i < 100; ++i) {
        TileKey key{"test.mrxs", 0, static_cast<uint32_t>(t * 100 + i), 0};
        cache.Put(key, CreateTestTile(256, 256, 3));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Should have 1000 tiles (up to capacity)
  EXPECT_EQ(cache.GetSize(), 1000);
}

TEST(LRUTileCacheTest, ConcurrentGetsAndPuts) {
  LRUTileCache cache(500);

  // Pre-populate
  for (uint32_t i = 0; i < 100; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  std::vector<std::thread> threads;

  // Readers
  for (int t = 0; t < 5; ++t) {
    threads.emplace_back([&cache]() {
      for (uint32_t i = 0; i < 100; ++i) {
        TileKey key{"test.mrxs", 0, i % 100, 0};
        cache.Get(key);
      }
    });
  }

  // Writers
  for (int t = 0; t < 5; ++t) {
    threads.emplace_back([&cache, t]() {
      for (uint32_t i = 0; i < 100; ++i) {
        TileKey key{"test.mrxs", 0, static_cast<uint32_t>(100 + t * 100 + i),
                    0};
        cache.Put(key, CreateTestTile(256, 256, 3));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Cache should be at capacity
  EXPECT_EQ(cache.GetSize(), 500);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(LRUTileCacheTest, CapacityOne) {
  LRUTileCache cache(1);

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};

  cache.Put(key1, CreateTestTile(256, 256, 3));
  EXPECT_EQ(cache.GetSize(), 1);

  cache.Put(key2, CreateTestTile(256, 256, 3));
  EXPECT_EQ(cache.GetSize(), 1);

  // key1 should be evicted
  EXPECT_EQ(cache.Get(key1), nullptr);
  EXPECT_NE(cache.Get(key2), nullptr);
}

TEST(LRUTileCacheTest, LargeCapacity) {
  LRUTileCache cache(100000);

  EXPECT_EQ(cache.GetCapacity(), 100000);
  EXPECT_EQ(cache.GetSize(), 0);
}

TEST(LRUTileCacheTest, DifferentTileSizes) {
  LRUTileCache cache(100);

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};

  cache.Put(key1, CreateTestTile(256, 256, 3));  // 256*256*3 bytes
  cache.Put(key2, CreateTestTile(512, 512, 4));  // 512*512*4 bytes

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.size, 2);

  size_t expected_memory = (256 * 256 * 3) + (512 * 512 * 4);
  EXPECT_EQ(stats.memory_usage_bytes, expected_memory);
}

TEST(LRUTileCacheTest, SameKeyDifferentSlides) {
  LRUTileCache cache(100);

  TileKey key1{"slide1.mrxs", 0, 10, 20};
  TileKey key2{"slide2.mrxs", 0, 10, 20};

  cache.Put(key1, CreateTestTile(256, 256, 3));
  cache.Put(key2, CreateTestTile(256, 256, 3));

  // Both should be present (different filenames)
  EXPECT_NE(cache.Get(key1), nullptr);
  EXPECT_NE(cache.Get(key2), nullptr);
  EXPECT_EQ(cache.GetSize(), 2);
}

TEST(LRUTileCacheTest, MultiLevel) {
  LRUTileCache cache(100);

  for (uint16_t level = 0; level < 5; ++level) {
    TileKey key{"test.mrxs", level, 10, 20};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // All levels should be cached
  EXPECT_EQ(cache.GetSize(), 5);

  for (uint16_t level = 0; level < 5; ++level) {
    TileKey key{"test.mrxs", level, 10, 20};
    EXPECT_NE(cache.Get(key), nullptr);
  }
}

// ============================================================================
// Edge Cases - Oversized Tiles
// ============================================================================

TEST(LRUTileCacheTest, OversizedTiles) {
  LRUTileCache cache(10);

  // Create a very large tile (10MB)
  std::vector<uint8_t> large_data(10 * 1024 * 1024, 42);
  auto large_tile = std::make_shared<CachedTileData>(
      std::move(large_data), aifocore::Size<uint32_t, 2>{2048, 2048}, 10);

  TileKey key{"large.mrxs", 0, 0, 0};
  cache.Put(key, large_tile);

  EXPECT_EQ(cache.GetSize(), 1);

  auto retrieved = cache.Get(key);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->data.size(), 10 * 1024 * 1024);
  EXPECT_EQ(retrieved->data[0], 42);
}

TEST(LRUTileCacheTest, MixedTileSizes) {
  LRUTileCache cache(10);

  // Small tile (256x256x3)
  TileKey small_key{"test.mrxs", 0, 0, 0};
  cache.Put(small_key, CreateTestTile(256, 256, 3));

  // Medium tile (512x512x4)
  TileKey medium_key{"test.mrxs", 0, 1, 0};
  std::vector<uint8_t> medium_data(512 * 512 * 4, 128);
  auto medium_tile = std::make_shared<CachedTileData>(
      std::move(medium_data), aifocore::Size<uint32_t, 2>{512, 512}, 4);
  cache.Put(medium_key, medium_tile);

  // Large tile (1024x1024x16 for spectral)
  TileKey large_key{"test.qptiff", 0, 0, 0};
  std::vector<uint8_t> large_data(1024 * 1024 * 16, 255);
  auto large_tile = std::make_shared<CachedTileData>(
      std::move(large_data), aifocore::Size<uint32_t, 2>{1024, 1024}, 16);
  cache.Put(large_key, large_tile);

  EXPECT_EQ(cache.GetSize(), 3);

  // Verify memory usage accounts for all tiles
  auto stats = cache.GetStats();
  size_t expected_memory =
      (256 * 256 * 3) + (512 * 512 * 4) + (1024 * 1024 * 16);
  EXPECT_EQ(stats.memory_usage_bytes, expected_memory);
}

TEST(LRUTileCacheTest, OversizedTileEviction) {
  LRUTileCache cache(5);

  // Fill with oversized tiles
  for (uint32_t i = 0; i < 5; ++i) {
    TileKey key{"large.mrxs", 0, i, 0};
    std::vector<uint8_t> data(1024 * 1024, static_cast<uint8_t>(i));
    auto tile = std::make_shared<CachedTileData>(
        std::move(data), aifocore::Size<uint32_t, 2>{1024, 1024}, 1);
    cache.Put(key, tile);
  }

  EXPECT_EQ(cache.GetSize(), 5);
  auto stats = cache.GetStats();
  EXPECT_EQ(stats.memory_usage_bytes, 5 * 1024 * 1024);

  // Add one more - should evict oldest
  TileKey new_key{"large.mrxs", 0, 5, 0};
  std::vector<uint8_t> data(1024 * 1024, 99);
  auto tile = std::make_shared<CachedTileData>(
      std::move(data), aifocore::Size<uint32_t, 2>{1024, 1024}, 1);
  cache.Put(new_key, tile);

  EXPECT_EQ(cache.GetSize(), 5);

  // First tile should be evicted
  TileKey first_key{"large.mrxs", 0, 0, 0};
  EXPECT_EQ(cache.Get(first_key), nullptr);
}

// ============================================================================
// Mixed Cache Hit/Miss Patterns
// ============================================================================

TEST(LRUTileCacheTest, MixedHitMissPattern) {
  LRUTileCache cache(10);

  // Populate cache with some tiles
  for (uint32_t i = 0; i < 5; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // Access pattern: hit, miss, hit, miss, hit
  TileKey hit1{"test.mrxs", 0, 0, 0};    // Hit
  TileKey miss1{"test.mrxs", 0, 10, 0};  // Miss
  TileKey hit2{"test.mrxs", 0, 1, 0};    // Hit
  TileKey miss2{"test.mrxs", 0, 11, 0};  // Miss
  TileKey hit3{"test.mrxs", 0, 2, 0};    // Hit

  cache.Get(hit1);
  cache.Get(miss1);
  cache.Get(hit2);
  cache.Get(miss2);
  cache.Get(hit3);

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.hits, 3);
  EXPECT_EQ(stats.misses, 2);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 0.6);
}

TEST(LRUTileCacheTest, SequentialAccessPattern) {
  LRUTileCache cache(50);

  // Simulate sequential tile reading (common for slide scanning)
  for (uint32_t y = 0; y < 10; ++y) {
    for (uint32_t x = 0; x < 10; ++x) {
      TileKey key{"scan.mrxs", 0, x, y};
      cache.Put(key, CreateTestTile(256, 256, 3));
    }
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.size, 50);  // Capped at capacity

  // Access tiles in reverse order - should be mostly hits
  size_t hits_before = stats.hits;
  for (uint32_t y = 5; y < 10; ++y) {
    for (uint32_t x = 5; x < 10; ++x) {
      TileKey key{"scan.mrxs", 0, x, y};
      cache.Get(key);
    }
  }

  stats = cache.GetStats();
  EXPECT_GT(stats.hits, hits_before);
}

TEST(LRUTileCacheTest, RandomAccessPattern) {
  LRUTileCache cache(20);

  // Populate with tiles
  for (uint32_t i = 0; i < 50; ++i) {
    TileKey key{"random.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // Random access pattern (simulating zoom/pan navigation)
  std::vector<uint32_t> access_pattern = {5, 15, 5, 25, 5, 35, 15, 25, 35, 5};

  for (uint32_t idx : access_pattern) {
    TileKey key{"random.mrxs", 0, idx, 0};
    cache.Get(key);
  }

  auto stats = cache.GetStats();
  // Some hits from repeated access to indices 5, 15, 25, 35
  EXPECT_GT(stats.hits, 0);
  EXPECT_GT(stats.misses, 0);
}

// ============================================================================
// Hash Collision and Key Uniqueness
// ============================================================================

TEST(LRUTileCacheTest, HashCollisionResistance) {
  LRUTileCache cache(1000);

  // Create many keys that might collide
  for (uint32_t i = 0; i < 100; ++i) {
    for (uint16_t level = 0; level < 10; ++level) {
      TileKey key{"test.mrxs", level, i, i};
      cache.Put(key, CreateTestTile(256, 256, 3));
    }
  }

  // Should have all 1000 tiles (no collisions causing overwrites)
  EXPECT_EQ(cache.GetSize(), 1000);

  // Verify all tiles are retrievable
  for (uint32_t i = 0; i < 100; ++i) {
    for (uint16_t level = 0; level < 10; ++level) {
      TileKey key{"test.mrxs", level, i, i};
      EXPECT_NE(cache.Get(key), nullptr);
    }
  }
}

TEST(LRUTileCacheTest, SimilarKeysDifferentiate) {
  LRUTileCache cache(10);

  // Keys that differ only in one field
  TileKey key_file1{"file1.mrxs", 0, 10, 20};
  TileKey key_file2{"file2.mrxs", 0, 10, 20};  // Different file
  TileKey key_level{"file1.mrxs", 1, 10, 20};  // Different level
  TileKey key_x{"file1.mrxs", 0, 11, 20};      // Different x
  TileKey key_y{"file1.mrxs", 0, 10, 21};      // Different y

  cache.Put(key_file1, CreateTestTile(256, 256, 3));
  cache.Put(key_file2, CreateTestTile(256, 256, 3));
  cache.Put(key_level, CreateTestTile(256, 256, 3));
  cache.Put(key_x, CreateTestTile(256, 256, 3));
  cache.Put(key_y, CreateTestTile(256, 256, 3));

  // All should be present (no overwrites)
  EXPECT_EQ(cache.GetSize(), 5);
  EXPECT_NE(cache.Get(key_file1), nullptr);
  EXPECT_NE(cache.Get(key_file2), nullptr);
  EXPECT_NE(cache.Get(key_level), nullptr);
  EXPECT_NE(cache.Get(key_x), nullptr);
  EXPECT_NE(cache.Get(key_y), nullptr);
}

// ============================================================================
// Statistics Accuracy Under Load
// ============================================================================

TEST(LRUTileCacheTest, StatisticsAccuracyWithEvictions) {
  LRUTileCache cache(10);

  // Fill to capacity
  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.size, 10);
  EXPECT_EQ(stats.hits, 0);
  EXPECT_EQ(stats.misses, 0);

  // Access some tiles (hits)
  for (uint32_t i = 0; i < 5; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Get(key);
  }

  // Access non-existent tiles (misses)
  for (uint32_t i = 20; i < 25; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Get(key);
  }

  stats = cache.GetStats();
  EXPECT_EQ(stats.hits, 5);
  EXPECT_EQ(stats.misses, 5);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 0.5);

  // Add more tiles (trigger evictions)
  for (uint32_t i = 100; i < 110; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // Cache should still be at capacity
  EXPECT_EQ(cache.GetSize(), 10);

  // Stats should be preserved across evictions
  stats = cache.GetStats();
  EXPECT_EQ(stats.hits, 5);
  EXPECT_EQ(stats.misses, 5);
}

TEST(LRUTileCacheTest, StatisticsAfterClearPreserveCapacity) {
  LRUTileCache cache(50);

  // Build up stats
  for (uint32_t i = 0; i < 30; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  for (uint32_t i = 0; i < 20; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Get(key);  // Hits
  }

  for (uint32_t i = 50; i < 60; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Get(key);  // Misses
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.size, 30);
  EXPECT_EQ(stats.hits, 20);
  EXPECT_EQ(stats.misses, 10);

  // Clear cache
  cache.Clear();

  stats = cache.GetStats();
  EXPECT_EQ(stats.size, 0);
  EXPECT_EQ(stats.capacity, 50);  // Preserved
  EXPECT_EQ(stats.hits, 0);       // Reset
  EXPECT_EQ(stats.misses, 0);     // Reset
  EXPECT_EQ(stats.hit_ratio, 0.0);
}

// ============================================================================
// Concurrent Eviction Scenarios
// ============================================================================

TEST(LRUTileCacheTest, ConcurrentEvictionPressure) {
  LRUTileCache cache(100);

  // Multiple threads adding tiles, causing evictions
  std::vector<std::thread> threads;
  std::atomic<int> tiles_added{0};

  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&cache, &tiles_added, t]() {
      for (uint32_t i = 0; i < 50; ++i) {
        TileKey key{"test.mrxs", 0, static_cast<uint32_t>(t * 1000 + i), 0};
        cache.Put(key, CreateTestTile(256, 256, 3));
        tiles_added++;
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(tiles_added, 500);      // All puts attempted
  EXPECT_EQ(cache.GetSize(), 100);  // Capped at capacity

  // Cache should be stable
  auto stats = cache.GetStats();
  EXPECT_EQ(stats.capacity, 100);
  EXPECT_EQ(stats.size, 100);
}

TEST(LRUTileCacheTest, ConcurrentAccessDuringEviction) {
  LRUTileCache cache(50);

  // Pre-populate cache
  for (uint32_t i = 0; i < 50; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  std::vector<std::thread> threads;
  std::atomic<int> hits{0};
  std::atomic<int> misses{0};

  // Readers accessing existing tiles
  for (int t = 0; t < 5; ++t) {
    threads.emplace_back([&cache, &hits, &misses]() {
      for (uint32_t i = 0; i < 100; ++i) {
        TileKey key{"test.mrxs", 0, i % 50, 0};
        auto result = cache.Get(key);
        if (result) {
          hits++;
        } else {
          misses++;
        }
      }
    });
  }

  // Writers adding new tiles (causing evictions)
  for (int t = 0; t < 5; ++t) {
    threads.emplace_back([&cache, t]() {
      for (uint32_t i = 0; i < 100; ++i) {
        TileKey key{"test.mrxs", 0, static_cast<uint32_t>(100 + t * 100 + i),
                    0};
        cache.Put(key, CreateTestTile(256, 256, 3));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Should have some hits despite evictions
  EXPECT_GT(hits, 0);
  EXPECT_EQ(cache.GetSize(), 50);
}

// ============================================================================
// Cache Warming Patterns
// ============================================================================

TEST(LRUTileCacheTest, CacheWarmingScenario) {
  LRUTileCache cache(100);

  // Simulate cache warming - preload common tiles
  std::vector<TileKey> hot_tiles;
  for (uint32_t i = 0; i < 20; ++i) {
    TileKey key{"hotspot.mrxs", 0, i / 5, i % 5};  // 4x5 region
    hot_tiles.push_back(key);
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // Add many other tiles
  for (uint32_t i = 0; i < 200; ++i) {
    TileKey key{"large.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // Hot tiles may have been evicted
  int hot_tiles_remaining = 0;
  for (const auto& key : hot_tiles) {
    if (cache.Get(key) != nullptr) {
      hot_tiles_remaining++;
    }
  }

  // Not all hot tiles will remain (they're oldest)
  EXPECT_LT(hot_tiles_remaining, 20);
}

TEST(LRUTileCacheTest, RepeatedAccessKeepsInCache) {
  LRUTileCache cache(10);

  TileKey frequent_key{"frequent.mrxs", 0, 5, 5};
  cache.Put(frequent_key, CreateTestTile(256, 256, 3));

  // Access this key frequently while adding others
  for (uint32_t i = 0; i < 50; ++i) {
    // Access frequent key
    cache.Get(frequent_key);

    // Add new tile
    TileKey new_key{"test.mrxs", 0, i, 0};
    cache.Put(new_key, CreateTestTile(256, 256, 3));
  }

  // Frequent key should still be in cache
  EXPECT_NE(cache.Get(frequent_key), nullptr);
}

// ============================================================================
// Null Tile Handling
// ============================================================================

TEST(LRUTileCacheTest, NullTileRejection) {
  LRUTileCache cache(10);

  TileKey key{"test.mrxs", 0, 0, 0};
  cache.Put(key, nullptr);

  // Null tiles should not be added
  EXPECT_EQ(cache.GetSize(), 0);
  EXPECT_EQ(cache.Get(key), nullptr);
}

TEST(LRUTileCacheTest, MixedNullAndValidTiles) {
  LRUTileCache cache(10);

  TileKey key1{"test.mrxs", 0, 0, 0};
  TileKey key2{"test.mrxs", 0, 1, 0};
  TileKey key3{"test.mrxs", 0, 2, 0};

  cache.Put(key1, CreateTestTile(256, 256, 3));  // Valid
  cache.Put(key2, nullptr);                      // Null
  cache.Put(key3, CreateTestTile(256, 256, 3));  // Valid

  EXPECT_EQ(cache.GetSize(), 2);  // Only valid tiles
  EXPECT_NE(cache.Get(key1), nullptr);
  EXPECT_EQ(cache.Get(key2), nullptr);
  EXPECT_NE(cache.Get(key3), nullptr);
}

// ============================================================================
// Memory Pressure Scenarios
// ============================================================================

TEST(LRUTileCacheTest, HighMemoryPressure) {
  LRUTileCache cache(100);

  // Fill with large tiles
  for (uint32_t i = 0; i < 100; ++i) {
    TileKey key{"large.qptiff", 0, i, 0};
    std::vector<uint8_t> data(512 * 512 * 16, static_cast<uint8_t>(i));
    auto tile = std::make_shared<CachedTileData>(
        std::move(data), aifocore::Size<uint32_t, 2>{512, 512}, 16);
    cache.Put(key, tile);
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.size, 100);
  EXPECT_EQ(stats.memory_usage_bytes, 100 * 512 * 512 * 16);

  // Should be ~400MB total
  EXPECT_GE(stats.memory_usage_bytes, 400 * 1024 * 1024);
}

TEST(LRUTileCacheTest, MemoryTrackingAccuracy) {
  LRUTileCache cache(100);

  size_t total_bytes = 0;

  // Add tiles of varying sizes
  std::vector<std::pair<uint32_t, uint32_t>> tile_sizes = {
      {256, 256}, {512, 512}, {1024, 1024}, {128, 128}, {64, 64}};

  for (size_t i = 0; i < tile_sizes.size(); ++i) {
    auto [w, h] = tile_sizes[i];
    uint32_t channels = 3;
    size_t bytes = w * h * channels;

    TileKey key{"test.mrxs", 0, static_cast<uint32_t>(i), 0};
    std::vector<uint8_t> data(bytes, 42);
    auto tile = std::make_shared<CachedTileData>(
        std::move(data), aifocore::Size<uint32_t, 2>{w, h}, channels);

    cache.Put(key, tile);
    total_bytes += bytes;
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.memory_usage_bytes, total_bytes);
}

// ============================================================================
// Capacity Edge Cases
// ============================================================================

TEST(LRUTileCacheTest, VeryLargeCapacity) {
  LRUTileCache cache(1000000);  // 1 million tiles

  EXPECT_EQ(cache.GetCapacity(), 1000000);

  // Add some tiles
  for (uint32_t i = 0; i < 100; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  EXPECT_EQ(cache.GetSize(), 100);
}

TEST(LRUTileCacheTest, CapacityReductionWithOversizedCache) {
  LRUTileCache cache(1000);

  // Fill cache significantly
  for (uint32_t i = 0; i < 500; ++i) {
    TileKey key{"test.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  EXPECT_EQ(cache.GetSize(), 500);

  // Reduce capacity drastically
  auto status = cache.SetCapacity(10);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(cache.GetCapacity(), 10);
  EXPECT_EQ(cache.GetSize(), 0);  // Cleared

  // Verify can use new capacity
  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"new.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  EXPECT_EQ(cache.GetSize(), 10);
}

// ============================================================================
// Real-World Usage Patterns
// ============================================================================

TEST(LRUTileCacheTest, SlideViewerZoomPattern) {
  LRUTileCache cache(100);

  // Simulate user zooming in on a region
  // Level 2 (overview)
  for (uint32_t i = 0; i < 10; ++i) {
    TileKey key{"viewer.mrxs", 2, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // Level 1 (medium zoom)
  for (uint32_t i = 0; i < 20; ++i) {
    TileKey key{"viewer.mrxs", 1, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  // Level 0 (full resolution) - user's focus
  for (uint32_t i = 0; i < 40; ++i) {
    TileKey key{"viewer.mrxs", 0, i, 0};
    cache.Put(key, CreateTestTile(256, 256, 3));
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.size, 70);

  // Repeatedly access level 0 tiles (user panning)
  for (int repeat = 0; repeat < 5; ++repeat) {
    for (uint32_t i = 10; i < 30; ++i) {
      TileKey key{"viewer.mrxs", 0, i, 0};
      cache.Get(key);
    }
  }

  // Should have high hit ratio for repeated access
  stats = cache.GetStats();
  EXPECT_GT(stats.hit_ratio, 0.8);
}

TEST(LRUTileCacheTest, MultiSlideWorkflow) {
  LRUTileCache cache(200);

  // User working with multiple slides
  std::vector<std::string> slides = {"slide1.mrxs", "slide2.mrxs",
                                     "slide3.mrxs"};

  for (const auto& slide : slides) {
    for (uint32_t i = 0; i < 50; ++i) {
      TileKey key{slide, 0, i, 0};
      cache.Put(key, CreateTestTile(256, 256, 3));
    }
  }

  EXPECT_EQ(cache.GetSize(), 150);

  // Access tiles from different slides
  for (const auto& slide : slides) {
    for (uint32_t i = 0; i < 10; ++i) {
      TileKey key{slide, 0, i, 0};
      auto result = cache.Get(key);
      EXPECT_NE(result, nullptr);
    }
  }

  auto stats = cache.GetStats();
  EXPECT_EQ(stats.hits, 30);  // 3 slides * 10 tiles each
}

// ============================================================================
// Empty Data Edge Cases
// ============================================================================

TEST(LRUTileCacheTest, EmptyTileData) {
  LRUTileCache cache(10);

  TileKey key{"empty.mrxs", 0, 0, 0};
  std::vector<uint8_t> empty_data;  // Empty vector
  auto empty_tile = std::make_shared<CachedTileData>(
      std::move(empty_data), aifocore::Size<uint32_t, 2>{0, 0}, 0);

  cache.Put(key, empty_tile);

  // Empty tile should be cached
  EXPECT_EQ(cache.GetSize(), 1);

  auto retrieved = cache.Get(key);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->data.size(), 0);
  EXPECT_EQ(retrieved->size[0], 0);
  EXPECT_EQ(retrieved->size[1], 0);

  // Memory usage should be 0 for empty tile
  EXPECT_EQ(cache.GetMemoryUsage(), 0);
}

}  // namespace runtime
}  // namespace fastslide
