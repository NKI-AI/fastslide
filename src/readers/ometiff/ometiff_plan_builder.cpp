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

#include "fastslide/readers/ometiff/ometiff_plan_builder.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> OmetiffPlanBuilder::BuildPlan(
    const core::TileRequest& request,
    const std::vector<OmeTiffLevelInfo>& pyramid,
    PlanarConfig output_planar_config,
    const simpletiff::TiffIndex& tiff_index) {
  core::TilePlan plan;
  plan.request = request;

  AIFOCORE_RETURN_IF_ERROR(ValidateRequest(request, pyramid));

  const OmeTiffLevelInfo& level_info = pyramid[request.level];
  const size_t num_channels = level_info.pages.size();

  const auto& page_header = tiff_index.Page(level_info.pages[0]);

  const size_t output_channels =
      (output_planar_config == PlanarConfig::kContiguous)
          ? page_header.samples_per_pixel
          : num_channels;

  const uint16_t bits_per_sample = page_header.bits_per_sample;
  plan.output.pixel_format =
      readers::simpletiff_plan::PixelFormatFromBitsPerSample(bits_per_sample);

  const auto bounds = readers::simpletiff_plan::DetermineRegionBounds(
      request, readers::simpletiff_plan::ToDimensions2D(level_info.size));
  const auto region = readers::simpletiff_plan::ClampRegionToLevel(
      bounds, readers::simpletiff_plan::ToDimensions2D(level_info.size));

  plan.output.dimensions = {region.width, region.height};
  plan.output.channels = static_cast<uint32_t>(output_channels);
  plan.output.planar_config = output_planar_config;
  plan.output.background = {0, 0, 0, 255};
  plan.actual_region = {.top_left = {region.x, region.y},
                        .size = {region.width, region.height},
                        .level = request.level};

  if (region.outside) {
    return plan;
  }

  const auto geom = readers::simpletiff_plan::QueryTileGeometry(
      tiff_index, level_info.pages[0],
      readers::simpletiff_plan::ToDimensions2D(level_info.size));
  const uint32_t bytes_per_pixel = readers::simpletiff_plan::BytesPerPixel(
      bits_per_sample, page_header.samples_per_pixel);

  plan.operations = readers::simpletiff_plan::BuildTileReadOps(
      request, readers::simpletiff_plan::ToDimensions2D(level_info.size),
      region, geom, bytes_per_pixel, num_channels,
      [&](size_t ch, const readers::simpletiff_plan::IntersectingTile&) {
        return std::make_pair(level_info.pages[ch],
                              TileCoordinate{static_cast<uint32_t>(ch), 0});
      });

  return plan;
}

aifocore::Status OmetiffPlanBuilder::ValidateRequest(
    const core::TileRequest& request,
    const std::vector<OmeTiffLevelInfo>& pyramid) {
  if (request.level < 0 ||
      static_cast<size_t>(request.level) >= pyramid.size()) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }
  const OmeTiffLevelInfo& level_info = pyramid[request.level];
  if (level_info.pages.empty()) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Level {} has no pages/channels", request.level));
  }
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide
