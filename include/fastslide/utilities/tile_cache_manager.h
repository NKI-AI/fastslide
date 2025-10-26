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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TILE_CACHE_MANAGER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TILE_CACHE_MANAGER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "fastslide/utilities/cache.h"

namespace fastslide {

/// @brief Function type for loading tiles when cache misses occur
/// @return StatusOr containing the loaded tile data, or an error status
using TileLoader = std::function<absl::StatusOr<std::shared_ptr<CachedTile>>()>;

/// @brief Manager for tile caching that abstracts cache operations
///
/// This class encapsulates all tile caching logic, providing a clean interface
/// for tile access without exposing cache internals. It handles cache key
/// creation, cache miss fallbacks, and provides a unified API for both tiled
/// and scanline-based data access.
///
/// The manager can be used with any cache implementation that follows the
/// TileCache interface, making it flexible and reusable across different
/// components.
class TileCacheManager {
 public:
  /// @brief Constructor
  /// @param cache Shared pointer to tile cache (nullptr disables caching)
  explicit TileCacheManager(std::shared_ptr<TileCache> cache = nullptr);

  /// @brief Destructor
  ~TileCacheManager() = default;

  // Non-copyable but movable
  TileCacheManager(const TileCacheManager&) = delete;
  TileCacheManager& operator=(const TileCacheManager&) = delete;

  TileCacheManager(TileCacheManager&&) = default;
  TileCacheManager& operator=(TileCacheManager&&) = default;

  /// @brief Get a tile, loading it if not in cache
  /// @param filename File path for cache key
  /// @param level Pyramid level for cache key
  /// @param tile_coords Tile coordinates for cache key
  /// @param loader Function to load tile data on cache miss
  /// @return StatusOr containing the cached tile or an error status
  absl::StatusOr<std::shared_ptr<CachedTile>> GetTile(
      const std::string& filename, uint16_t level,
      const aifocore::Size<uint32_t, 2>& tile_coords, TileLoader loader);

  /// @brief Get a tile from cache without loading on miss
  /// @param filename File path for cache key
  /// @param level Pyramid level for cache key
  /// @param tile_x Tile X coordinate for cache key
  /// @param tile_y Tile Y coordinate for cache key
  /// @return Cached tile or nullptr if not found or caching disabled
  std::shared_ptr<CachedTile> GetTileFromCache(const std::string& filename,
                                               uint16_t level, uint32_t tile_x,
                                               uint32_t tile_y);

  /// @brief Put a tile in cache
  /// @param filename File path for cache key
  /// @param level Pyramid level for cache key
  /// @param tile_x Tile X coordinate for cache key
  /// @param tile_y Tile Y coordinate for cache key
  /// @param tile Tile data to cache
  void PutTile(const std::string& filename, uint16_t level, uint32_t tile_x,
               uint32_t tile_y, std::shared_ptr<CachedTile> tile);

  /// @brief Check if caching is enabled
  /// @return True if cache is set and enabled
  bool IsCacheEnabled() const;

  /// @brief Set the cache instance
  /// @param cache Shared pointer to tile cache (nullptr disables caching)
  void SetCache(std::shared_ptr<TileCache> cache);

  /// @brief Get the current cache instance
  /// @return Shared pointer to current cache (nullptr if disabled)
  std::shared_ptr<TileCache> GetCache() const;

  /// @brief Get cache statistics
  /// @return Cache statistics or empty stats if caching is disabled
  TileCache::Stats GetStats() const;

  /// @brief Clear all cached tiles
  void Clear();

 private:
  /// @brief Create a cache key for the given parameters
  /// @param filename File path
  /// @param level Pyramid level
  /// @param tile_x Tile X coordinate
  /// @param tile_y Tile Y coordinate
  /// @return Cache key for the tile
  runtime::TileKey CreateCacheKey(const std::string& filename, uint16_t level,
                                  uint32_t tile_x, uint32_t tile_y) const;

  /// @brief Tile cache instance
  std::shared_ptr<TileCache> cache_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TILE_CACHE_MANAGER_H_
