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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SPATIAL_GRID_HASH_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SPATIAL_GRID_HASH_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace fastslide::spatial {

// Small epsilon so max lies in the same cell when aligned to the step.
inline constexpr double kCellEps = 1e-9;

struct CellRange {
  int32_t gx_min = 0;
  int32_t gx_max = -1;
  int32_t gy_min = 0;
  int32_t gy_max = -1;
};

[[nodiscard]] inline CellRange ComputeCellRange(double x0, double y0, double x1,
                                                double y1, double inv_step_x,
                                                double inv_step_y) {
  CellRange r{};
  r.gx_min = static_cast<int32_t>(std::floor(x0 * inv_step_x));
  r.gx_max = static_cast<int32_t>(std::floor((x1 - kCellEps) * inv_step_x));
  r.gy_min = static_cast<int32_t>(std::floor(y0 * inv_step_y));
  r.gy_max = static_cast<int32_t>(std::floor((y1 - kCellEps) * inv_step_y));
  return r;
}

template <typename MapT>
inline void IndexTileIntoCells(MapT& cell_index,
                               const std::array<double, 2>& bbox_min,
                               const std::array<double, 2>& bbox_max,
                               double inv_step_x, double inv_step_y,
                               size_t tile_idx) {
  const CellRange r = ComputeCellRange(bbox_min[0], bbox_min[1], bbox_max[0],
                                       bbox_max[1], inv_step_x, inv_step_y);
  for (int32_t gy = r.gy_min; gy <= r.gy_max; ++gy) {
    for (int32_t gx = r.gx_min; gx <= r.gx_max; ++gx) {
      cell_index[{gx, gy}].push_back(tile_idx);
    }
  }
}

// Thread-safe epoch generator used by MRXS.
[[nodiscard]] inline uint32_t NextEpoch(std::atomic<uint32_t>& query_epoch,
                                        std::vector<uint32_t>& seen_epoch,
                                        std::mutex& epoch_wrap_mutex) {
  uint32_t epoch = ++query_epoch;
  if (epoch == 0) {  // wrapped to 0
    std::lock_guard<std::mutex> lock(epoch_wrap_mutex);
    epoch = query_epoch.load(std::memory_order_acquire);
    if (epoch == 0) {
      std::fill(seen_epoch.begin(), seen_epoch.end(), 0);
      epoch = ++query_epoch;
    }
  }
  return epoch;
}

// Thread-local dedup helper used by CZI. Keeps per-thread state with low
// overhead and no locking.
class ThreadLocalEpochDeduper {
 public:
  ThreadLocalEpochDeduper() = default;

  void BeginQuery(size_t count) {
    if (seen_.size() != count) {
      seen_.assign(count, 0);
      epoch_ = 1;
    }
    ++epoch_;
    if (epoch_ == 0) {
      std::fill(seen_.begin(), seen_.end(), 0);
      epoch_ = 1;
    }
  }

  [[nodiscard]] bool MarkIfNew(size_t idx) {
    if (seen_[idx] == epoch_) {
      return false;
    }
    seen_[idx] = epoch_;
    return true;
  }

 private:
  std::vector<uint32_t> seen_;
  uint32_t epoch_ = 1;
};

}  // namespace fastslide::spatial

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SPATIAL_GRID_HASH_H_
