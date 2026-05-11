// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_CLIP_REGION_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_CLIP_REGION_H_

#include <algorithm>
#include <cmath>

#include "fastslide/core/tile_plan.h"

namespace fastslide::runtime::tile_writer_internal {

/// @brief Clipped tile-to-canvas paint rectangle.
struct ClippedRegion {
  int src_x;
  int src_y;
  int dst_x;
  int dst_y;
  int copy_w;
  int copy_h;
};

/// @brief Clip a tile->canvas paint rectangle so neither endpoint underflows.
///
/// `TileTransform::DestRegion` stores `dest.x`/`dest.y` as doubles which can
/// be negative when a tile straddles the upper/left edge of the read region
/// (e.g. when reading a 1x1 region from inside a 256x256 tile). The legacy
/// flow truncated to `uint32_t`, which both produced an underflowed huge
/// offset and triggered an out-of-range error, silently dropping the paint.
///
/// This helper computes the integer source/destination origin and copy
/// extent that are simultaneously inside the tile and inside the canvas.
/// Returns `false` when the regions do not intersect at all (caller should
/// skip the paint without raising an error).
inline bool ClipPaintRegion(const core::TileTransform::SourceRegion& src,
                            const core::TileTransform::DestRegion& dst,
                            int tile_w, int tile_h, int img_w, int img_h,
                            ClippedRegion& out) {
  // Use floor for source, since src.x/src.y can be sub-pixel for MRXS tile
  // overlaps. Source coordinates are non-negative by construction (tile-
  // local subregion offsets), so a simple cast is fine.
  int src_x = static_cast<int>(std::floor(src.x));
  int src_y = static_cast<int>(std::floor(src.y));
  int dst_x = static_cast<int>(std::floor(dst.x));
  int dst_y = static_cast<int>(std::floor(dst.y));
  int copy_w = static_cast<int>(dst.width);
  int copy_h = static_cast<int>(dst.height);

  // Clip top-left: when dst is negative, advance src and shrink extent.
  if (dst_x < 0) {
    src_x -= dst_x;
    copy_w += dst_x;
    dst_x = 0;
  }
  if (dst_y < 0) {
    src_y -= dst_y;
    copy_h += dst_y;
    dst_y = 0;
  }
  if (src_x < 0) {
    dst_x -= src_x;
    copy_w += src_x;
    src_x = 0;
  }
  if (src_y < 0) {
    dst_y -= src_y;
    copy_h += src_y;
    src_y = 0;
  }

  // Clip bottom-right against both tile and canvas extents.
  copy_w = std::min(copy_w, tile_w - src_x);
  copy_w = std::min(copy_w, img_w - dst_x);
  copy_h = std::min(copy_h, tile_h - src_y);
  copy_h = std::min(copy_h, img_h - dst_y);

  if (copy_w <= 0 || copy_h <= 0) {
    return false;
  }

  out = {src_x, src_y, dst_x, dst_y, copy_w, copy_h};
  return true;
}

}  // namespace fastslide::runtime::tile_writer_internal

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_CLIP_REGION_H_
