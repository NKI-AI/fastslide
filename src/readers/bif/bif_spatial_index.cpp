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

#include "fastslide/readers/bif/bif_spatial_index.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/spatial/grid_hash.h"

namespace fastslide {
namespace bif {

aifocore::Result<std::unique_ptr<BifSpatialIndex>> BifSpatialIndex::Build(
    const StitchResult& stitch, uint32_t source_page, uint32_t downsample,
    uint32_t grid_cols_level) {
  if (downsample == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF spatial index: zero downsample");
  }
  if (grid_cols_level == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF spatial index: zero grid stride");
  }

  const uint32_t d = downsample;
  const double dd = static_cast<double>(d);

  // Each physical tile in directory L (= level0 / d) packs d*d level-0 tiles,
  // each downsampled to (tile_w/d, tile_h/d).
  //
  // Sub-cell *origin* is the FRACTIONAL k*full/d - it is deliberately not
  // rounded. Rounding it would shift the painted content by up to half a pixel;
  // because tile_h/d is non-integral for d>=32 (1360/32 = 42.5) the rounding
  // error differs per level, which makes tile seams *drift* between levels.
  // Keeping the fractional origin holds a given source feature at the same
  // scene position across all levels.
  //
  // Sub-cell *size* is ceil(full/d) (uniform). Sub-cells are read at source
  // step full/d but reconstructed at destination step adv/d (smaller, tiles
  // overlap). At the coarsest levels that overlap is sub-pixel; ceil(full/d) >=
  // the largest destination step, so neighbouring tiles always overlap and no
  // 1px gaps (white seam rows) open up. The extra overlap is duplicated content
  // resolved by last-writer-wins; reads past the physical tile edge are clamped
  // by the blit.
  const double sub_w_d = std::max(1.0, std::ceil(stitch.tile_width / dd));
  const double sub_h_d = std::max(1.0, std::ceil(stitch.tile_height / dd));
  const auto sub_w = static_cast<uint32_t>(sub_w_d);
  const auto sub_h = static_cast<uint32_t>(sub_h_d);

  auto index = std::unique_ptr<BifSpatialIndex>(new BifSpatialIndex());
  index->tiles_.reserve(stitch.tiles.size());

  // Grid-hash cell size: the nominal downsampled tile footprint.
  const double step_x =
      std::max(1.0, static_cast<double>(stitch.tile_width) / dd);
  const double step_y =
      std::max(1.0, static_cast<double>(stitch.tile_height) / dd);
  index->inv_step_x_ = 1.0 / step_x;
  index->inv_step_y_ = 1.0 / step_y;

  for (const auto& t : stitch.tiles) {
    SpatialTile st;
    st.source_page = source_page;
    const uint32_t phys_tx = t.gx / d;
    const uint32_t phys_ty = t.gy / d;
    st.tiff_tile_index = phys_ty * grid_cols_level + phys_tx;
    st.src_x = static_cast<double>(t.gx % d) * stitch.tile_width / dd;
    st.src_y = static_cast<double>(t.gy % d) * stitch.tile_height / dd;
    st.src_w = sub_w;
    st.src_h = sub_h;
    st.dest_x = t.x / dd;
    st.dest_y = t.y / dd;
    st.dest_w = sub_w;
    st.dest_h = sub_h;

    const size_t tile_idx = index->tiles_.size();
    index->tiles_.push_back(st);

    const std::array<double, 2> bbox_min{st.dest_x, st.dest_y};
    const std::array<double, 2> bbox_max{st.dest_x + st.dest_w,
                                         st.dest_y + st.dest_h};
    spatial::IndexTileIntoCells(index->cell_index_, bbox_min, bbox_max,
                                index->inv_step_x_, index->inv_step_y_,
                                tile_idx);
  }

  return index;
}

std::vector<size_t> BifSpatialIndex::QueryRegion(double x, double y,
                                                 double width,
                                                 double height) const {
  std::vector<size_t> result;
  if (width <= 0.0 || height <= 0.0 || tiles_.empty()) {
    return result;
  }

  const double x1 = x + width;
  const double y1 = y + height;
  const spatial::CellRange range =
      spatial::ComputeCellRange(x, y, x1, y1, inv_step_x_, inv_step_y_);

  // Gather candidate indices from all overlapped cells, then dedup. Building a
  // fresh result vector per call keeps QueryRegion thread-safe without shared
  // mutable state.
  for (int32_t gy = range.gy_min; gy <= range.gy_max; ++gy) {
    for (int32_t gx = range.gx_min; gx <= range.gx_max; ++gx) {
      auto it = cell_index_.find({gx, gy});
      if (it == cell_index_.end()) {
        continue;
      }
      for (size_t idx : it->second) {
        const SpatialTile& t = tiles_[idx];
        const bool intersects = t.dest_x < x1 && t.dest_x + t.dest_w > x &&
                                t.dest_y < y1 && t.dest_y + t.dest_h > y;
        if (intersects) {
          result.push_back(idx);
        }
      }
    }
  }

  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

}  // namespace bif
}  // namespace fastslide
