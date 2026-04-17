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

#include "fastslide/readers/ndpitiff/ndpitiff_plan_builder.h"

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/ndpitiff/ndpitiff.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> NdpiTiffPlanBuilder::BuildPlan(
    const core::TileRequest& request, const NdpiTiffReader& reader) {
  core::TilePlan plan;
  plan.request = request;

  const auto& pyramid = reader.GetPyramidLevels();
  if (request.level < 0 ||
      static_cast<size_t>(request.level) >= pyramid.size()) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }
  if (pyramid.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "NDPI has no pyramid levels");
  }

  const auto& level_info = pyramid[request.level];
  const uint16_t page = level_info.page;
  const auto& tiff_index = reader.GetTiffIndex();
  if (page >= tiff_index.NumPages()) {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Page {} out of range", page));
  }

  const auto& page_header = tiff_index.Page(page);
  const uint16_t bits_per_sample = page_header.bits_per_sample;
  const uint16_t samples_per_pixel = page_header.samples_per_pixel;

  plan.output.pixel_format =
      readers::simpletiff_plan::PixelFormatFromBitsPerSample(bits_per_sample);

  const auto bounds = readers::simpletiff_plan::DetermineRegionBounds(
      request, readers::simpletiff_plan::ToDimensions2D(level_info.size));
  const auto region = readers::simpletiff_plan::ClampRegionToLevel(
      bounds, readers::simpletiff_plan::ToDimensions2D(level_info.size));

  plan.output.dimensions = {region.width, region.height};
  plan.output.channels = samples_per_pixel;
  plan.output.planar_config = PlanarConfig::kContiguous;
  plan.output.background = {255, 255, 255, 255};
  plan.actual_region = {.top_left = {region.x, region.y},
                        .size = {region.width, region.height},
                        .level = request.level};

  if (region.outside) {
    return plan;
  }

  const auto geom = readers::simpletiff_plan::QueryTileGeometry(
      tiff_index, page,
      readers::simpletiff_plan::ToDimensions2D(level_info.size));
  const uint32_t bytes_per_pixel = readers::simpletiff_plan::BytesPerPixel(
      bits_per_sample, samples_per_pixel);

  plan.operations = readers::simpletiff_plan::BuildTileReadOps(
      request, readers::simpletiff_plan::ToDimensions2D(level_info.size),
      region, geom, bytes_per_pixel, 1,
      [&](size_t, const readers::simpletiff_plan::IntersectingTile& it) {
        return std::make_pair(static_cast<uint32_t>(page),
                              TileCoordinate{it.tile_x, it.tile_y});
      });

  return plan;
}

}  // namespace fastslide
