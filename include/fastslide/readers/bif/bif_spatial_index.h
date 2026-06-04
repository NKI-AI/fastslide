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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_SPATIAL_INDEX_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_SPATIAL_INDEX_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/bif/bif_stitcher.h"
#include "fastslide/utilities/unordered_dense.h"

/**
 * @file bif_spatial_index.h
 * @brief Grid-hash spatial index over the placed BIF snapshot tiles.
 *
 * A separate index is built per pyramid level: the level-0 geometry from
 * `StitchResult` is scaled by the level's integer (dyadic) downsample, and each
 * placed tile is mapped to a physical tile + sub-rectangle of the level's IFD,
 * exactly as specified in bif.rst ("Re-stitching a higher level").
 */

namespace fastslide {
namespace bif {

/// @brief A placed tile ready to read and paint at a given level.
struct SpatialTile {
  uint32_t source_page = 0;      ///< IFD to read this level's pixels from.
  uint32_t tiff_tile_index = 0;  ///< Linear tile index within `source_page`.
  double dest_x = 0.0;           ///< Placement X in level coordinates.
  double dest_y = 0.0;           ///< Placement Y in level coordinates.
  uint32_t dest_w = 0;           ///< Drawn width in level coordinates.
  uint32_t dest_h = 0;           ///< Drawn height in level coordinates.
  double src_x = 0.0;  ///< Sub-rect X within the decoded tile (fractional).
  double src_y = 0.0;  ///< Sub-rect Y within the decoded tile (fractional).
  uint32_t src_w = 0;  ///< Sub-rect width.
  uint32_t src_h = 0;  ///< Sub-rect height.
};

/// @brief Grid-hash spatial index for a single pyramid level.
class BifSpatialIndex {
 public:
  /// @brief Build the index for one level by scaling the level-0 stitch.
  ///
  /// @param stitch          Level-0 reconstruction.
  /// @param source_page     IFD index to read this level's pixels from.
  /// @param downsample      Integer (dyadic) downsample factor for this level.
  /// @param grid_cols_level TIFF tile-grid stride of `source_page`
  ///                        (`ImageWidth / tile_w`).
  [[nodiscard]] static aifocore::Result<std::unique_ptr<BifSpatialIndex>> Build(
      const StitchResult& stitch, uint32_t source_page, uint32_t downsample,
      uint32_t grid_cols_level);

  /// @brief Return indices of tiles whose placement intersects the region.
  [[nodiscard]] std::vector<size_t> QueryRegion(double x, double y,
                                                double width,
                                                double height) const;

  [[nodiscard]] const std::vector<SpatialTile>& tiles() const { return tiles_; }

 private:
  BifSpatialIndex() = default;

  ankerl::unordered_dense::map<std::pair<int32_t, int32_t>, std::vector<size_t>>
      cell_index_;
  std::vector<SpatialTile> tiles_;
  double inv_step_x_ = 1.0;
  double inv_step_y_ = 1.0;
};

}  // namespace bif
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_SPATIAL_INDEX_H_
