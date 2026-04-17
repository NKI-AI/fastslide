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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_TILE_REQUEST_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_TILE_REQUEST_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "fastslide/image.h"

/**
 * @file tile_request.h
 * @brief Core domain models for tile and region requests
 * 
 * This header defines the structures used to request regions and tiles
 * from slides. These are lightweight, dependency-free structures suitable
 * for use across format plugins and language bindings.
 */

namespace fastslide {
namespace core {

/// @brief Region of interest specification
///
/// Specifies a rectangular region to read from a slide at a particular
/// pyramid level. All coordinates are in the coordinate space of the
/// specified level.
///
/// This is an aggregate type to support designated initializers in C++20.
struct RegionSpec {
  ImageCoordinate top_left;  ///< Top-left coordinate (level coordinates)
  ImageDimensions size;      ///< Desired region size in pixels
  int level;                 ///< Pyramid level (0 = full resolution)

  /// @brief Check if region is valid
  [[nodiscard]] bool IsValid() const noexcept {
    return size[0] > 0 && size[1] > 0 && level >= 0;
  }
};

/// @brief Tile coordinate in grid space
///
/// Identifies a specific tile within a level's tile grid.
/// Coordinates are in tile indices, not pixel coordinates.
///
/// This is an aggregate type to support designated initializers in C++20.
struct TileCoordinate {
  uint32_t x;  ///< Tile X index in grid
  uint32_t y;  ///< Tile Y index in grid

  /// @brief Equality comparison
  bool operator==(const TileCoordinate& other) const {
    return x == other.x && y == other.y;
  }

  /// @brief Inequality comparison
  bool operator!=(const TileCoordinate& other) const {
    return !(*this == other);
  }
};

/// @brief Fractional region bounds for precise positioning
///
/// Used by formats like MRXS that require fractional pixel coordinates
/// for subpixel-accurate tile placement and overlap averaging.
struct FractionalRegionBounds {
  double x;       ///< X coordinate (fractional pixels, level-native)
  double y;       ///< Y coordinate (fractional pixels, level-native)
  double width;   ///< Width in pixels
  double height;  ///< Height in pixels

  /// @brief Check if bounds are valid
  [[nodiscard]] bool IsValid() const noexcept {
    return width > 0.0 && height > 0.0;
  }
};

/// @brief Tile request specification
///
/// Specifies a request for a single tile from a slide, including
/// the level, tile coordinates, and optional channel selection.
///
/// For region-based requests (where the caller wants a specific region
/// rather than a specific tile), the optional region_bounds field
/// carries the actual region coordinates with fractional precision.
struct TileRequest {
  int level;                            ///< Pyramid level
  TileCoordinate tile_coord;            ///< Tile coordinates in grid
  std::vector<size_t> channel_indices;  ///< Requested channels (empty = all)

  /// @brief Optional region bounds for region-based requests
  /// @note Used when request originates from RegionSpec with fractional coords
  /// @note PrepareRequest() implementations should use this when available
  std::optional<FractionalRegionBounds> region_bounds;

  /// @brief Check if request is valid
  [[nodiscard]] bool IsValid() const noexcept { return level >= 0; }

  /// @brief Check if all channels are requested
  [[nodiscard]] bool IsAllChannels() const noexcept {
    return channel_indices.empty();
  }

  /// @brief Check if this is a region-based request
  [[nodiscard]] bool IsRegionRequest() const noexcept {
    return region_bounds.has_value();
  }

  /// @brief Default constructor
  TileRequest() : level(0), tile_coord{}, channel_indices{}, region_bounds{} {}

  /// @brief Constructor with level and coordinates
  TileRequest(int lvl, TileCoordinate coord)
      : level(lvl), tile_coord(coord), channel_indices{}, region_bounds{} {}

  /// @brief Constructor with level, coordinates, and channels
  TileRequest(int lvl, TileCoordinate coord, std::vector<size_t> channels)
      : level(lvl),
        tile_coord(coord),
        channel_indices(std::move(channels)),
        region_bounds{} {}

  /// @brief Constructor with region bounds (for RegionSpec conversion)
  TileRequest(int lvl, FractionalRegionBounds bounds)
      : level(lvl),
        tile_coord{0, 0},
        channel_indices{},
        region_bounds(bounds) {}
};

/// @brief Multi-tile request specification
///
/// Specifies a batch request for multiple tiles, useful for optimizing
/// I/O by reading multiple tiles in a single operation.
struct MultiTileRequest {
  int level;                                ///< Pyramid level
  std::vector<TileCoordinate> tile_coords;  ///< Tile coordinates
  std::vector<size_t> channel_indices;  ///< Requested channels (empty = all)

  /// @brief Check if request is valid
  [[nodiscard]] bool IsValid() const noexcept {
    return level >= 0 && !tile_coords.empty();
  }

  /// @brief Get number of tiles requested
  [[nodiscard]] size_t GetTileCount() const noexcept {
    return tile_coords.size();
  }

  /// @brief Default constructor
  MultiTileRequest() : level(0), tile_coords{}, channel_indices{} {}

  /// @brief Constructor with level and coordinates
  MultiTileRequest(int lvl, std::vector<TileCoordinate> coords)
      : level(lvl), tile_coords(std::move(coords)), channel_indices{} {}
};

}  // namespace core

// Import core types into fastslide namespace for backward compatibility
using core::MultiTileRequest;
using core::RegionSpec;
using core::TileCoordinate;
using core::TileRequest;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_TILE_REQUEST_H_
