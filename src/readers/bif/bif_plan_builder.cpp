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

#include "fastslide/readers/bif/bif_plan_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"

namespace fastslide {

namespace {

void DetermineRegionBounds(const core::TileRequest& request,
                           const ImageDimensions& level_dims, double& x,
                           double& y, uint32_t& width, uint32_t& height) {
  if (request.IsRegionRequest() && request.region_bounds->IsValid()) {
    x = request.region_bounds->x;
    y = request.region_bounds->y;
    width = static_cast<uint32_t>(std::ceil(request.region_bounds->width));
    height = static_cast<uint32_t>(std::ceil(request.region_bounds->height));
  } else {
    x = 0.0;
    y = 0.0;
    width = level_dims[0];
    height = level_dims[1];
  }
}

}  // namespace

aifocore::Result<core::TilePlan> BifPlanBuilder::BuildPlan(
    const core::TileRequest& request, const BifPlanContext& context) {
  if (context.spatial_index == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "BIF plan: null spatial index");
  }

  core::TilePlan plan;
  plan.request = request;

  double region_x = 0.0;
  double region_y = 0.0;
  uint32_t width = 0;
  uint32_t height = 0;
  DetermineRegionBounds(request, context.level_dims, region_x, region_y, width,
                        height);

  const auto white =
      static_cast<uint8_t>(std::clamp(context.scan_white_point, 0, 255));

  plan.output.dimensions = {width, height};
  plan.output.channels = context.channels;
  plan.output.planar_config = PlanarConfig::kContiguous;
  plan.output.pixel_format = core::OutputSpec::PixelFormat::kUInt8;
  plan.output.background = {white, white, white, 255};
  plan.actual_region = {
      .top_left = {static_cast<uint32_t>(std::lround(region_x)),
                   static_cast<uint32_t>(std::lround(region_y))},
      .size = {width, height},
      .level = request.level};

  if (width == 0 || height == 0) {
    return plan;
  }

  const auto hits =
      context.spatial_index->QueryRegion(region_x, region_y, width, height);
  const auto& tiles = context.spatial_index->tiles();

  plan.operations.reserve(hits.size());
  for (size_t idx : hits) {
    const auto& st = tiles[idx];

    // Sub-pixel destination: keep the fractional offset so the Canvas resamples
    // each tile onto the integer output grid (bilinear path). Because every
    // tile is resampled to the *same* output grid, overlapping tiles agree in
    // the overlap and meet seamlessly - provided the resampler clamps at the
    // tile's own sub-rectangle border instead of reading the neighbouring
    // packed tile (handled in Canvas::BilinearRgbBlit). This keeps pixels
    // aligned to the grid.
    const double dest_x = st.dest_x - region_x;
    const double dest_y = st.dest_y - region_y;
    if (dest_x + st.dest_w <= 0.0 || dest_y + st.dest_h <= 0.0 ||
        dest_x >= width || dest_y >= height) {
      continue;
    }

    core::TileReadOp op;
    op.level = request.level;
    // tile_coord doubles as the cache key payload (see BifTileExecutor):
    // x = TIFF tile index, y = source IFD page.
    op.tile_coord = {st.tiff_tile_index, st.source_page};
    op.source_id = st.source_page;
    op.byte_offset = st.tiff_tile_index;
    op.byte_size = 0;
    op.transform.source = {static_cast<double>(st.src_x),
                           static_cast<double>(st.src_y), st.src_w, st.src_h};
    op.transform.dest = {dest_x, dest_y, st.dest_w, st.dest_h};

    // Carry blend metadata so the Canvas takes the RGB8 copy path with
    // first-writer-wins coverage (overlap resolution per bif.rst).
    core::BlendMetadata blend;
    blend.weight = 1.0;
    blend.gain = 1.0F;
    blend.mode = core::BlendMode::kOverwrite;
    op.blend_metadata = blend;

    plan.operations.push_back(op);
  }

  return plan;
}

}  // namespace fastslide
