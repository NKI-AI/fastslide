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
#include <span>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/runtime/tile_writer/clip_region.h"
#include "fastslide/runtime/tile_writer/direct/copy_planar.h"
#include "fastslide/runtime/tile_writer/direct/copy_rgb8.h"

namespace fastslide::runtime {

aifocore::Status Canvas::PaintTilePlanar(const core::TileReadOp& operation,
                                         std::span<const uint8_t> pixel_data,
                                         uint32_t tile_width,
                                         uint32_t tile_height) {
  const auto& src = operation.transform.source;
  const auto& dst = operation.transform.dest;
  uint8_t* image_data = output_image_->GetData();
  const uint32_t bytes_per_sample = output_image_->GetBytesPerSample();
  const uint32_t target_channel = operation.tile_coord.x;

  if (target_channel >= config_.channels) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format(
            "Target channel {} exceeds image channel count {}", target_channel,
            config_.channels));
  }

  tile_writer_internal::ClippedRegion clip{};
  if (!tile_writer_internal::ClipPaintRegion(
          src, dst, static_cast<int>(tile_width), static_cast<int>(tile_height),
          static_cast<int>(config_.dimensions[0]),
          static_cast<int>(config_.dimensions[1]), clip)) {
    return aifocore::Status::OkStatus();
  }

  CopyTilePlanar(
      pixel_data.data(), static_cast<int>(tile_width),
      static_cast<int>(tile_height), clip.src_x, clip.src_y, image_data,
      static_cast<int>(config_.dimensions[0]),
      static_cast<int>(config_.dimensions[1]), clip.dst_x, clip.dst_y,
      clip.copy_w, clip.copy_h, static_cast<int>(target_channel),
      static_cast<int>(config_.channels), static_cast<int>(bytes_per_sample));

  return aifocore::Status::OkStatus();
}

aifocore::Status Canvas::PaintTileInterleaved(
    const core::TileReadOp& operation, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, uint32_t tile_channels) {
  const auto& src = operation.transform.source;
  const auto& dst = operation.transform.dest;
  uint8_t* image_data = output_image_->GetData();
  const uint32_t bytes_per_sample = output_image_->GetBytesPerSample();

  tile_writer_internal::ClippedRegion clip{};
  if (!tile_writer_internal::ClipPaintRegion(
          src, dst, static_cast<int>(tile_width), static_cast<int>(tile_height),
          static_cast<int>(config_.dimensions[0]),
          static_cast<int>(config_.dimensions[1]), clip)) {
    return aifocore::Status::OkStatus();
  }

  // Fast path: full-tile copy of an RGB8 brightfield tile, with the
  // destination origin already inside the canvas (no clipping happened).
  const bool fast_path = (config_.channels == 3) && (tile_channels == 3) &&
                         (bytes_per_sample == 1) && (clip.src_x == 0) &&
                         (clip.src_y == 0) &&
                         (clip.copy_w == static_cast<int>(tile_width)) &&
                         (clip.copy_h == static_cast<int>(tile_height));

  if (fast_path) {
    CopyTileRectRGB8(pixel_data.data(), static_cast<int>(tile_width),
                     static_cast<int>(tile_height), 0, 0, image_data,
                     static_cast<int>(config_.dimensions[0]),
                     static_cast<int>(config_.dimensions[1]), clip.dst_x,
                     clip.dst_y, clip.copy_w, clip.copy_h);
    return aifocore::Status::OkStatus();
  }

  CopyRectGeneral(pixel_data.data(), static_cast<int>(tile_width),
                  static_cast<int>(tile_height),
                  static_cast<int>(tile_channels), clip.src_x, clip.src_y,
                  image_data, static_cast<int>(config_.dimensions[0]),
                  static_cast<int>(config_.dimensions[1]),
                  static_cast<int>(config_.channels), clip.dst_x, clip.dst_y,
                  clip.copy_w, clip.copy_h, static_cast<int>(bytes_per_sample));

  return aifocore::Status::OkStatus();
}

}  // namespace fastslide::runtime
