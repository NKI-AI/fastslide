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

#include "fastslide/readers/olympusvsi/olympusvsi_plan_builder.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide::formats::olympusvsi {

namespace {

/// @brief Clamp an ETS background component into the 8-bit Canvas
/// background channel.
///
/// ``BackgroundColor`` (and `Canvas::Config`) only carries an 8-bit RGBA
/// fill triple, so for UINT16 stacks we down-shift each 16-bit
/// background sample to its top byte. ``Canvas`` then re-expands that
/// byte back to a 16-bit value via the ``0x0101`` Cairo/lodepng scale
/// (see `canvas_config.cpp`), keeping greys consistent across both
/// dtype paths.
uint8_t BackgroundComponentToByte(int32_t value, TilePixelType pixel_type) {
  if (pixel_type == TilePixelType::kUInt16) {
    return static_cast<uint8_t>(std::clamp(value, 0, 65535) >> 8);
  }
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

void FillOutputFromEtsHeader(const EtsHeader& ets, uint32_t n_channels,
                             core::OutputSpec* output) {
  output->channels = n_channels;
  output->channel_indices.resize(n_channels);
  for (uint32_t ch = 0; ch < n_channels; ++ch) {
    output->channel_indices[ch] = ch;
  }
  // Background carries up to four 8-bit fill components. For grayscale /
  // spectral fluorescence we still narrow the per-channel ETS background
  // (typically zero) through the same path; the Canvas re-expands it.
  output->background.r =
      BackgroundComponentToByte(ets.background[0], ets.pixel_type);
  output->background.g =
      BackgroundComponentToByte(ets.background[1], ets.pixel_type);
  output->background.b =
      BackgroundComponentToByte(ets.background[2], ets.pixel_type);
  output->background.a =
      (n_channels >= 4)
          ? BackgroundComponentToByte(ets.background[3], ets.pixel_type)
          : 255;
}

}  // namespace

aifocore::Result<core::TilePlan> OlympusVsiPlanBuilder::BuildPlan(
    const core::TileRequest& request,
    std::span<const OlympusVsiLevelInfo> pyramid, const EtsHeader& ets) {
  core::TilePlan plan;
  plan.request = request;

  if (pyramid.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Olympus VSI: empty pyramid");
  }
  if (request.level < 0 ||
      static_cast<size_t>(request.level) >= pyramid.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Olympus VSI: invalid level {}", request.level));
  }

  const auto& level = pyramid[request.level];
  // Clamp reads to the logical image (true sub-tile boundary). For `.vsi`
  // inputs this is the boundary-rect (tag 2053) size; for `.ets`-only inputs
  // it equals the tile-grid extent, so trailing-tile padding past the real
  // image edge is never returned.
  const readers::simpletiff_plan::Dimensions2D level_dims{
      level.reported_size[0], level.reported_size[1]};

  const bool is_16bit = ets.pixel_type == TilePixelType::kUInt16;
  // Channel layout:
  //   * 8-bit brightfield → decoder-native RGB (contiguous), channel
  //     count from the ETS header (clamped to [1, 4]).
  //   * 16-bit fluorescence → one output channel per materialised
  //     grayscale plane (`level.n_channels`). With more than one plane
  //     the channels are independent fluorophores, so we use a
  //     separate-planar, spectral-tagged uint16 canvas; a single plane
  //     stays contiguous grayscale.
  const uint32_t n_channels = is_16bit
                                  ? std::max(1u, level.n_channels)
                                  : std::max(1u, std::min(ets.n_channels, 4u));
  const bool separate_planar = is_16bit && n_channels > 1U;

  plan.output.pixel_format = is_16bit ? core::OutputSpec::PixelFormat::kUInt16
                                      : core::OutputSpec::PixelFormat::kUInt8;
  plan.output.planar_config =
      separate_planar ? PlanarConfig::kSeparate : PlanarConfig::kContiguous;
  plan.output.force_spectral_image = separate_planar;
  FillOutputFromEtsHeader(ets, n_channels, &plan.output);

  const auto bounds =
      readers::simpletiff_plan::DetermineRegionBounds(request, level_dims);
  const auto region =
      readers::simpletiff_plan::ClampRegionToLevel(bounds, level_dims);
  plan.output.dimensions = {region.width, region.height};
  plan.actual_region = {.top_left = {region.x, region.y},
                        .size = {region.width, region.height},
                        .level = request.level};
  if (region.outside || region.width == 0 || region.height == 0) {
    return plan;
  }

  if (level.tile_w == 0 || level.tile_h == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Olympus VSI: level has zero tile dimensions");
  }

  // Pick the tile map for the requested focal (Z) / time (T) plane. An
  // out-of-range plane selects no tiles, so the canvas paints background.
  const OlympusVsiLevelInfo::TileMap* tile_map =
      level.MapForPlane(request.plane.z, request.plane.t);
  if (tile_map == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Olympus VSI: requested plane (z={}, t={}) is out of range "
            "(z_count={}, t_count={})",
            request.plane.z, request.plane.t, level.z_count, level.t_count));
  }

  const uint32_t first_x = region.x / level.tile_w;
  const uint32_t first_y = region.y / level.tile_h;
  const uint32_t last_x = (region.x + region.width - 1) / level.tile_w;
  const uint32_t last_y = (region.y + region.height - 1) / level.tile_h;

  uint64_t total_bytes = 0;
  for (uint32_t ty = first_y; ty <= last_y; ++ty) {
    for (uint32_t tx = first_x; tx <= last_x; ++tx) {
      const uint32_t tile_left = tx * level.tile_w;
      const uint32_t tile_top = ty * level.tile_h;
      const uint32_t tile_right =
          std::min(tile_left + level.tile_w, level.reported_size[0]);
      const uint32_t tile_bottom =
          std::min(tile_top + level.tile_h, level.reported_size[1]);
      const uint32_t inter_left = std::max(tile_left, region.x);
      const uint32_t inter_top = std::max(tile_top, region.y);
      const uint32_t inter_right =
          std::min(tile_right, region.x + region.width);
      const uint32_t inter_bottom =
          std::min(tile_bottom, region.y + region.height);
      if (inter_left >= inter_right || inter_top >= inter_bottom) {
        continue;
      }

      const uint32_t src_x = inter_left - tile_left;
      const uint32_t src_y = inter_top - tile_top;
      const uint32_t dst_x = inter_left - region.x;
      const uint32_t dst_y = inter_top - region.y;
      const uint32_t copy_w = inter_right - inter_left;
      const uint32_t copy_h = inter_bottom - inter_top;

      // One op per present channel plane. Single-channel stacks emit a
      // single op with destination channel 0; multi-plane fluorescence
      // emits one op per plane, each carrying its destination channel in
      // ``channel_group_offset`` so the planar Canvas knows where the
      // decoded grayscale tile lands.
      for (uint32_t ch = 0; ch < n_channels; ++ch) {
        const uint64_t key = OlympusVsiLevelInfo::PackKey3(ch, tx, ty);
        const auto it = tile_map->find(key);
        if (it == tile_map->end()) {
          // Missing cell for this plane: background fill via the canvas.
          continue;
        }
        const LevelTileEntry& entry = it->second;

        core::TileReadOp op;
        op.level = request.level;
        op.tile_coord = {tx, ty};
        op.source_id = 0;
        op.byte_offset = entry.offset;
        op.byte_size = entry.n_bytes;
        op.channel_group_offset = ch;
        op.transform.source = {static_cast<double>(src_x),
                               static_cast<double>(src_y), copy_w, copy_h};
        op.transform.dest = {static_cast<double>(dst_x),
                             static_cast<double>(dst_y), copy_w, copy_h};
        plan.operations.push_back(op);
        total_bytes += entry.n_bytes;
      }
    }
  }

  plan.cost.total_tiles = plan.operations.size();
  plan.cost.total_bytes_to_read = total_bytes;
  return plan;
}

}  // namespace fastslide::formats::olympusvsi
