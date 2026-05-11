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

#include "fastslide/readers/aperio/aperio_plan_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"
#include "fastslide/slide_reader.h"
#include "simpletiff/index.h"

namespace fastslide {

aifocore::Result<core::TilePlan> AperioPlanBuilder::BuildPlan(
    const core::TileRequest& request, const AperioPlanContext& context) {

  core::TilePlan plan;
  plan.request = request;

  // Validate request
  AIFOCORE_RETURN_IF_ERROR(ValidateRequest(request, context));

  // Get level info
  const auto& aperio_level = context.pyramid_levels[request.level];
  LevelInfo level_info;
  level_info.dimensions = {aperio_level.size[0], aperio_level.size[1]};
  level_info.downsample_factor = aperio_level.downsample_factor;

  // Get pyramid level metadata
  const uint16_t page = aperio_level.page;

  // Query TIFF structure into a *local* metadata snapshot. We deliberately do
  // not stash this on the reader: each `TileReadOp` already carries its own
  // page in `source_id`, and the executor re-derives per-page geometry from
  // the read-only `TiffIndex`. Sharing a mutable cross-request snapshot
  // previously caused tile_index/page mismatches when ReadRegion was invoked
  // concurrently for different pyramid levels (see regression test).
  TiffStructureMetadata tiff_metadata;
  AIFOCORE_RETURN_IF_ERROR(
      QueryTiffStructure(context, page, level_info, tiff_metadata));

  // Determine region bounds from request
  double x, y;
  uint32_t width, height;
  DetermineRegionBounds(request, level_info, x, y, width, height);

  // Check if region is completely outside level bounds
  if (x >= level_info.dimensions[0] || y >= level_info.dimensions[1]) {
    // Return empty plan with background fill
    plan.output =
        CreateOutputSpec(width, height, tiff_metadata.samples_per_pixel);
    plan.actual_region = {
        .top_left = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
        .size = {width, height},
        .level = request.level};
    return plan;
  }

  // Clamp width and height to level bounds
  if (x + width > level_info.dimensions[0]) {
    width = level_info.dimensions[0] - static_cast<uint32_t>(x);
  }
  if (y + height > level_info.dimensions[1]) {
    height = level_info.dimensions[1] - static_cast<uint32_t>(y);
  }

  // Create tile operations for intersecting tiles
  auto operations = CreateTileOperations(request, tiff_metadata, level_info, x,
                                         y, width, height);
  plan.operations = std::move(operations);

  // Set output specification
  plan.output =
      CreateOutputSpec(width, height, tiff_metadata.samples_per_pixel);

  // Set actual region
  const uint32_t region_x = static_cast<uint32_t>(x);
  const uint32_t region_y = static_cast<uint32_t>(y);
  plan.actual_region = {.top_left = {region_x, region_y},
                        .size = {width, height},
                        .level = request.level};

  return plan;
}

aifocore::Status AperioPlanBuilder::ValidateRequest(
    const core::TileRequest& request, const AperioPlanContext& context) {

  if (request.level < 0 ||
      request.level >= static_cast<int>(context.pyramid_levels.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }

  return aifocore::Status::OkStatus();
}

void AperioPlanBuilder::DetermineRegionBounds(const core::TileRequest& request,
                                              const LevelInfo& level_info,
                                              double& x, double& y,
                                              uint32_t& width,
                                              uint32_t& height) {
  const auto bounds = readers::simpletiff_plan::DetermineRegionBounds(
      request, readers::simpletiff_plan::ToDimensions2D(level_info.dimensions));
  x = bounds.x;
  y = bounds.y;
  width = bounds.width;
  height = bounds.height;
}

aifocore::Status AperioPlanBuilder::QueryTiffStructure(
    const AperioPlanContext& context, uint16_t page,
    const LevelInfo& level_info, TiffStructureMetadata& tiff_metadata) {
  const auto& tiff_index = context.tiff_index;
  if (page >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Page {} out of range", page));
  }

  const auto& page_header = tiff_index.Page(page);

  // Aperio is typically RGB (3 channels), but query the actual value to handle
  // edge cases (associated images, malformed files, etc.) and validate it to
  // prevent bad_array_new_length downstream.
  const uint16_t samples_per_pixel = page_header.samples_per_pixel;
  if (samples_per_pixel == 0 || samples_per_pixel > 100) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid samples_per_pixel: {} (expected 1-100)",
                              samples_per_pixel));
  }

  if (page_header.storage != simpletiff::Storage::kTiles &&
      page_header.storage != simpletiff::Storage::kStrips) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Unsupported storage type for page");
  }

  // Reuse the shared tile/strip geometry helper instead of re-implementing it.
  const auto geometry = readers::simpletiff_plan::QueryTileGeometry(
      tiff_index, page,
      readers::simpletiff_plan::ToDimensions2D(level_info.dimensions));

  tiff_metadata.page = page;
  tiff_metadata.samples_per_pixel = samples_per_pixel;
  tiff_metadata.is_tiled = geometry.is_tiled;
  tiff_metadata.tile_width = geometry.tile_width;
  tiff_metadata.tile_height = geometry.tile_height;
  return aifocore::Status::OkStatus();
}

std::vector<core::TileReadOp> AperioPlanBuilder::CreateTileOperations(
    const core::TileRequest& request,
    const TiffStructureMetadata& tiff_metadata, const LevelInfo& level_info,
    double x, double y, uint32_t width, uint32_t height) {

  std::vector<core::TileReadOp> operations;

  const uint32_t tile_width = tiff_metadata.tile_width;
  const uint32_t tile_height = tiff_metadata.tile_height;
  const uint16_t samples_per_pixel = tiff_metadata.samples_per_pixel;
  const bool is_tiled = tiff_metadata.is_tiled;
  const uint16_t page = tiff_metadata.page;

  const auto region = readers::simpletiff_plan::ClampRegionToLevel(
      readers::simpletiff_plan::RegionBounds{
          .x = x, .y = y, .width = width, .height = height},
      readers::simpletiff_plan::ToDimensions2D(level_info.dimensions));

  const readers::simpletiff_plan::TileGeometry geom = {
      .tile_width = tile_width,
      .tile_height = tile_height,
      .is_tiled = is_tiled,
  };

  constexpr uint16_t kBitsPerSample = 8;
  const uint32_t bytes_per_pixel = readers::simpletiff_plan::BytesPerPixel(
      kBitsPerSample, samples_per_pixel);

  operations = readers::simpletiff_plan::BuildTileReadOps(
      request, readers::simpletiff_plan::ToDimensions2D(level_info.dimensions),
      region, geom, bytes_per_pixel, 1,
      [&](size_t, const readers::simpletiff_plan::IntersectingTile& it) {
        return std::make_pair(static_cast<uint32_t>(page),
                              TileCoordinate{it.tile_x, it.tile_y});
      });

  return operations;
}

core::OutputSpec AperioPlanBuilder::CreateOutputSpec(
    uint32_t width, uint32_t height, uint16_t samples_per_pixel) {

  core::OutputSpec spec;
  spec.dimensions = {width, height};
  spec.channels = samples_per_pixel;
  spec.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  spec.background = {255, 255, 255, 255};  // White background

  return spec;
}

}  // namespace fastslide
