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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

namespace fastslide::runtime {

namespace {

/// @brief Multiply without wrapping.
/// @return False when the product would exceed `size_t`.
bool CheckedMul(size_t a, size_t b, size_t* out) {
  if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
    return false;
  }
  *out = a * b;
  return true;
}

}  // namespace

aifocore::Status Canvas::PaintTileLocked(const core::TileReadOp& op,
                                         std::span<const uint8_t> pixel_data,
                                         uint32_t tile_width,
                                         uint32_t tile_height,
                                         uint32_t tile_channels) {
  if (!output_image_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Canvas has null image pointer");
  }

  // The paint sinks below take a bare pointer and derive every source offset
  // from the declared tile geometry, which originates in file metadata. If the
  // decoded buffer is shorter than that geometry implies, they read past its
  // end and the stale bytes land in the image handed back to the caller.
  // Reconcile the two here, once, before any sink sees the pointer.
  if (tile_channels == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Tile declares zero channels");
  }

  // `CopyTilePlanar` strides the source as a single-channel plane; every other
  // sink reads interleaved samples.
  const bool planar_plane_source =
      !use_rgb8_blending_ && !use_rgb16_copy_blending_ &&
      config_.planar_config == PlanarConfig::kSeparate;
  const uint32_t source_channels = planar_plane_source ? 1U : tile_channels;

  size_t required = output_image_->GetBytesPerSample();
  const bool fits = CheckedMul(required, tile_width, &required) &&
                    CheckedMul(required, tile_height, &required) &&
                    CheckedMul(required, source_channels, &required);
  if (!fits) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Tile geometry {}x{}x{} overflows an address-space-sized buffer",
            tile_width, tile_height, source_channels));
  }
  if (pixel_data.size() < required) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Tile buffer holds {} bytes but declared geometry {}x{}x{} at {} "
            "bytes/sample requires {}",
            pixel_data.size(), tile_width, tile_height, source_channels,
            output_image_->GetBytesPerSample(), required));
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
