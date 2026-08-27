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

#include "fastslide/runtime/tile_writer.h"

#include <cstdint>
#include <mutex>
#include <span>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

namespace fastslide::runtime {

aifocore::Status Canvas::PaintTileLocked(const core::TileReadOp& op,
                                         std::span<const uint8_t> pixel_data,
                                         uint32_t tile_width,
                                         uint32_t tile_height,
                                         uint32_t tile_channels) {
  if (!output_image_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Canvas has null image pointer");
  }

  if (use_rgb8_blending_) {
    return PaintTileRgb8Blended(op, pixel_data, tile_width, tile_height,
                                tile_channels);
  }

  if (use_rgb16_copy_blending_) {
    return PaintTileRgb16Copy(op, pixel_data, tile_width, tile_height,
                              tile_channels);
  }

  // PaintTilePlanar / PaintTileInterleaved both clip the source/dest
  // rectangles internally via ClipPaintRegion, so out-of-canvas portions of
  // a tile (negative dest origin, or destination extending past the canvas
  // border) are silently dropped instead of raised as errors. This is the
  // common case for sub-tile reads such as the 1x1 pixel inspector probe.
  if (config_.planar_config == PlanarConfig::kSeparate) {
    return PaintTilePlanar(op, pixel_data, tile_width, tile_height);
  }
  return PaintTileInterleaved(op, pixel_data, tile_width, tile_height,
                              tile_channels);
}

aifocore::Status Canvas::PaintTile(const core::TileReadOp& op,
                                   std::span<const uint8_t> tile_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   uint32_t tile_channels) {
  return PaintTileLocked(op, tile_data, tile_width, tile_height, tile_channels);
}

aifocore::Status Canvas::PaintTile(const core::TileReadOp& op,
                                   std::span<const uint8_t> tile_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   uint32_t tile_channels,
                                   std::mutex& accumulator_mutex) {
  std::lock_guard<std::mutex> lock(accumulator_mutex);
  return PaintTileLocked(op, tile_data, tile_width, tile_height, tile_channels);
}

}  // namespace fastslide::runtime
