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
#include "fastslide/readers/inflight_decodes.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide {

/// @brief A decoded tile shared read-only by the cache and the readers. It owns
/// its pixels, which stay valid as long as the handle lives, on any thread.
using TileHandle = std::shared_ptr<const CachedTileData>;

/// @brief A view of a tile decoded into this thread's reusable buffers (see
/// `TiffBasedTileExecutor`); valid until the thread decodes again. Readers
/// return it from `ReadTileFromDisk` until they implement `DecodeTile`.
struct DecodedTileData {
  std::span<const uint8_t> data;
  uint32_t width;
  uint32_t height;
  uint32_t channels;
};

/// @brief CRTP mixin that reads tiles through the reader's tile cache.
///
/// Executors read tiles with `ReadTile`, which returns an owning handle: a
/// cache hit shares the cached tile, a miss decodes it once (concurrent misses
/// wait for one decode) and caches the decoded tile itself, without copying.
///
/// Usage:
/// ```cpp
/// class MyTileExecutor : public CachedTileExecutor<MyTileExecutor> {
///   // MakeCacheKey(op, context) -> TileKey
///   // DecodeTile(op, context) -> Result<CachedTileData> (owns its pixels), or
///   // ReadTileFromDisk(op, context) -> Result<DecodedTileData> (copied once)
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

  /// @brief Read one tile: from the reader's cache when it has one, otherwise
  /// by decoding it.
  ///
  /// The handle owns the pixels. With a cache, a hit returns the cached tile
  /// itself and concurrent misses on a tile decode it once (see
  /// `readers::InflightDecodes`); the decoded tile is cached as is.
  template <typename Reader, typename... Args>
  static aifocore::Result<TileHandle> ReadTile(const core::TileReadOp& op,
                                               const Reader& reader,
                                               Args&&... args) {
    if (!IsCacheBypassEnabled() && reader.IsCacheEnabled()) {
      AIFOCORE_ASSIGN_OR_RETURN(
          auto tile, ReadThroughCache(op, reader, std::forward<Args>(args)...));
      return TileHandle(std::move(tile));
    }
    AIFOCORE_ASSIGN_OR_RETURN(
        auto tile,
        Derived::DecodeTile(op, reader, std::forward<Args>(args)...));
    return TileHandle(std::make_shared<CachedTileData>(std::move(tile)));
  }

  /// @brief Decode one tile into a tile that owns its pixels.
  ///
  /// Executors whose decoder produces an owned buffer define their own
  /// `DecodeTile` and move the buffer into the result. This default wraps
  /// `ReadTileFromDisk`, whose view points into this thread's reusable
  /// buffers, and copies the pixels out.
  template <typename Reader, typename... Args>
  static aifocore::Result<CachedTileData> DecodeTile(const core::TileReadOp& op,
                                                     const Reader& reader,
                                                     Args&&... args) {
    AIFOCORE_ASSIGN_OR_RETURN(
        auto decoded,
        Derived::ReadTileFromDisk(op, reader, std::forward<Args>(args)...));
    return CachedTileData(
        std::vector<uint8_t>(decoded.data.begin(), decoded.data.end()),
        {decoded.width, decoded.height}, decoded.channels);
  }

  /// @brief Read a tile as a view (older API, being replaced by `ReadTile`).
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

    if (!IsCacheBypassEnabled() && reader.IsCacheEnabled()) {
      AIFOCORE_ASSIGN_OR_RETURN(
          kept_alive_tile,
          ReadThroughCache(op, reader, std::forward<Args>(args)...));
      return std::span<const uint8_t>(kept_alive_tile->data);
    }

    // No cache: decode into TiffBasedTileExecutor's thread-local buffers.
    AIFOCORE_ASSIGN_OR_RETURN(
        auto decoded_data,
        Derived::ReadTileFromDisk(op, reader, std::forward<Args>(args)...));
    return decoded_data.data;
  }

  /// @brief Read a tile, preserving decoded metadata (width/height/channels).
  ///
  /// This is required for formats where the decoded tile dimensions can differ
  /// from the nominal metadata (e.g. edge tiles), or where decoders may return
  /// padding that should be interpreted using the decoded dimensions.
  template <typename Reader, typename... Args>
  static aifocore::Result<DecodedTileData> ReadWithCacheDecoded(
      const core::TileReadOp& op, const Reader& reader, Args&&... args) {
    static thread_local std::shared_ptr<CachedTileData> kept_alive_tile;
    kept_alive_tile.reset();

    if (!IsCacheBypassEnabled() && reader.IsCacheEnabled()) {
      AIFOCORE_ASSIGN_OR_RETURN(
          kept_alive_tile,
          ReadThroughCache(op, reader, std::forward<Args>(args)...));
      return DecodedTileData{
          .data = std::span<const uint8_t>(kept_alive_tile->data),
          .width = kept_alive_tile->size[0],
          .height = kept_alive_tile->size[1],
          .channels = kept_alive_tile->channels,
      };
    }

    return Derived::ReadTileFromDisk(op, reader, std::forward<Args>(args)...);
  }

  /// @brief Get a tile from the cache, or decode it once and cache it.
  ///
  /// Concurrent misses on the same tile decode it once: the first thread
  /// decodes (see `readers::InflightDecodes`), the others wait for its tile
  /// instead of decoding it again. The cache must be enabled.
  template <typename Reader, typename... Args>
  static aifocore::Result<std::shared_ptr<CachedTileData>> ReadThroughCache(
      const core::TileReadOp& op, const Reader& reader, Args&&... args) {
    auto cache = reader.GetCache();
    const TileKey key = Derived::MakeCacheKey(op, reader, args...);
    if (auto tile = cache->Get(key)) {
      return tile;
    }

    auto claim = readers::InflightDecodes::Instance().Join(key);
    if (!claim.leader) {
      if (auto tile = claim.waiters->Wait()) {
        return tile;
      }
      // The leader's decode failed: decode here, to report our own error.
      return DecodeAndCache(op, reader, *cache, key,
                            std::forward<Args>(args)...);
    }

    // A decode that finished between our miss and `Join` may be cached
    // already; decoding it again then is harmless (same pixels), and
    // re-checking the cache on every miss would cost more than it saves.
    readers::InflightDecodes::Leader leader(key);
    AIFOCORE_ASSIGN_OR_RETURN(
        auto tile,
        DecodeAndCache(op, reader, *cache, key, std::forward<Args>(args)...));
    // Cached before the waiters wake, so later misses hit the cache.
    leader.Finish(tile);
    return tile;
  }

  template <typename Reader, typename... Args>
  static aifocore::Result<std::shared_ptr<CachedTileData>> DecodeAndCache(
      const core::TileReadOp& op, const Reader& reader,
      runtime::ITileCache& cache, const TileKey& key, Args&&... args) {
    AIFOCORE_ASSIGN_OR_RETURN(
        auto decoded,
        Derived::DecodeTile(op, reader, std::forward<Args>(args)...));
    auto tile = std::make_shared<CachedTileData>(std::move(decoded));
    cache.Put(key, tile);
    return tile;
  }
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CACHED_TILE_EXECUTOR_H_
