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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_STITCHER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_STITCHER_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/bif/bif_xml.h"

/**
 * @file bif_stitcher.h
 * @brief Level-0 reconstruction for Roche VENTANA BIF snapshot mosaics.
 *
 * Each AOI is a serpentine grid of snapshot tiles placed at its stage position
 * (`Pos-X`/`Pos-Y`, relative to the left-/top-most AOI; `Pos-Y` is measured
 * upwards from below all AOIs). Within an AOI, tile positions follow from the
 * `<TileJointInfo>` overlaps, whose meaning depends on the scanner generation
 * (`OverlapModel`):
 *
 *   - pre-DP 200 (iScan) joints carry the full overlap of neighbouring tiles,
 *     so every tile advances by the tile size plus the confidence-weighted
 *     mean joint offset (`kMeanAdvance`);
 *   - VENTANA DP 200 joints carry per-tile corrections to the nominal tile
 *     size (mostly 0), so tiles advance by the tile size and each keeps the
 *     offset of its own joint, accumulated along its row and column
 *     (`kPerTile`); level sizes are rounded up to a multiple of 16 px.
 *
 * This is the layout of OpenSlide's ventana driver, with the DP 200 changes of
 * openslide/openslide#395.
 *
 * Higher pyramid levels reuse this geometry scaled by `1/downsample`; that
 * scaling lives in the spatial index builder, not here.
 */

namespace fastslide {
namespace bif {

/// @brief One placed snapshot tile at level 0 (sub-pixel position).
struct Level0Tile {
  double x = 0.0;   ///< Top-left X in level-0 output coordinates (px).
  double y = 0.0;   ///< Top-left Y in level-0 output coordinates (px).
  uint32_t gx = 0;  ///< Tile column in the IFD-2 TIFF tile grid.
  uint32_t gy = 0;  ///< Tile row in the IFD-2 TIFF tile grid.
};

/// @brief Result of stitching level 0.
struct StitchResult {
  std::vector<Level0Tile> tiles;
  uint32_t tile_width = 0;
  uint32_t tile_height = 0;
  uint32_t level0_width = 0;   ///< Stitched bounding-box width.
  uint32_t level0_height = 0;  ///< Stitched bounding-box height.
  /// `OverlapModel::kPerTile`: every level's size is the right/bottom extent of
  /// the placed tiles / downsample, rounded up to a multiple of 16 px
  /// (`RoundUpToMultipleOf16`).
  bool align16 = false;
  double extent_x = 0.0;
  double extent_y = 0.0;
};

/// @brief Compute the serpentine image-column of a 1-based snapshot index.
///
/// `Tile1` indices snake left-to-right on even rows and right-to-left on odd
/// rows (see bif.rst "Coordinate Systems").
[[nodiscard]] int SerpentineColumn(int tile1_one_based, int num_cols);

/// @brief How `<TileJointInfo>` overlaps turn into tile positions.
enum class OverlapModel {
  /// Every tile advances by the tile size plus the confidence-weighted mean
  /// joint offset (pre-DP 200 scanners).
  kMeanAdvance,
  /// Tiles advance by the tile size and keep the offset of their own joint,
  /// accumulated along the row and column; level sizes are rounded up to a
  /// multiple of 16 px (VENTANA DP 200).
  kPerTile,
};

/// @brief The overlap model of a scanner (`<iScan ScannerModel>`).
[[nodiscard]] OverlapModel OverlapModelFor(std::string_view scanner_model);

/// @brief Round a level extent up to whole pixels, then to a multiple of 16
/// (the DP 200 level size, see `StitchResult::align16`).
[[nodiscard]] uint32_t RoundUpToMultipleOf16(double extent);

/// @brief Reconstruct the level-0 tile placement from IFD-2 stitch metadata.
///
/// @param encode  Parsed `<EncodeInfo>` (per-AOI grids, positions, joints).
/// @param tile_w  Physical tile width in pixels (IFD 2 TileWidth).
/// @param tile_h  Physical tile height in pixels (IFD 2 TileLength).
/// @param model   How the joint overlaps are applied (`OverlapModelFor`).
/// @return The placed tiles and the stitched level-0 dimensions.
[[nodiscard]] aifocore::Result<StitchResult> StitchLevel0(
    const EncodeInfo& encode, uint32_t tile_w, uint32_t tile_h,
    OverlapModel model);

}  // namespace bif
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_STITCHER_H_
