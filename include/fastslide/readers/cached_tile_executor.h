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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CACHED_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CACHED_TILE_EXECUTOR_H_

#include <cstdlib>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide {

/// @brief Structure to hold decoded tile data and metadata
struct DecodedTileData {
  std::span<const uint8_t> data;
  uint32_t width;
  uint32_t height;
  uint32_t channels;
};

/// @brief CRTP mixin for adding caching support to tile executors
///
/// This class provides a generic ReadWithCache implementation that handles:
/// 1. Checking the reader's cache for existing tiles
/// 2. Keeping cached tiles alive via thread-local storage
/// 3. Delegating to the derived class for reading from disk/decoding
/// 4. Storing new tiles in the cache
///
/// It inherits from TiffBasedTileExecutor to also provide thread-local buffer
/// management.
///
/// Usage:
/// ```cpp
/// class MyTileExecutor : public CachedTileExecutor<MyTileExecutor> {
///   // Implement MakeCacheKey
///   // Implement ReadTileFromDisk returning DecodedTileData
///   // Implement MakeCachedTileData taking DecodedTileData
/// };
/// ```
template <typename Derived>
class CachedTileExecutor : public TiffBasedTileExecutor<Derived> {
 protected:
  static bool IsCacheBypassEnabled() {
    // Debug escape hatch: allow disabling caching globally without changing
    // APIs or plumbing config through every reader/executor call.
    //
    // Usage:
    //   FASTSLIDE_DISABLE_TILE_CACHE=1  (or "true") to bypass cache Get/Put.
    //
    // Note: this is intentionally process-wide and cheap; evaluated once per
    // translation unit.
    static const bool kDisabled = []() -> bool {
      const char* env = std::getenv("FASTSLIDE_DISABLE_TILE_CACHE");
      if (env == nullptr) {
        return false;
      }
      // Accept common truthy values.
      return (env[0] == '1') || (env[0] == 't') || (env[0] == 'T') ||
             (env[0] == 'y') || (env[0] == 'Y');
    }();
    return kDisabled;
  }

  /// @brief Read a tile, checking cache first, or reading from disk and caching
  ///
  /// @param op Tile operation info
  /// @param reader Reader instance (provides cache and filename)
  /// @param args Additional arguments to pass to ReadTileFromDisk
  /// @return Span of tile data (valid until next call on this thread)
  template <typename Reader, typename... Args>
  static aifocore::Result<std::span<const uint8_t>> ReadWithCache(
      const core::TileReadOp& op, const Reader& reader, Args&&... args) {

    // Thread-local storage to keep cached tile alive while span is in use
    static thread_local std::shared_ptr<CachedTileData> kept_alive_tile;

    // Reset kept alive tile to release reference to previous tile
    kept_alive_tile.reset();

    const bool bypass_cache = IsCacheBypassEnabled();

    // Check cache if enabled (and not bypassed)
    if (!bypass_cache && reader.IsCacheEnabled()) {
      auto cache = reader.GetCache();
      // Delegate key creation to derived class
      TileKey key =
          Derived::MakeCacheKey(op, reader, std::forward<Args>(args)...);

      if (auto tile = cache->Get(key)) {
        kept_alive_tile = tile;
        return std::span<const uint8_t>(tile->data);
      }
    }

    // Cache miss: Read from disk via derived class
    // This should use TiffBasedTileExecutor's thread-local buffers
    auto data_or =
        Derived::ReadTileFromDisk(op, reader, std::forward<Args>(args)...);
    if (!data_or.ok()) {
      return data_or.status();
    }

    const auto& decoded_data = *data_or;

    // Store in cache if enabled (and not bypassed)
    if (!bypass_cache && reader.IsCacheEnabled()) {
      auto cache = reader.GetCache();
      TileKey key =
          Derived::MakeCacheKey(op, reader, std::forward<Args>(args)...);

      // Create cached tile data
      auto cached_tile = Derived::MakeCachedTileData(decoded_data);

      cache->Put(key, cached_tile);
    }

    return decoded_data.data;
  }

  /// @brief Helper to create CachedTileData from DecodedTileData
  static std::shared_ptr<CachedTileData> MakeCachedTileData(
      const DecodedTileData& decoded) {
    auto cached_tile = std::make_shared<CachedTileData>();
    // Copy data to vector for cache ownership
    cached_tile->data.assign(decoded.data.begin(), decoded.data.end());
    cached_tile->size = {decoded.width, decoded.height};
    cached_tile->channels = decoded.channels;
    return cached_tile;
  }
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CACHED_TILE_EXECUTOR_H_
