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

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/runtime/tile_writer/pixel_ops.h"

namespace fastslide::runtime {

aifocore::Status Canvas::PaintTileRgb8Blended(
    const core::TileReadOp& op, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, uint32_t tile_channels) {
  if (tile_channels != 3) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "RGB8-blended canvas requires 3-channel tiles");
  }

  const size_t pixel_count = static_cast<size_t>(tile_width) * tile_height;
  const size_t expected_bytes = pixel_count * 3;
  if (pixel_data.size() < expected_bytes) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Tile buffer smaller than declared dimensions");
  }

  const float gain =
      op.blend_metadata.has_value() ? op.blend_metadata->gain : 1.0F;

  const uint8_t* src = pixel_data.data();
  thread_local std::vector<uint8_t> gain_scratch;
  if (std::abs(gain - 1.0F) >= 1e-4F) {
    gain_scratch.resize(expected_bytes);
    pixel::ApplyGainLinear(src, gain_scratch.data(), pixel_count, gain);
    src = gain_scratch.data();
  }

  const double dest_x = op.transform.dest.x;
  const double dest_y = op.transform.dest.y;
  const double src_x = op.transform.source.x;
  const double src_y = op.transform.source.y;
  const int dest_w = static_cast<int>(op.transform.dest.width);
  const int dest_h = static_cast<int>(op.transform.dest.height);
  const int tw = static_cast<int>(tile_width);
  const int th = static_cast<int>(tile_height);

  const double frac_x = dest_x - std::floor(dest_x);
  const double frac_y = dest_y - std::floor(dest_y);
  const bool is_integer_pos = (frac_x < 1e-9 && frac_y < 1e-9);
  const bool needs_subtile =
      (src_x != 0.0 || src_y != 0.0 || dest_w != tw || dest_h != th);

  if (is_integer_pos && !needs_subtile) {
    RgbBlitT<uint8_t>(src, tw, th, static_cast<int>(dest_x),
                      static_cast<int>(dest_y));
  } else if (is_integer_pos && needs_subtile) {
    RgbBlitOffsetT<uint8_t>(src, tw, th, static_cast<int>(src_x),
                            static_cast<int>(src_y), dest_w, dest_h,
                            static_cast<int>(dest_x), static_cast<int>(dest_y));
  } else {
    BilinearRgbBlit(src, tw, th, dest_x, dest_y, src_x, src_y, dest_w, dest_h);
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status Canvas::PaintTileRgb16Copy(const core::TileReadOp& op,
                                            std::span<const uint8_t> pixel_data,
                                            uint32_t tile_width,
                                            uint32_t tile_height,
                                            uint32_t tile_channels) {
  if (tile_channels != 3) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "RGB16-copy canvas requires 3-channel tiles");
  }

  const size_t pixel_count = static_cast<size_t>(tile_width) * tile_height;
  const size_t expected_bytes = pixel_count * 3 * sizeof(uint16_t);
  if (pixel_data.size() < expected_bytes) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Tile buffer smaller than declared 16-bit dimensions");
  }

  const uint16_t* src = reinterpret_cast<const uint16_t*>(pixel_data.data());

  // 16-bit MRXS fluorescence: integer position, no bilinear, no gain.
  // Fractional MRXS positions are floored to integer; this is intentional
  // (see "blend_mode_16bit = integer_only_16" in the design plan).
  const int dest_x = static_cast<int>(std::floor(op.transform.dest.x));
  const int dest_y = static_cast<int>(std::floor(op.transform.dest.y));
  const int src_x = static_cast<int>(std::floor(op.transform.source.x));
  const int src_y = static_cast<int>(std::floor(op.transform.source.y));
  const int dest_w = static_cast<int>(op.transform.dest.width);
  const int dest_h = static_cast<int>(op.transform.dest.height);
  const int tw = static_cast<int>(tile_width);
  const int th = static_cast<int>(tile_height);

  const bool needs_subtile =
      (src_x != 0 || src_y != 0 || dest_w != tw || dest_h != th);

  if (!needs_subtile) {
    RgbBlitT<uint16_t>(src, tw, th, dest_x, dest_y);
  } else {
    RgbBlitOffsetT<uint16_t>(src, tw, th, src_x, src_y, dest_w, dest_h, dest_x,
                             dest_y);
  }
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide::runtime
