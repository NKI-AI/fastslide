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

#include "fastslide/utilities/tiff/tiff_cache_service.h"

#include <memory>
#include <string>
#include <utility>

namespace fastslide::tiff {

TiffCacheService::TiffCacheService(std::shared_ptr<TileCache> cache)
    : TileCacheManager(std::move(cache)) {}

absl::StatusOr<std::shared_ptr<CachedTile>> TiffCacheService::GetTileFromTiff(
    const std::string& filename, uint16_t page,
    const aifocore::Size<uint32_t, 2>& tile_coords, TiffFile& tiff_file,
    const aifocore::Size<uint32_t, 2>& tile_dims,
    const PlanarInterleaver::Config& interleaver_config,
    uint32_t bytes_per_pixel) {

  // Calculate cache coordinates (tile indices)
  auto cache_coords = CalculateCacheCoords(tile_coords, tile_dims);

  // Create tile loader with interleaving
  auto tile_loader = CreateTileLoaderWithInterleaving(
      tiff_file, tile_coords, tile_dims, interleaver_config, bytes_per_pixel);

  // Use base class functionality for cache lookup/storage
  return GetTile(filename, page, cache_coords, tile_loader);
}

absl::StatusOr<std::shared_ptr<CachedTile>>
TiffCacheService::GetScanlineFromTiff(
    const std::string& filename, uint16_t page, uint32_t row,
    TiffFile& tiff_file, uint32_t image_width,
    const PlanarInterleaver::Config& interleaver_config,
    uint32_t bytes_per_pixel) {

  // Create scanline loader with interleaving
  auto scanline_loader = CreateScanlineLoaderWithInterleaving(
      tiff_file, row, image_width, interleaver_config, bytes_per_pixel);

  // Use scanline coordinates for caching (treat as special tile coordinates)
  const aifocore::Size<uint32_t, 2> scanline_coords{0, row};

  // Use base class functionality for cache lookup/storage
  return GetTile(filename, page, scanline_coords, scanline_loader);
}

aifocore::Size<uint32_t, 2> TiffCacheService::CalculateCacheCoords(
    const aifocore::Size<uint32_t, 2>& tile_coords,
    const aifocore::Size<uint32_t, 2>& tile_dims) const {

  // Convert tile coordinates to tile indices for cache keys
  return tile_coords / tile_dims;
}

}  // namespace fastslide::tiff
