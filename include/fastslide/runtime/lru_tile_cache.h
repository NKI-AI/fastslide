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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_LRU_TILE_CACHE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_LRU_TILE_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "fastslide/runtime/cache_interface.h"

/**
 * @file lru_tile_cache.h
 * @brief LRU tile cache implementation of ITileCache
 * 
 * This header provides a concrete implementation of the ITileCache interface
 * using an LRU (Least Recently Used) eviction policy. This is the default
 * cache implementation used by FastSlide.
 */

namespace fastslide {
namespace runtime {

/// @brief Hash function for TileKey
struct TileKeyHash {
  std::size_t operator()(const TileKey& key) const {
    std::size_t hash1 = std::hash<std::string>{}(key.filename);
    std::size_t hash2 = std::hash<uint16_t>{}(key.level);
    std::size_t hash3 = std::hash<uint32_t>{}(key.tile_x);
    std::size_t hash4 = std::hash<uint32_t>{}(key.tile_y);

    // Use a better hash combination method than simple XOR
    std::size_t result = hash1;
    result = result * 31 + hash2;
    result = result * 31 + hash3;
    result = result * 31 + hash4;

    return result;
  }
};

/// @brief LRU tile cache implementation
///
/// A thread-safe LRU cache implementation that implements the ITileCache
/// interface. Uses a combination of hash map for O(1) lookup and doubly-linked
/// list for O(1) LRU operations.
///
/// This is the default cache implementation used by FastSlide readers.
class LRUTileCache : public ITileCache {
 public:
  /// @brief Create a new LRU tile cache
  /// @param capacity Maximum number of tiles to cache
  /// @return StatusOr containing the cache instance or error
  static absl::StatusOr<std::shared_ptr<LRUTileCache>> Create(
      size_t capacity = 1000);

  /// @brief Constructor
  /// @param capacity Maximum number of tiles to cache
  explicit LRUTileCache(size_t capacity);

  /// @brief Destructor
  ~LRUTileCache() override = default;

  // ITileCache interface implementation
  std::shared_ptr<CachedTileData> Get(const TileKey& key) override;
  void Put(const TileKey& key, std::shared_ptr<CachedTileData> tile) override;
  void Clear() override;
  size_t GetSize() const override;
  size_t GetCapacity() const override;
  size_t GetMemoryUsage() const override;
  Stats GetStats() const override;

  /// @brief Set cache capacity
  /// @param capacity New capacity
  /// @return Status indicating success or failure
  absl::Status SetCapacity(size_t capacity);

 private:
  /// @brief Internal cache entry
  struct CacheEntry {
    TileKey key;
    std::shared_ptr<CachedTileData> tile;
    std::list<TileKey>::iterator lru_it;
  };

  /// @brief Evict least recently used tile
  void EvictLru();

  size_t capacity_;
  mutable absl::Mutex mutex_;
  std::unordered_map<TileKey, CacheEntry, TileKeyHash> cache_;
  std::list<TileKey> lru_list_;

  // Statistics
  mutable size_t hits_;
  mutable size_t misses_;
};

}  // namespace runtime

// Import into fastslide namespace
using runtime::LRUTileCache;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_LRU_TILE_CACHE_H_
