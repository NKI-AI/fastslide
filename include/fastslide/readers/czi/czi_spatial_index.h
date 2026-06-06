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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_SPATIAL_INDEX_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_SPATIAL_INDEX_H_

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/spatial/grid_hash.h"
#include "fastslide/utilities/unordered_dense.h"

namespace fastslide {
namespace czi {

/// @brief Simple bounding box for 2D coordinates.
struct Box {
  std::array<double, 2> min;
  std::array<double, 2> max;
};

/// @brief Minimal tile info needed for planning and caching.
struct TileInfo {
  uint32_t subblock_index = 0;  ///< Index into reader's subblock array.
  uint32_t width = 0;           ///< Tile pixel width (level coordinates).
  uint32_t height = 0;          ///< Tile pixel height (level coordinates).
  uint32_t channel = 0;         ///< Output channel plane (0 for RGB scenes).
};

/// @brief Spatial tile record.
struct SpatialTile {
  TileInfo info;
  Box bbox;  ///< Bounding box in level coordinates (double).
};

/// @brief Spatial index for CZI tiles.
///
/// CZI tiles are positioned in level-0 coordinates and must be mapped into
/// level coordinates by dividing by the downsample factor. This yields
/// fractional tile origins when the tile origin isn't a multiple of the
/// downsample. The plan builder needs this fractional information to avoid
/// seams when stitching regions.
///
/// This index uses a grid-hash strategy similar to MRXS but with a simpler
/// cell size selection (derived from max tile dimension in the level).
class CziSpatialIndex {
 public:
  static aifocore::Result<std::shared_ptr<CziSpatialIndex>> Build(
      std::vector<SpatialTile> tiles, double step);

  [[nodiscard]] std::vector<size_t> QueryRegion(double x, double y,
                                                double width,
                                                double height) const;

  [[nodiscard]] const std::vector<SpatialTile>& GetTiles() const {
    return tiles_;
  }

  [[nodiscard]] double GetStep() const { return step_; }

 private:
  CziSpatialIndex(ankerl::unordered_dense::map<std::pair<int32_t, int32_t>,
                                               std::vector<size_t>>
                      cell_index,
                  std::vector<SpatialTile> tiles, double step);

  ankerl::unordered_dense::map<std::pair<int32_t, int32_t>, std::vector<size_t>>
      cell_index_;
  std::vector<SpatialTile> tiles_;
  double step_ = 1.0;
  double inv_step_ = 1.0;
};

}  // namespace czi
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_SPATIAL_INDEX_H_
