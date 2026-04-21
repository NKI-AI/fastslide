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

#include "fastslide/readers/dicom/dicom_plan_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/dicom/dicom.h"

namespace fastslide {

namespace {

aifocore::Status ValidateRequest(const core::TileRequest& request,
                                 const DicomReader& reader) {
  if (request.level < 0 || request.level >= reader.GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }
  return aifocore::Status::OkStatus();
}

void DetermineRegionBounds(const core::TileRequest& request,
                           const LevelInfo& level_info, double& x, double& y,
                           uint32_t& width, uint32_t& height) {
  if (request.IsRegionRequest() && request.region_bounds->IsValid()) {
    x = request.region_bounds->x;
    y = request.region_bounds->y;
    width = static_cast<uint32_t>(std::ceil(request.region_bounds->width));
    height = static_cast<uint32_t>(std::ceil(request.region_bounds->height));
  } else {
    x = 0.0;
    y = 0.0;
    width = level_info.dimensions[0];
    height = level_info.dimensions[1];
  }
}

core::OutputSpec CreateOutputSpec(uint32_t width, uint32_t height) {
  core::OutputSpec spec;
  spec.dimensions = {width, height};
  spec.channels = 3;
  spec.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  spec.background = {255, 255, 255, 255};
  return spec;
}

}  // namespace

aifocore::Result<core::TilePlan> DicomPlanBuilder::BuildPlan(
    const core::TileRequest& request, const DicomReader& reader) {
  core::TilePlan plan;
  plan.request = request;

  AIFOCORE_RETURN_IF_ERROR(ValidateRequest(request, reader));
  AIFOCORE_ASSIGN_OR_RETURN(const auto level_info,
                            reader.GetLevelInfo(request.level));

  double x = 0.0;
  double y = 0.0;
  uint32_t width = 0;
  uint32_t height = 0;
  DetermineRegionBounds(request, level_info, x, y, width, height);

  if (width == 0 || height == 0) {
    plan.output = CreateOutputSpec(width, height);
    plan.actual_region = {
        .top_left = {0, 0}, .size = {width, height}, .level = request.level};
    return plan;
  }

  // Clamp to level bounds.
  if (x >= static_cast<double>(level_info.dimensions[0]) ||
      y >= static_cast<double>(level_info.dimensions[1])) {
    plan.output = CreateOutputSpec(width, height);
    plan.actual_region = {
        .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
        .size = {width, height},
        .level = request.level};
    return plan;
  }
  if (x + width > level_info.dimensions[0]) {
    width = level_info.dimensions[0] - static_cast<uint32_t>(x);
  }
  if (y + height > level_info.dimensions[1]) {
    height = level_info.dimensions[1] - static_cast<uint32_t>(y);
  }

  const auto& level = reader.GetLevel(request.level);
  const uint32_t tile_w = level.tile_w;
  const uint32_t tile_h = level.tile_h;

  // Compute the tile grid range that overlaps the requested region.
  const uint32_t col_start = static_cast<uint32_t>(x) / tile_w;
  const uint32_t row_start = static_cast<uint32_t>(y) / tile_h;
  const uint32_t col_end =
      (static_cast<uint32_t>(x) + width + tile_w - 1) / tile_w;
  const uint32_t row_end =
      (static_cast<uint32_t>(y) + height + tile_h - 1) / tile_h;

  const uint32_t tiles_across = (level.width + tile_w - 1) / tile_w;
  const uint32_t tiles_down = (level.height + tile_h - 1) / tile_h;

  std::vector<core::TileReadOp> ops;
  ops.reserve(static_cast<size_t>(col_end - col_start) * (row_end - row_start));

  for (uint32_t row = row_start; row < row_end && row < tiles_down; ++row) {
    for (uint32_t col = col_start; col < col_end && col < tiles_across; ++col) {
      const double tile_x = static_cast<double>(col * tile_w);
      const double tile_y = static_cast<double>(row * tile_h);

      // Actual tile dimensions (handle edge tiles).
      const uint32_t actual_tile_w =
          std::min(tile_w, level.width - col * tile_w);
      const uint32_t actual_tile_h =
          std::min(tile_h, level.height - row * tile_h);

      const double dest_x = tile_x - x;
      const double dest_y = tile_y - y;

      // Tile completely outside the region — skip.
      if (dest_x + actual_tile_w <= 0.0 || dest_y + actual_tile_h <= 0.0 ||
          dest_x >= width || dest_y >= height) {
        continue;
      }

      core::TileReadOp op{};
      op.level = request.level;
      // Encode DICOM frame position (column, row) in tile_coord.
      op.tile_coord = {col, row};
      op.source_id = 0;
      op.byte_offset = 0;
      op.byte_size = 0;

      op.transform.source = {0.0, 0.0, actual_tile_w, actual_tile_h};
      op.transform.dest = {dest_x, dest_y, actual_tile_w, actual_tile_h};

      core::BlendMetadata blend{};
      blend.weight = 1.0;
      blend.gain = 1.0f;
      blend.mode = core::BlendMode::kAverage;
      op.blend_metadata = blend;

      ops.push_back(op);
    }
  }

  plan.operations = std::move(ops);
  plan.output = CreateOutputSpec(width, height);
  plan.actual_region = {
      .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
      .size = {width, height},
      .level = request.level};

  return plan;
}

}  // namespace fastslide
