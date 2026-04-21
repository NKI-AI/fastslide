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

#include "fastslide/readers/omezarr/omezarr_plan_builder.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

namespace {

uint64_t EncodeChunk(uint64_t chunk_y, uint64_t chunk_x) {
  return (chunk_y << 32) | (chunk_x & 0xFFFFFFFFULL);
}

/// @brief The channel-chunk index ("cc") for a source channel.
///
/// OME-Zarr stores channels in `chunk_c`-sized blocks along the c axis, so the
/// chunk that contains source channel `s` is `s / chunk_c`. Returns 0 for
/// arrays without a channel axis or with degenerate `chunk_c`.
uint64_t ChannelChunkIndex(const OmeZarrLevelInfo& level,
                           size_t source_channel) {
  if (level.c_axis == static_cast<size_t>(-1) || level.chunk_c == 0)
    return 0;
  return static_cast<uint64_t>(source_channel) / level.chunk_c;
}

}  // namespace

aifocore::Result<core::TilePlan> OmeZarrPlanBuilder::BuildPlan(
    const core::TileRequest& request,
    const std::vector<OmeZarrLevelInfo>& pyramid,
    PlanarConfig output_planar_config, DataType data_type,
    const std::vector<size_t>& visible_channels) {
  core::TilePlan plan;
  plan.request = request;

  if (request.level < 0 ||
      static_cast<size_t>(request.level) >= pyramid.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid OME-Zarr level: {}", request.level));
  }

  const auto& level = pyramid[request.level];
  const readers::simpletiff_plan::Dimensions2D level_dims{
      static_cast<uint32_t>(level.x_size), static_cast<uint32_t>(level.y_size)};

  std::vector<size_t> source_channels;
  if (!request.channel_indices.empty()) {
    source_channels = request.channel_indices;
  } else if (!visible_channels.empty()) {
    source_channels = visible_channels;
  } else {
    source_channels.reserve(level.c_size);
    for (size_t i = 0; i < level.c_size; ++i)
      source_channels.push_back(i);
  }
  for (size_t ch : source_channels) {
    if (ch >= level.c_size) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kOutOfRange,
          aifocore::fmt::format("Channel index {} >= channel count {}", ch,
                                level.c_size));
    }
  }

  plan.output.pixel_format = core::ToOutputPixelFormat(data_type);
  plan.output.planar_config = output_planar_config;
  plan.output.background = {0, 0, 0, 255};
  plan.output.channel_indices = source_channels;
  plan.output.channels = static_cast<uint32_t>(source_channels.size());

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

  const uint64_t chunk_w = level.chunk_x;
  const uint64_t chunk_h = level.chunk_y;
  if (chunk_w == 0 || chunk_h == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "OME-Zarr level has zero-size chunk dimensions");
  }

  const uint64_t first_chunk_x = region.x / chunk_w;
  const uint64_t first_chunk_y = region.y / chunk_h;
  const uint64_t last_chunk_x =
      (static_cast<uint64_t>(region.x) + region.width - 1) / chunk_w;
  const uint64_t last_chunk_y =
      (static_cast<uint64_t>(region.y) + region.height - 1) / chunk_h;

  // Deduplicate channel-chunk indices: when chunk_c > 1 every source channel
  // in the same channel-chunk block lives inside the same on-disk chunk, so we
  // only need to read/decode that chunk once.
  std::set<uint64_t> needed_ccs;
  for (size_t source_channel : source_channels) {
    needed_ccs.insert(ChannelChunkIndex(level, source_channel));
  }

  const uint32_t per_chunk_bytes = static_cast<uint32_t>(std::min<uint64_t>(
      level.BytesPerChunk(), std::numeric_limits<uint32_t>::max()));

  for (uint64_t cc : needed_ccs) {
    for (uint64_t cy = first_chunk_y; cy <= last_chunk_y; ++cy) {
      for (uint64_t cx = first_chunk_x; cx <= last_chunk_x; ++cx) {
        const uint64_t chunk_left = cx * chunk_w;
        const uint64_t chunk_top = cy * chunk_h;
        const uint64_t chunk_right =
            std::min(chunk_left + chunk_w, level.x_size);
        const uint64_t chunk_bottom =
            std::min(chunk_top + chunk_h, level.y_size);
        const uint64_t inter_left = std::max<uint64_t>(chunk_left, region.x);
        const uint64_t inter_top = std::max<uint64_t>(chunk_top, region.y);
        const uint64_t inter_right =
            std::min<uint64_t>(chunk_right, region.x + region.width);
        const uint64_t inter_bottom =
            std::min<uint64_t>(chunk_bottom, region.y + region.height);
        if (inter_left >= inter_right || inter_top >= inter_bottom)
          continue;

        core::TileReadOp op;
        op.level = request.level;
        // tile_coord.x is filled in per output channel by the executor; we
        // leave it at zero. tile_coord.y carries chunk_x for cache keying.
        op.tile_coord.x = 0;
        op.tile_coord.y = static_cast<uint32_t>(cx);
        op.source_id = static_cast<uint32_t>(cc);
        op.byte_offset = EncodeChunk(cy, cx);
        op.byte_size = per_chunk_bytes;

        const uint32_t src_x = static_cast<uint32_t>(inter_left - chunk_left);
        const uint32_t src_y = static_cast<uint32_t>(inter_top - chunk_top);
        const uint32_t dst_x = static_cast<uint32_t>(inter_left - region.x);
        const uint32_t dst_y = static_cast<uint32_t>(inter_top - region.y);
        const uint32_t copy_w = static_cast<uint32_t>(inter_right - inter_left);
        const uint32_t copy_h = static_cast<uint32_t>(inter_bottom - inter_top);
        op.transform.source = {static_cast<double>(src_x),
                               static_cast<double>(src_y), copy_w, copy_h};
        op.transform.dest = {static_cast<double>(dst_x),
                             static_cast<double>(dst_y), copy_w, copy_h};
        plan.operations.push_back(op);
      }
    }
  }

  plan.cost.total_tiles = plan.operations.size();
  plan.cost.total_bytes_to_read =
      plan.operations.size() * level.BytesPerChunk();
  return plan;
}

}  // namespace fastslide
