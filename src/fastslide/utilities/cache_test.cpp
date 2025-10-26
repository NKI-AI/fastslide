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

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include "aifocore/concepts/numeric.h"
#include "fastslide/utilities/cache.h"

namespace fastslide {

class TileCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create test tiles
    std::vector<uint8_t> data1(256 * 256 * 3, 128);  // Gray tile
    std::vector<uint8_t> data2(256 * 256 * 3, 255);  // White tile
    std::vector<uint8_t> data3(256 * 256 * 3, 0);    // Black tile

    tile1_ = std::make_shared<CachedTile>(
        std::move(data1), aifocore::Size<uint32_t, 2>{256, 256}, 3);
    tile2_ = std::make_shared<CachedTile>(
        std::move(data2), aifocore::Size<uint32_t, 2>{256, 256}, 3);
    tile3_ = std::make_shared<CachedTile>(
        std::move(data3), aifocore::Size<uint32_t, 2>{256, 256}, 3);

    // Create test keys
    key1_ = TileKey("test1.tiff", 0, 0, 0);
    key2_ = TileKey("test1.tiff", 0, 1, 0);
    key3_ = TileKey("test2.tiff", 1, 0, 0);
  }

  std::shared_ptr<CachedTile> tile1_;
  std::shared_ptr<CachedTile> tile2_;
  std::shared_ptr<CachedTile> tile3_;
  TileKey key1_;
  TileKey key2_;
  TileKey key3_;
};

// Test basic cache operations
TEST_F(TileCacheTest, BasicOperations) {
  TileCache cache(10);

  // Test initial state
  EXPECT_EQ(cache.GetCapacity(), 10);
  EXPECT_EQ(cache.GetSize(), 0);
  EXPECT_EQ(cache.Get(key1_), nullptr);

  // Test Put and Get
  cache.Put(key1_, tile1_);
  EXPECT_EQ(cache.GetSize(), 1);

  auto retrieved = cache.Get(key1_);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->size[0], 256);
  EXPECT_EQ(retrieved->size[1], 256);
  EXPECT_EQ(retrieved->channels, 3);
  EXPECT_EQ(retrieved->data.size(), 256 * 256 * 3);

  // Test overwriting existing key
  cache.Put(key1_, tile2_);
  EXPECT_EQ(cache.GetSize(), 1);

  retrieved = cache.Get(key1_);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->data[0], 255);  // Should be white tile now

  // Test Clear
  cache.Clear();
  EXPECT_EQ(cache.GetSize(), 0);
  EXPECT_EQ(cache.Get(key1_), nullptr);
}

// Test LRU eviction behavior
TEST_F(TileCacheTest, LRUEviction) {
  TileCache cache(2);  // Small cache for testing eviction

  // Fill cache to capacity
  cache.Put(key1_, tile1_);
  cache.Put(key2_, tile2_);
  EXPECT_EQ(cache.GetSize(), 2);

  // Verify both tiles are in cache
  EXPECT_NE(cache.Get(key1_), nullptr);
  EXPECT_NE(cache.Get(key2_), nullptr);

  // Add third tile, should evict least recently used
  cache.Put(key3_, tile3_);
  EXPECT_EQ(cache.GetSize(), 2);

  // key1_ should be evicted (was accessed before key2_)
  EXPECT_EQ(cache.Get(key1_), nullptr);
  EXPECT_NE(cache.Get(key2_), nullptr);
  EXPECT_NE(cache.Get(key3_), nullptr);
}

// Test LRU order with access patterns
TEST_F(TileCacheTest, LRUAccessPattern) {
  TileCache cache(2);

  // Add two tiles
  cache.Put(key1_, tile1_);
  cache.Put(key2_, tile2_);

  // Access key1_ to make it more recently used
  cache.Get(key1_);

  // Add third tile, should evict key2_ (less recently used)
  cache.Put(key3_, tile3_);

  EXPECT_NE(cache.Get(key1_), nullptr);
  EXPECT_EQ(cache.Get(key2_), nullptr);
  EXPECT_NE(cache.Get(key3_), nullptr);
}

// Test statistics tracking
TEST_F(TileCacheTest, Statistics) {
  TileCache cache(5);

  // Initial stats
  auto stats = cache.GetStats();
  EXPECT_EQ(stats.capacity, 5);
  EXPECT_EQ(stats.size, 0);
  EXPECT_EQ(stats.hits, 0);
  EXPECT_EQ(stats.misses, 0);
  EXPECT_EQ(stats.hit_ratio, 0.0);

  // Add some tiles
  cache.Put(key1_, tile1_);
  cache.Put(key2_, tile2_);

  // Test misses
  cache.Get(key3_);  // Miss
  cache.Get(key3_);  // Miss

  stats = cache.GetStats();
  EXPECT_EQ(stats.size, 2);
  EXPECT_EQ(stats.hits, 0);
  EXPECT_EQ(stats.misses, 2);
  EXPECT_EQ(stats.hit_ratio, 0.0);

  // Test hits
  cache.Get(key1_);  // Hit
  cache.Get(key2_);  // Hit
  cache.Get(key1_);  // Hit

  stats = cache.GetStats();
  EXPECT_EQ(stats.hits, 3);
  EXPECT_EQ(stats.misses, 2);
  EXPECT_DOUBLE_EQ(stats.hit_ratio, 3.0 / 5.0);

  // Test Clear resets stats
  cache.Clear();
  stats = cache.GetStats();
  EXPECT_EQ(stats.size, 0);
  EXPECT_EQ(stats.hits, 0);
  EXPECT_EQ(stats.misses, 0);
  EXPECT_EQ(stats.hit_ratio, 0.0);
}

// Test error handling
TEST_F(TileCacheTest, ErrorHandling) {
  // Test zero capacity using Create method
  auto cache_result = TileCache::Create(0);
  EXPECT_FALSE(cache_result.ok());
  EXPECT_EQ(cache_result.status().code(), absl::StatusCode::kInvalidArgument);

  // Test putting null tile
  TileCache cache(5);
  cache.Put(key1_, nullptr);
  EXPECT_EQ(cache.GetSize(), 0);
  EXPECT_EQ(cache.Get(key1_), nullptr);
}

// Test TileKey equality and hashing
TEST_F(TileCacheTest, TileKeyOperations) {
  TileKey key1("test.tiff", 0, 100, 200);
  TileKey key2("test.tiff", 0, 100, 200);
  TileKey key3("test.tiff", 0, 100, 201);
  TileKey key4("test.tiff", 1, 100, 200);
  TileKey key5("other.tiff", 0, 100, 200);

  // Test equality
  EXPECT_EQ(key1, key2);
  EXPECT_NE(key1, key3);
  EXPECT_NE(key1, key4);
  EXPECT_NE(key1, key5);

  // Test hashing - equal keys should have same hash
  TileKeyHash hasher;
  EXPECT_EQ(hasher(key1), hasher(key2));

  // Different keys should likely have different hashes
  // (not guaranteed but very likely with good hash function)
  EXPECT_NE(hasher(key1), hasher(key3));
  EXPECT_NE(hasher(key1), hasher(key4));
  EXPECT_NE(hasher(key1), hasher(key5));
}

// Test thread safety
TEST_F(TileCacheTest, ThreadSafety) {
  TileCache cache(100);
  const int num_threads = 10;
  const int operations_per_thread = 100;

  std::vector<std::thread> threads;
  std::atomic<int> successful_operations(0);

  // Create multiple threads that perform cache operations
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&cache, &successful_operations, i]() {
      for (int j = 0; j < operations_per_thread; ++j) {
        // Create unique key for this thread and operation
        TileKey key("thread_" + std::to_string(i) + "_op_" + std::to_string(j) +
                        ".tiff",
                    static_cast<uint16_t>(i % 5), static_cast<uint32_t>(j),
                    static_cast<uint32_t>(i));

        // Create tile data
        std::vector<uint8_t> data(256 * 256 * 3, static_cast<uint8_t>(i + j));
        auto tile = std::make_shared<CachedTile>(
            std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);

        // Put tile in cache
        cache.Put(key, tile);

        // Try to get it back
        auto retrieved = cache.Get(key);
        if (retrieved != nullptr) {
          successful_operations++;
        }

        // Occasionally clear cache to test concurrent access
        if (j % 50 == 0) {
          cache.Clear();
        }
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // We should have had some successful operations
  EXPECT_GT(successful_operations.load(), 0);

  // Get final stats - should not crash
  auto stats = cache.GetStats();
  EXPECT_LE(stats.size, 100);  // Should not exceed capacity
}

// Test GlobalTileCache
TEST_F(TileCacheTest, GlobalTileCache) {
  auto& global_cache = GlobalTileCache::Instance();

  // Test setting capacity
  auto status = global_cache.SetCapacity(20);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(global_cache.GetCapacity(), 20);

  // Test zero capacity returns error status
  status = global_cache.SetCapacity(0);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);

  // Test basic operations through global cache
  auto& cache = global_cache.GetCache();
  cache.Put(key1_, tile1_);

  auto retrieved = cache.Get(key1_);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->size[0], 256);

  // Test const access
  const auto& const_cache = global_cache.GetCache();
  auto stats = const_cache.GetStats();
  EXPECT_EQ(stats.size, 1);

  // Clear for next test
  cache.Clear();
}

// Test performance characteristics
TEST_F(TileCacheTest, Performance) {
  TileCache cache(1000);

  // Time cache operations
  auto start = std::chrono::high_resolution_clock::now();

  // First, populate the cache with tiles
  for (int i = 0; i < 100; ++i) {
    TileKey key{"perf_test.tiff", 0, static_cast<uint32_t>(i), 0};
    std::vector<uint8_t> data(256 * 256 * 3, static_cast<uint8_t>(i));
    auto tile = std::make_shared<CachedTile>(
        std::move(data), aifocore::Size<uint32_t, 2>{256, 256}, 3);
    cache.Put(key, tile);
  }

  // Now perform many Get operations that should mostly hit
  for (int i = 0; i < 10000; ++i) {
    TileKey key{"perf_test.tiff", 0, static_cast<uint32_t>(i % 100), 0};
    cache.Get(key);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  // Should complete reasonably quickly (less than 1 second)
  EXPECT_LT(duration.count(), 1000000);

  // Should have good hit ratio due to key reuse
  auto stats = cache.GetStats();
  EXPECT_GT(stats.hit_ratio,
            0.9);  // Should be very high since we're reusing keys
}

// Test edge cases
TEST_F(TileCacheTest, EdgeCases) {
  TileCache cache(1);  // Capacity of 1

  // Test single item cache
  cache.Put(key1_, tile1_);
  EXPECT_NE(cache.Get(key1_), nullptr);

  // Adding second item should evict first
  cache.Put(key2_, tile2_);
  EXPECT_EQ(cache.Get(key1_), nullptr);
  EXPECT_NE(cache.Get(key2_), nullptr);

  // Test large tile data
  std::vector<uint8_t> large_data(10 * 1024 * 1024, 42);  // 10MB tile
  auto large_tile = std::make_shared<CachedTile>(
      std::move(large_data), aifocore::Size<uint32_t, 2>{1024, 1024}, 10);

  cache.Put(key3_, large_tile);
  auto retrieved = cache.Get(key3_);
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->data.size(), 10 * 1024 * 1024);
  EXPECT_EQ(retrieved->data[0], 42);
}

}  // namespace fastslide
