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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_ENVELOPES_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_ENVELOPES_H_

#include <cstdint>
#include <span>
#include <vector>

#include "fastslide/core/slide_descriptor.h"

namespace fastslide {

/// @brief Valid data envelopes of an iSyntax image, as the Philips SDK
/// reports them.
///
/// An envelope is an orthogonal polygon around a region of the slide that
/// holds data. iSyntax v2 headers list them; for v1 the SDK takes the box
/// around every group of connected level 0 tiles present in the file.
///
/// At every level the SDK erodes each polygon like the level range (see
/// IsyntaxPlanBuilder::ComputeValidRange): edges where the polygon starts
/// move by `ceil(v / 2) + 1`, edges where it ends by `floor(v / 2) - 1`. It
/// then cuts the polygon into vertical strips at the x of its vertices, and
/// reports the strips as rectangles.
class IsyntaxEnvelopes {
 public:
  struct Point {
    int64_t x = 0;
    int64_t y = 0;
  };

  /// @brief Vertices of an orthogonal polygon, in any order. Pixels from the
  /// smallest x / y of the polygon are in it, up to the largest excluded.
  using Polygon = std::vector<Point>;

  /// @brief Envelopes of an iSyntax v2 header, in pixels of the padded level
  /// 0 grid. Their API origin is `level0_padding` pixels into the grid.
  static IsyntaxEnvelopes FromHeader(std::vector<Polygon> grid_polygons,
                                     int32_t level0_padding);

  /// @brief Envelopes of an iSyntax v1 image: the box around every group of
  /// connected level 0 tiles present, clipped to the image.
  ///
  /// @param present Presence of every level 0 tile, row major.
  /// @param width_in_tiles Width of the tile grid.
  /// @param tile_width Tile width, in pixels.
  /// @param tile_height Tile height, in pixels.
  /// @param level0_padding Padding of level 0, in level 0 pixels.
  /// @param width Width of level 0, in pixels.
  /// @param height Height of level 0, in pixels.
  static IsyntaxEnvelopes FromTiles(std::span<const uint8_t> present,
                                    int64_t width_in_tiles, int64_t tile_width,
                                    int64_t tile_height, int32_t level0_padding,
                                    int64_t width, int64_t height);

  [[nodiscard]] bool empty() const { return polygons_.empty(); }

  /// @brief Box around every envelope, at level 0, clipped to the image.
  /// Invalid (empty) when there is no envelope or the box misses the image.
  [[nodiscard]] SlideBounds Bounds(int64_t width, int64_t height) const;

  /// @brief Rectangles of the envelopes at @p level, in Philips SDK pixels
  /// of that level.
  [[nodiscard]] std::vector<SlideBounds> Regions(int32_t level) const;

 private:
  explicit IsyntaxEnvelopes(std::vector<Polygon> polygons)
      : polygons_(std::move(polygons)) {}

  /// Level 0 polygons, in API pixels.
  std::vector<Polygon> polygons_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_ENVELOPES_H_
