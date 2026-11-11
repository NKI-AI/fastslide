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
#include <string_view>
#include <utility>
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

}  // namespace

OverlapModel OverlapModelFor(std::string_view scanner_model) {
  return scanner_model == "VENTANA DP 200" ? OverlapModel::kPerTile
                                           : OverlapModel::kMeanAdvance;
}

uint32_t RoundUpToMultipleOf16(double extent) {
  return static_cast<uint32_t>(std::ceil(std::ceil(extent) / 16.0) * 16.0);
}

aifocore::Result<StitchResult> StitchLevel0(const EncodeInfo& encode,
                                            uint32_t tile_w, uint32_t tile_h,
                                            OverlapModel model) {
  if (tile_w == 0 || tile_h == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF stitch: zero tile size");
  }
  const double tw = static_cast<double>(tile_w);
  const double th = static_cast<double>(tile_h);
  const bool per_tile = model == OverlapModel::kPerTile;

  struct Area {
    const AoiInfo* aoi;
    double x;
    double y;
    std::vector<double> off_x;  // per tile (row-major), from its joint
    std::vector<double> off_y;
  };

  // Joint offsets (-Overlap), per tile and confidence-weighted over the slide.
  std::vector<Area> areas;
  double sum_x = 0.0;
  double sum_y = 0.0;
  double weight_x = 0.0;
  double weight_y = 0.0;
  for (const auto& aoi : encode.aois) {
    const int cols = aoi.num_cols;
    const int rows = aoi.num_rows;
    if (cols <= 0 || rows <= 0) {
      continue;
    }
    Area area{&aoi, aoi.pos_x, aoi.pos_y,
              std::vector<double>(static_cast<size_t>(cols) * rows, 0.0),
              std::vector<double>(static_cast<size_t>(cols) * rows, 0.0)};
    for (const auto& joint : aoi.joints) {
      if (joint.tile1 < 1 || joint.tile2 < 1 || joint.tile1 > cols * rows ||
          joint.tile2 > cols * rows) {
        continue;
      }
      const Cell a = SerpentineCell(joint.tile1, cols, rows);
      const Cell b = SerpentineCell(joint.tile2, cols, rows);
      switch (joint.direction) {
        case JointDirection::kLeft:
        case JointDirection::kRight:
          // The offset belongs to the right-hand tile of the pair (Tile2).
          area.off_x[static_cast<size_t>(b.row) * cols + b.col] =
              -joint.overlap_x;
          sum_x += joint.confidence * -joint.overlap_x;
          weight_x += joint.confidence;
          break;
        case JointDirection::kUp:
          // The offset belongs to the upper tile of the pair (Tile1).
          area.off_y[static_cast<size_t>(a.row) * cols + a.col] =
              -joint.overlap_y;
          sum_y += joint.confidence * -joint.overlap_y;
          weight_y += joint.confidence;
          break;
        case JointDirection::kDown:
          // No sample file emits DOWN, so its geometry has never been checked
          // against real pixel data (docs/source/formats/bif.rst, "Divergences
          // from the Whitepaper").
          return AIFOCORE_MAKE_STATUS(
              aifocore::StatusCode::kUnimplemented,
              "BIF stitch: the DOWN tile-joint direction has not been verified "
              "against a real BIF file");
        case JointDirection::kUnknown:
          break;
      }
    }
    areas.push_back(std::move(area));
  }
  if (areas.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF stitch: no placed tiles");
  }

  // kMeanAdvance: every tile advances by the tile size plus the mean joint
  // offset. kPerTile: tiles advance by the tile size and carry their own
  // joint's offset, accumulated along the row and column.
  const double adv_x = per_tile || weight_x <= 0.0 ? tw : tw + sum_x / weight_x;
  const double adv_y = per_tile || weight_y <= 0.0 ? th : th + sum_y / weight_y;

  // AOIs are placed by their stage position. Pos-Y is the distance from the
  // bottom of the AOI to a point below all AOIs, so flip it.
  double top = 0.0;
  for (const auto& area : areas) {
    top = std::max(top, area.y + (area.aoi->num_rows - 1) * adv_y + th);
  }
  double start_x = std::numeric_limits<double>::infinity();
  double start_y = std::numeric_limits<double>::infinity();
  for (auto& area : areas) {
    area.y = top - area.y - ((area.aoi->num_rows - 1) * adv_y + th);
    start_x = std::min(start_x, area.x);
    start_y = std::min(start_y, area.y);
  }

  StitchResult result;
  result.tile_width = tile_w;
  result.tile_height = tile_h;
  result.align16 = per_tile;
  for (const auto& area : areas) {
    const int cols = area.aoi->num_cols;
    const int rows = area.aoi->num_rows;
    // Tile-grid cell of the AOI's first tile in the IFD-2 TIFF (AoiOrigin).
    const int start_col =
        static_cast<int>(std::lround(area.aoi->origin_x / tw));
    const int start_row =
        static_cast<int>(std::lround(area.aoi->origin_y / th));
    const double area_x = area.x - (start_col * adv_x + start_x);
    const double area_y = area.y - (start_row * adv_y + start_y);
    std::vector<double> offset_y(cols, area_y);
    for (int row = 0; row < rows; ++row) {
      double offset_x = area_x;
      for (int col = 0; col < cols; ++col) {
        if (per_tile) {
          offset_x += area.off_x[static_cast<size_t>(row) * cols + col];
          offset_y[col] += area.off_y[static_cast<size_t>(row) * cols + col];
        }
        Level0Tile tile;
        tile.gx = static_cast<uint32_t>(start_col + col);
        tile.gy = static_cast<uint32_t>(start_row + row);
        tile.x = (start_col + col) * adv_x + offset_x;
        tile.y = (start_row + row) * adv_y + offset_y[col];
        result.extent_x = std::max(result.extent_x, tile.x + tw);
        result.extent_y = std::max(result.extent_y, tile.y + th);
        result.tiles.push_back(tile);
      }
    }
  }
  if (per_tile) {
    result.level0_width = RoundUpToMultipleOf16(result.extent_x);
    result.level0_height = RoundUpToMultipleOf16(result.extent_y);
  } else {
    result.level0_width = static_cast<uint32_t>(std::ceil(result.extent_x));
    result.level0_height = static_cast<uint32_t>(std::ceil(result.extent_y));
  }
  return result;
}

}  // namespace bif
}  // namespace fastslide
