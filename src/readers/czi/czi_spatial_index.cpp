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

#include "fastslide/readers/czi/czi_spatial_index.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"

namespace fastslide {
namespace czi {

CziSpatialIndex::CziSpatialIndex(
    ankerl::unordered_dense::map<std::pair<int32_t, int32_t>,
                                 std::vector<size_t>>
        cell_index,
    std::vector<SpatialTile> tiles, double step)
    : cell_index_(std::move(cell_index)),
      tiles_(std::move(tiles)),
      step_(step),
      inv_step_(1.0 / step) {}

aifocore::Result<std::shared_ptr<CziSpatialIndex>> CziSpatialIndex::Build(
    std::vector<SpatialTile> tiles, double step) {
  if (tiles.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Cannot build CZI spatial index from empty list");
  }
  if (step <= 0.0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid CZI spatial index step");
  }

  ankerl::unordered_dense::map<std::pair<int32_t, int32_t>, std::vector<size_t>>
      cell_index;
  cell_index.reserve(tiles.size());

  const double inv_step = 1.0 / step;

  for (size_t idx = 0; idx < tiles.size(); ++idx) {
    const auto& bbox = tiles[idx].bbox;
    spatial::IndexTileIntoCells(cell_index, bbox.min, bbox.max, inv_step,
                                inv_step, idx);
  }

  return std::shared_ptr<CziSpatialIndex>(
      new CziSpatialIndex(std::move(cell_index), std::move(tiles), step));
}

std::vector<size_t> CziSpatialIndex::QueryRegion(double x, double y,
                                                 double width,
                                                 double height) const {
  const double qx0 = x;
  const double qy0 = y;
  const double qx1 = x + width;
  const double qy1 = y + height;

  const spatial::CellRange r =
      spatial::ComputeCellRange(qx0, qy0, qx1, qy1, inv_step_, inv_step_);

  std::vector<size_t> out;
  out.reserve(64);

  // Deduplicate with a cheap thread-local epoch vector (sizes are modest for
  // CZI).
  static thread_local spatial::ThreadLocalEpochDeduper deduper;
  deduper.BeginQuery(tiles_.size());

  for (int32_t gy = r.gy_min; gy <= r.gy_max; ++gy) {
    for (int32_t gx = r.gx_min; gx <= r.gx_max; ++gx) {
      auto it = cell_index_.find({gx, gy});
      if (it == cell_index_.end()) {
        continue;
      }
      for (size_t idx : it->second) {
        if (!deduper.MarkIfNew(idx)) {
          continue;
        }

        const auto& bbox = tiles_[idx].bbox;
        if (bbox.max[0] <= qx0 || bbox.min[0] >= qx1 || bbox.max[1] <= qy0 ||
            bbox.min[1] >= qy1) {
          continue;
        }
        out.push_back(idx);
      }
    }
  }

  return out;
}

}  // namespace czi
}  // namespace fastslide
