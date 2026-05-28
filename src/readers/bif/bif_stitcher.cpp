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

#include "fastslide/readers/bif/bif_stitcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {
namespace bif {

int SerpentineColumn(int tile1_one_based, int num_cols) {
  if (num_cols <= 0) {
    return 0;
  }
  const int idx = tile1_one_based - 1;
  const int row = idx / num_cols;
  const int col = idx % num_cols;
  if ((row & 1) == 1) {
    return num_cols - 1 - col;
  }
  return col;
}

namespace {

// A joint contributes to the grid spacing only when the scanner actually joined
// the pair and reported it at or above a high-confidence floor. The floor is
// the empirical knee of the confidence distribution observed in the sample
// corpus (see docs/source/formats/bif.rst, "Sample-file measurements"): real
// files carry a broad spread well below 100, so an equality test would discard
// almost every usable joint, while the long low-confidence tail is noise.
constexpr int kMinConfidence = 95;

// Image-space cell (col, row) of a 1-based serpentine tile index. The
// serpentine snake starts at the lower-left, so the snake-row counts up from
// the bottom and the image row is its mirror.
struct Cell {
  int col;
  int row;
};

[[nodiscard]] Cell SerpentineCell(int tile_one_based, int num_cols,
                                  int num_rows) {
  const int idx = tile_one_based - 1;
  const int snake_row = num_cols > 0 ? idx / num_cols : 0;
  return Cell{SerpentineColumn(tile_one_based, num_cols),
              num_rows - 1 - snake_row};
}

// Accumulates measured overlaps per grid boundary so each boundary can carry
// its own spacing. A boundary with no trusted measurement falls back to the
// AOI-wide mean, which keeps every tile on a consistent grid (no gaps) without
// smearing a sharp per-boundary overlap across the boundaries that do not have
// it.
struct BoundaryOverlaps {
  std::vector<double> sum;
  std::vector<int> count;
  double global_sum = 0.0;
  int global_count = 0;

  explicit BoundaryOverlaps(int boundary_count)
      : sum(std::max(boundary_count, 0), 0.0),
        count(std::max(boundary_count, 0), 0) {}

  void Add(int boundary, double overlap) {
    if (boundary >= 0 && boundary < static_cast<int>(sum.size())) {
      sum[boundary] += overlap;
      count[boundary] += 1;
    }
    global_sum += overlap;
    global_count += 1;
  }

  // Mean overlap at a boundary, or the AOI-wide mean when that boundary was
  // never measured (and 0 when nothing was measured at all).
  [[nodiscard]] double MeanAt(int boundary) const {
    if (boundary >= 0 && boundary < static_cast<int>(count.size()) &&
        count[boundary] > 0) {
      return sum[boundary] / count[boundary];
    }
    return global_count > 0 ? global_sum / global_count : 0.0;
  }
};

}  // namespace

aifocore::Result<StitchResult> StitchLevel0(const EncodeInfo& encode,
                                            uint32_t tile_w, uint32_t tile_h,
                                            uint32_t grid_cols) {
  if (tile_w == 0 || tile_h == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF stitch: zero tile size");
  }
  if (grid_cols == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF stitch: zero grid stride");
  }

  StitchResult result;
  result.tile_width = tile_w;
  result.tile_height = tile_h;

  const double tw = static_cast<double>(tile_w);
  const double th = static_cast<double>(tile_h);

  // The whitepaper ("Image stitching process") lays each AOI out as a regular
  // serpentine tile grid: tiles abut except for the measured overlap, where the
  // later tile overwrites the earlier one. Horizontally joined pairs (LEFT or
  // RIGHT) share OverlapX pixels along the column boundary between them;
  // vertically joined pairs (UP or DOWN) share OverlapY pixels along the row
  // boundary between them. We collect the overlaps per boundary, turn each
  // boundary into its own grid pitch (tile size minus the measured overlap, or
  // the AOI-wide mean where a boundary was not measured), and accumulate those
  // pitches into absolute column/row positions. Every tile is then placed at
  // its grid position, so the layout covers all tiles regardless of which
  // individual joints survived filtering, while a sharp overlap (e.g. a single
  // 24px seam among otherwise-abutting columns) lands only at its own boundary.
  for (const auto& aoi : encode.aois) {
    const int cols = aoi.num_cols;
    const int rows = aoi.num_rows;
    if (cols <= 0 || rows <= 0) {
      continue;
    }

    BoundaryOverlaps col_overlap(cols - 1);
    BoundaryOverlaps row_overlap(rows - 1);
    for (const auto& joint : aoi.joints) {
      if (!joint.flag_joined || joint.confidence < kMinConfidence) {
        continue;
      }
      const Cell a = SerpentineCell(joint.tile1, cols, rows);
      const Cell b = SerpentineCell(joint.tile2, cols, rows);
      switch (joint.direction) {
        case JointDirection::kLeft:
        case JointDirection::kRight:
          // Horizontal join: the pair shares OverlapX across the column
          // boundary between them (the lower of the two column indices).
          // Verified against the sample corpus (Ventana-1.bif emits LEFT,
          // OS-1.bif emits RIGHT).
          col_overlap.Add(std::min(a.col, b.col), joint.overlap_x);
          break;
        case JointDirection::kUp:
          // Vertical join: the pair shares OverlapY across the row boundary
          // between them (the lower of the two row indices). Verified against
          // the sample corpus (both Ventana-1.bif and OS-1.bif emit UP).
          row_overlap.Add(std::min(a.row, b.row), joint.overlap_y);
          break;
        case JointDirection::kDown:
          // DOWN is the mirror of UP: Tile2 sits one row below Tile1, and the
          // whitepaper defines OverlapY identically for both vertical
          // directions, so the whitepaper-correct geometry is the same shared
          // OverlapY across the row boundary between the two tiles:
          //
          //   row_overlap.Add(std::min(a.row, b.row), joint.overlap_y);
          //
          // No VENTANA DP 200 file in the measured corpus emits DOWN, so this
          // path has never been verified against real pixel data. Fail loudly
          // rather than silently return an unchecked layout (see
          // docs/source/formats/bif.rst, "Divergences from the Whitepaper").
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kUnimplemented,
              "BIF stitch: the DOWN tile-joint direction is implemented "
              "according to the whitepaper but has not been verified against a "
              "real VENTANA DP 200 file");
        case JointDirection::kUnknown:
          break;
      }
    }

    // Accumulate per-boundary pitches into absolute positions within the AOI.
    std::vector<double> col_x(cols, 0.0);
    for (int col = 1; col < cols; ++col) {
      col_x[col] = col_x[col - 1] + (tw - col_overlap.MeanAt(col - 1));
    }
    std::vector<double> row_y(rows, 0.0);
    for (int row = 1; row < rows; ++row) {
      row_y[row] = row_y[row - 1] + (th - row_overlap.MeanAt(row - 1));
    }

    // The AOI's top-left tile lands at its image-space grid origin
    // (OriginX/OriginY, always a multiple of the tile size).
    const int tile_col_start = static_cast<int>(std::lround(aoi.origin_x / tw));
    const int tile_row_start = static_cast<int>(std::lround(aoi.origin_y / th));
    const double anchor_x = tile_col_start * tw;
    const double anchor_y = tile_row_start * th;

    for (int row = 0; row < rows; ++row) {
      for (int col = 0; col < cols; ++col) {
        Level0Tile tile;
        tile.x = anchor_x + col_x[col];
        tile.y = anchor_y + row_y[row];
        tile.gx = static_cast<uint32_t>(tile_col_start + col);
        tile.gy = static_cast<uint32_t>(tile_row_start + row);
        result.tiles.push_back(tile);
      }
    }
  }

  if (result.tiles.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF stitch: no placed tiles");
  }

  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();
  for (const auto& tile : result.tiles) {
    min_x = std::min(min_x, tile.x);
    min_y = std::min(min_y, tile.y);
    max_x = std::max(max_x, tile.x + tw);
    max_y = std::max(max_y, tile.y + th);
  }

  for (auto& tile : result.tiles) {
    tile.x -= min_x;
    tile.y -= min_y;
  }

  result.level0_width = static_cast<uint32_t>(std::ceil(max_x - min_x));
  result.level0_height = static_cast<uint32_t>(std::ceil(max_y - min_y));
  return result;
}

}  // namespace bif
}  // namespace fastslide
