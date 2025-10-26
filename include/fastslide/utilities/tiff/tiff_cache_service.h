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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TIFF_CACHE_SERVICE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TIFF_CACHE_SERVICE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "aifocore/concepts/numeric.h"
#include "fastslide/utilities/cache.h"
#include "fastslide/utilities/tiff/planar_interleaver.h"
#include "fastslide/utilities/tiff/tiff_file.h"
#include "fastslide/utilities/tile_cache_manager.h"

namespace fastslide {
namespace tiff {

/// @brief Specialized cache service for TIFF-based readers
///
/// This class extends TileCacheManager with TIFF-specific caching functionality,
/// providing high-level methods that handle both tiled and strip-based TIFF data
/// with automatic planar interleaving and cache management.
class TiffCacheService : public TileCacheManager {
 public:
  /// @brief Constructor
  /// @param cache Optional tile cache (nullptr to disable caching)
  explicit TiffCacheService(std::shared_ptr<TileCache> cache = nullptr);

  /// @brief Get a tile from cache or load it using TIFF file
  ///
  /// This method handles the complete tile loading workflow including
  /// cache lookup, planar interleaving, and cache storage.
  ///
  /// @param filename TIFF file path (for cache key generation)
  /// @param page TIFF page number (for cache key generation)
  /// @param tile_coords Tile coordinates
  /// @param tiff_file TiffFile wrapper for reading
  /// @param tile_dims Tile dimensions
  /// @param interleaver_config Planar interleaving configuration
  /// @param bytes_per_pixel Total bytes per pixel
  /// @return Cached tile or error status
  absl::StatusOr<std::shared_ptr<CachedTile>> GetTileFromTiff(
      const std::string& filename, uint16_t page,
      const aifocore::Size<uint32_t, 2>& tile_coords, TiffFile& tiff_file,
      const aifocore::Size<uint32_t, 2>& tile_dims,
      const PlanarInterleaver::Config& interleaver_config,
      uint32_t bytes_per_pixel);

  /// @brief Get a scanline from cache or load it using TIFF file
  ///
  /// This method treats scanlines as single-row tiles for caching purposes
  /// and handles planar interleaving for strip-based TIFF data.
  ///
  /// @param filename TIFF file path (for cache key generation)
  /// @param page TIFF page number (for cache key generation)
  /// @param row Scanline row number
  /// @param tiff_file TiffFile wrapper for reading
  /// @param image_width Image width
  /// @param interleaver_config Planar interleaving configuration
  /// @param bytes_per_pixel Total bytes per pixel
  /// @return Cached tile representing the scanline or error status
  absl::StatusOr<std::shared_ptr<CachedTile>> GetScanlineFromTiff(
      const std::string& filename, uint16_t page, uint32_t row,
      TiffFile& tiff_file, uint32_t image_width,
      const PlanarInterleaver::Config& interleaver_config,
      uint32_t bytes_per_pixel);

 private:
  /// @brief Calculate tile cache coordinates from tile coordinates
  /// @param tile_coords Tile coordinates in image space
  /// @param tile_dims Tile dimensions
  /// @return Cache coordinates (tile indices)
  aifocore::Size<uint32_t, 2> CalculateCacheCoords(
      const aifocore::Size<uint32_t, 2>& tile_coords,
      const aifocore::Size<uint32_t, 2>& tile_dims) const;
};

}  // namespace tiff
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TIFF_CACHE_SERVICE_H_
