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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TILE_UTILITIES_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TILE_UTILITIES_H_

#include <cstdint>
#include <iterator>

#include "aifocore/concepts/numeric.h"
#include "fastslide/core/tile_request.h"

namespace fastslide {
namespace tiff {

/// @brief Type alias for tile coordinates
using TileCoordinate = aifocore::Size<uint32_t, 2>;

/// @brief Iterator for 2D tile coordinates within a region
///
/// This iterator yields tile coordinates (x, y) that intersect with a given region,
/// following C++20 iterator concepts for use with ranges and algorithms.
class TileCoordinateIterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = TileCoordinate;
  using pointer = const value_type*;
  using reference = const value_type&;

  /// @brief Constructor for iterating within bounds
  /// @param start_x Starting X coordinate (aligned to tile grid)
  /// @param start_y Starting Y coordinate (aligned to tile grid)
  /// @param end_x Ending X coordinate (exclusive)
  /// @param end_y Ending Y coordinate (exclusive)
  /// @param tile_width Tile width for stepping
  /// @param tile_height Tile height for stepping
  TileCoordinateIterator(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                         uint32_t end_y, uint32_t tile_width,
                         uint32_t tile_height);

  /// @brief End iterator constructor
  TileCoordinateIterator();

  /// @brief Dereference operator
  reference operator*() const;

  /// @brief Arrow operator
  pointer operator->() const;

  /// @brief Pre-increment operator
  TileCoordinateIterator& operator++();

  /// @brief Post-increment operator
  TileCoordinateIterator operator++(int);

  /// @brief Equality comparison
  friend bool operator==(const TileCoordinateIterator& a,
                         const TileCoordinateIterator& b);

  /// @brief Inequality comparison
  friend bool operator!=(const TileCoordinateIterator& a,
                         const TileCoordinateIterator& b);

 private:
  uint32_t current_x_, current_y_;
  uint32_t start_x_, end_x_, end_y_;
  uint32_t tile_width_, tile_height_;
  value_type current_coord_;
  bool is_end_;
};

/// @brief Range view for tile coordinates within a region
class TileCoordinateRange {
 public:
  /// @brief Constructor
  /// @param region Region specification
  /// @param tile_dims Tile dimensions
  TileCoordinateRange(const RegionSpec& region,
                      const aifocore::Size<uint32_t, 2>& tile_dims);

  /// @brief Begin iterator
  TileCoordinateIterator begin() const;

  /// @brief End iterator
  TileCoordinateIterator end() const;

 private:
  uint32_t start_x_, start_y_, end_x_, end_y_;
  uint32_t tile_width_, tile_height_;
};

/// @brief Copy tile data from TIFF buffer to output buffer
///
/// This function handles the intersection calculation and memory copying
/// for extracting a region from a tile buffer into an output buffer.
///
/// @param tile_buffer Source tile buffer
/// @param output_buffer Destination buffer
/// @param tile_width Tile width in pixels
/// @param tile_height Tile height in pixels
/// @param tile_x Tile X coordinate in image space
/// @param tile_y Tile Y coordinate in image space
/// @param region_x Region X coordinate in image space
/// @param region_y Region Y coordinate in image space
/// @param region_width Region width in pixels
/// @param region_height Region height in pixels
/// @param bytes_per_pixel Bytes per pixel
void CopyTileToBuffer(const uint8_t* tile_buffer, uint8_t* output_buffer,
                      uint32_t tile_width, uint32_t tile_height,
                      uint32_t tile_x, uint32_t tile_y, uint32_t region_x,
                      uint32_t region_y, uint32_t region_width,
                      uint32_t region_height, uint32_t bytes_per_pixel);

}  // namespace tiff
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TILE_UTILITIES_H_
