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
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/bif/bif_xml.h"

/**
 * @file bif_stitcher.h
 * @brief Level-0 reconstruction for Roche VENTANA BIF snapshot mosaics.
 *
 * Implements the "Reconstruction algorithm" of `docs/source/formats/bif.rst`,
 * following the whitepaper's "Image stitching process": each AOI is a regular
 * serpentine tile grid where neighbouring snapshots abut except for a measured
 * overlap. Horizontally joined pairs (`LEFT`/`RIGHT`) share `OverlapX` pixels
 * along the column boundary between them and vertically joined pairs
 * (`UP`/`DOWN`) share `OverlapY` pixels along the row boundary between them.
 * The overlaps are collected per grid boundary so each boundary carries its own
 * pitch (tile size minus its measured overlap, or the AOI-wide mean where a
 * boundary was not measured); those pitches accumulate into absolute column and
 * row positions. Every tile is placed at its grid position, so the layout
 * covers all tiles regardless of which individual joints survived confidence
 * filtering, while a sharp single-boundary overlap stays at its own boundary
 * instead of being smeared across the grid.
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
};

/// @brief Compute the serpentine image-column of a 1-based snapshot index.
///
/// `Tile1` indices snake left-to-right on even rows and right-to-left on odd
/// rows (see bif.rst "Coordinate Systems").
[[nodiscard]] int SerpentineColumn(int tile1_one_based, int num_cols);

/// @brief Reconstruct the level-0 tile placement from IFD-2 stitch metadata.
///
/// @param encode     Parsed `<EncodeInfo>` (per-AOI grids + joints).
/// @param tile_w     Physical tile width in pixels (IFD 2 TileWidth).
/// @param tile_h     Physical tile height in pixels (IFD 2 TileLength).
/// @param grid_cols  IFD-2 TIFF tile-grid stride (`ImageWidth / tile_w`).
/// @return The placed tiles and the stitched level-0 dimensions.
[[nodiscard]] aifocore::Result<StitchResult> StitchLevel0(
    const EncodeInfo& encode, uint32_t tile_w, uint32_t tile_h,
    uint32_t grid_cols);

}  // namespace bif
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_STITCHER_H_
