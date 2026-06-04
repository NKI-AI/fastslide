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

#include "fastslide/readers/mrxs/mrxs_plan_builder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/readers/mrxs/spatial_index.h"
#include "fastslide/slide_reader.h"

namespace fastslide {

aifocore::Result<core::TilePlan> MrxsPlanBuilder::BuildPlan(
    const core::TileRequest& request, const MrxsPlanContext& context) {

  core::TilePlan plan;
  plan.request = request;

  // Validate request
  AIFOCORE_RETURN_IF_ERROR(ValidateRequest(request, context));

  // Get level info
  const auto& level_info = context.level_info;

  // Get spatial index for this level
  const auto& index = context.spatial_index;

  // Determine region bounds from request
  double x, y;
  uint32_t width, height;
  DetermineRegionBounds(request, level_info, x, y, width, height);

  // Query tiles that intersect with the region
  auto tile_indices = index->QueryRegion(x, y, width, height);

  // Get slide info for output specification
  const auto& slide_info = context.slide_info;
  const auto& zoom_level = slide_info.zoom_levels[request.level];

  if (tile_indices.empty()) {
    // No tiles found - return empty plan
    plan.output = CreateOutputSpec(width, height, zoom_level, slide_info);
    plan.actual_region = {
        .top_left = {0, 0}, .size = {width, height}, .level = request.level};
    return plan;
  }

  // Create tile operations. The 8-bit RGB brightfield path still needs the
  // sRGB-space `gain` carried in BlendMetadata; the 16-bit fluorescence
  // path drops it so the Canvas falls into the integer copy-with-coverage
  // branch instead of the bilinear+gain blender.
  const bool emit_blend_metadata = slide_info.camera_bitdepth < 16;
  auto operations = CreateTileOperations(request, *index, x, y, width, height,
                                         emit_blend_metadata);
  plan.operations = std::move(operations);

  // Set output specification
  plan.output = CreateOutputSpec(width, height, zoom_level, slide_info);

  // Set actual region
  plan.actual_region = {
      .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
      .size = {width, height},
      .level = request.level};

  return plan;
}

aifocore::Status MrxsPlanBuilder::ValidateRequest(
    const core::TileRequest& request, const MrxsPlanContext& context) {

  if (request.level < 0 ||
      request.level >=
          static_cast<int>(context.slide_info.zoom_levels.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }

  return aifocore::Status::OkStatus();
}

void MrxsPlanBuilder::DetermineRegionBounds(const core::TileRequest& request,
                                            const LevelInfo& level_info,
                                            double& x, double& y,
                                            uint32_t& width, uint32_t& height) {

  if (request.IsRegionRequest() && request.region_bounds->IsValid()) {
    // Use fractional region bounds from request
    x = request.region_bounds->x;
    y = request.region_bounds->y;
    width = static_cast<uint32_t>(std::ceil(request.region_bounds->width));
    height = static_cast<uint32_t>(std::ceil(request.region_bounds->height));
  } else {
    // Default to full level dimensions
    x = 0.0;
    y = 0.0;
    width = level_info.dimensions[0];
    height = level_info.dimensions[1];
  }
}

std::vector<core::TileReadOp> MrxsPlanBuilder::CreateTileOperations(
    const core::TileRequest& request,
    const mrxs::MrxsSpatialIndex& spatial_index, double x, double y,
    uint32_t width, uint32_t height, bool emit_blend_metadata) {

  auto tile_indices = spatial_index.QueryRegion(x, y, width, height);
  const auto& spatial_tiles = spatial_index.GetSpatialTiles();

  std::vector<core::TileReadOp> operations;
  operations.reserve(tile_indices.size());

  for (size_t idx : tile_indices) {
    const auto& spatial_tile = spatial_tiles[idx];
    auto op_opt = CreateTileOperation(request, spatial_tile, x, y, width,
                                      height, emit_blend_metadata);
    if (op_opt) {
      operations.push_back(*op_opt);
    }
  }

  return operations;
}

std::optional<core::TileReadOp> MrxsPlanBuilder::CreateTileOperation(
    const core::TileRequest& request, const mrxs::SpatialTile& spatial_tile,
    double x, double y, uint32_t width, uint32_t height,
    bool emit_blend_metadata) {

  const auto& tile = spatial_tile.tile_info;

  core::TileReadOp op;
  op.level = request.level;
  op.tile_coord = {static_cast<uint32_t>(tile.x),
                   static_cast<uint32_t>(tile.y)};

  // Source information from tile metadata
  op.source_id = tile.data_file_number;
  op.byte_offset = tile.offset;
  op.byte_size = tile.length;

  // Each PNG stores up to 3 channels (R/G/B planes). For >3 channel slides
  // the channel_group_index tells the executor where to drop those planes
  // in the multi-channel planar output (channels [3*g .. 3*g + 2]).
  op.channel_group_offset =
      static_cast<uint32_t>(tile.channel_group_index) * 3U;

  const double dest_x = spatial_tile.bbox.min[0] - x;
  const double dest_y = spatial_tile.bbox.min[1] - y;

  const uint32_t src_width =
      static_cast<uint32_t>(std::ceil(spatial_tile.tile_width));
  const uint32_t src_height =
      static_cast<uint32_t>(std::ceil(spatial_tile.tile_height));

  // Quick reject: tile entirely outside the output region.
  if (dest_x + src_width <= 0.0 || dest_y + src_height <= 0.0 ||
      dest_x >= width || dest_y >= height) {
    return std::nullopt;
  }

  op.transform.source = {tile.subregion_x, tile.subregion_y, src_width,
                         src_height};
  op.transform.dest = {dest_x, dest_y, src_width, src_height};

  if (emit_blend_metadata) {
    core::BlendMetadata blend;
    blend.weight = 1.0;
    blend.gain = tile.gain;
    blend.mode = core::BlendMode::kOverwrite;
    op.blend_metadata = blend;
  }

  return op;
}

core::OutputSpec MrxsPlanBuilder::CreateOutputSpec(
    uint32_t width, uint32_t height, const mrxs::SlideZoomLevel& zoom_level,
    const mrxs::SlideDataInfo& slide_info) {

  const bool is_fluorescence =
      slide_info.slide_type == mrxs::MrxsSlideType::kFluorescence &&
      !slide_info.filters.empty();
  const size_t n_channels =
      is_fluorescence ? slide_info.filters.size() : static_cast<size_t>(3);

  core::OutputSpec spec;
  spec.dimensions = {width, height};
  spec.channels = static_cast<uint32_t>(n_channels);
  spec.planar_config =
      (n_channels > 3) ? PlanarConfig::kSeparate : PlanarConfig::kContiguous;
  spec.pixel_format = (slide_info.camera_bitdepth >= 16)
                          ? core::OutputSpec::PixelFormat::kUInt16
                          : core::OutputSpec::PixelFormat::kUInt8;
  // Fluorescence channels are independent fluorophores, not RGB color
  // components. Tag the output as Spectral so FFI consumers (e.g. the FV
  // viewer) take the per-channel display path even when N <= 3.
  spec.force_spectral_image = is_fluorescence;

  if (is_fluorescence) {
    // For fluorescence, intensity 0 = no signal = black. The slide's
    // IMAGE_FILL_COLOR_BGR (typically 0x808080 grey) is meaningful only
    // for brightfield H&E backgrounds; using it here would paint missing
    // tiles as bright mid-grey on top of the channel display colors,
    // making sparse regions glow instead of looking empty.
    spec.background = {0, 0, 0, 0};
  } else {
    const auto rgb = mrxs::UnpackRgb24(zoom_level.background_color_rgb);
    spec.background = {rgb[0], rgb[1], rgb[2], 255};
  }

  return spec;
}

}  // namespace fastslide
