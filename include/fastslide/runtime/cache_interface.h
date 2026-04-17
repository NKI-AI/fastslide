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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_CACHE_INTERFACE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_CACHE_INTERFACE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/concepts/numeric.h"  // For aifocore::Size

/**
 * @file cache_interface.h
 * @brief Polymorphic tile cache interface for dependency injection
 * 
 * This header defines the abstract interface for tile caching, allowing
 * different cache implementations (LRU, shared-memory, GPU, etc.) to be
 * plugged into readers without changing their code.
 */

namespace fastslide {
namespace runtime {

/// @brief Cache key for uniquely identifying tiles
///
/// This structure uniquely identifies a tile within a slide file at a specific
/// level and position. It serves as the key for all cache operations.
struct TileKey {
  std::string filename;  ///< Path to the slide file
  uint16_t level;        ///< Pyramid level (0 = highest resolution)
  uint32_t tile_x;       ///< Tile X coordinate in the grid
  uint32_t tile_y;       ///< Tile Y coordinate in the grid

  /// @brief Equality comparison operator
  bool operator==(const TileKey& other) const {
    return filename == other.filename && level == other.level &&
           tile_x == other.tile_x && tile_y == other.tile_y;
  }

  /// @brief Inequality comparison operator
  bool operator!=(const TileKey& other) const { return !(*this == other); }

  /// @brief Default constructor
  TileKey() : level(0), tile_x(0), tile_y(0) {}

  /// @brief Constructor with values
  TileKey(std::string fname, uint16_t lvl, uint32_t tx, uint32_t ty)
      : filename(std::move(fname)), level(lvl), tile_x(tx), tile_y(ty) {}
};

/// @brief Cached tile data container
///
/// Contains the actual tile image data along with metadata about dimensions
/// and channels. This is the value stored in caches.
struct CachedTileData {
  std::vector<uint8_t> data;         ///< Raw tile image data
  aifocore::Size<uint32_t, 2> size;  ///< Tile size in pixels
  uint32_t channels;                 ///< Number of color channels

  /// @brief Default constructor
  CachedTileData() : channels(0) {}

  /// @brief Constructor with data
  CachedTileData(std::vector<uint8_t> tile_data,
                 aifocore::Size<uint32_t, 2> tile_size, uint32_t num_channels)
      : data(std::move(tile_data)), size(tile_size), channels(num_channels) {}

  /// @brief Get memory usage in bytes
  [[nodiscard]] size_t GetMemoryUsage() const { return data.size(); }
};

/// @brief Abstract tile cache interface
///
/// Defines the contract for tile caching implementations. Readers depend on
/// this interface rather than concrete cache classes, allowing different
/// cache strategies to be injected at runtime.
///
/// Implementations must be thread-safe as they may be accessed from multiple
/// reader instances concurrently.
class ITileCache {
 public:
  virtual ~ITileCache() = default;

  /// @brief Get a tile from cache
  ///
  /// Retrieves a tile from the cache if it exists. Thread-safe.
  ///
  /// @param key Tile key identifying the requested tile
  /// @return Cached tile data if found, nullptr otherwise
  [[nodiscard]] virtual std::shared_ptr<CachedTileData> Get(
      const TileKey& key) = 0;

  /// @brief Put a tile in cache
  ///
  /// Adds or updates a tile in the cache. Thread-safe.
  ///
  /// @param key Tile key identifying the tile
  /// @param tile Shared pointer to the tile data
  virtual void Put(const TileKey& key,
                   std::shared_ptr<CachedTileData> tile) = 0;

  /// @brief Clear all cached tiles
  ///
  /// Removes all tiles from the cache. Thread-safe.
  virtual void Clear() = 0;

  /// @brief Get current cache size (number of tiles)
  /// @return Current number of cached tiles
  [[nodiscard]] virtual size_t GetSize() const = 0;

  /// @brief Get cache capacity (maximum number of tiles)
  /// @return Maximum number of tiles that can be cached
  [[nodiscard]] virtual size_t GetCapacity() const = 0;

  /// @brief Get total memory usage in bytes
  /// @return Total memory usage of all cached tiles
  [[nodiscard]] virtual size_t GetMemoryUsage() const = 0;

  /// @brief Get cache statistics
  struct Stats {
    size_t capacity;            ///< Maximum cache capacity
    size_t size;                ///< Current number of cached tiles
    size_t hits;                ///< Number of cache hits
    size_t misses;              ///< Number of cache misses
    double hit_ratio;           ///< Hit ratio (hits / (hits + misses))
    size_t memory_usage_bytes;  ///< Total memory usage in bytes
  };

  /// @brief Get cache statistics
  /// @return Current cache statistics
  [[nodiscard]] virtual Stats GetStats() const = 0;
};

}  // namespace runtime

// Import runtime types into fastslide namespace
using runtime::CachedTileData;
using runtime::ITileCache;
using runtime::TileKey;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_CACHE_INTERFACE_H_
