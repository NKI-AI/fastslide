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

#include "fastslide/readers/simpletiff_plan_builder_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {
namespace readers {
namespace simpletiff_plan {

Dimensions2D ToDimensions2D(const ImageDimensions& dims) {
  return Dimensions2D{.width = dims[0], .height = dims[1]};
}

Dimensions2D ToDimensions2D(const core::ImageDimensions& dims) {
  return Dimensions2D{.width = dims[0], .height = dims[1]};
}

RegionBounds DetermineRegionBounds(const core::TileRequest& request,
                                   Dimensions2D level_dimensions) {
  RegionBounds out;
  if (request.IsRegionRequest() && request.region_bounds->IsValid()) {
    out.x = request.region_bounds->x;
    out.y = request.region_bounds->y;
    out.width = static_cast<uint32_t>(std::ceil(request.region_bounds->width));
    out.height =
        static_cast<uint32_t>(std::ceil(request.region_bounds->height));
  } else {
    out.x = 0.0;
    out.y = 0.0;
    out.width = level_dimensions.width;
    out.height = level_dimensions.height;
  }
  return out;
}

ClampedRegion ClampRegionToLevel(const RegionBounds& bounds,
                                 Dimensions2D level_dimensions) {
  ClampedRegion out;
  out.x = static_cast<uint32_t>(bounds.x);
  out.y = static_cast<uint32_t>(bounds.y);
  out.width = bounds.width;
  out.height = bounds.height;

  if (bounds.x >= level_dimensions.width ||
      bounds.y >= level_dimensions.height) {
    out.outside = true;
    return out;
  }

  if (bounds.x + bounds.width > level_dimensions.width) {
    out.width = level_dimensions.width - out.x;
  }
  if (bounds.y + bounds.height > level_dimensions.height) {
    out.height = level_dimensions.height - out.y;
  }
  return out;
}

TileGeometry QueryTileGeometry(const simpletiff::TiffIndex& tiff_index,
                               uint32_t page, Dimensions2D level_dimensions) {
  TileGeometry out;
  const auto& page_header = tiff_index.Page(page);
  out.is_tiled = (page_header.storage == simpletiff::Storage::kTiles);
  if (out.is_tiled) {
    const auto& tiles = tiff_index.Tiles(page_header.payload_id);
    out.tile_width = tiles.tile_w;
    out.tile_height = tiles.tile_h;
    return out;
  }

  // Strips: tile_width = image width, tile_height = rows per strip.
  out.tile_width = level_dimensions.width;
  const auto& strips = tiff_index.Strips(page_header.payload_id);
  uint32_t rows_per_strip = strips.rows_per_strip;
  if (rows_per_strip == 0) {
    rows_per_strip = level_dimensions.height;
  }
  out.tile_height = rows_per_strip;
  return out;
}

core::OutputSpec::PixelFormat PixelFormatFromBitsPerSample(
    uint16_t bits_per_sample) {
  return core::ToOutputPixelFormat(DataTypeFromBitsPerSample(bits_per_sample));
}

uint32_t BytesPerSample(uint16_t bits_per_sample) {
  return (static_cast<uint32_t>(bits_per_sample) + 7u) / 8u;
}

uint32_t BytesPerPixel(uint16_t bits_per_sample, uint16_t samples_per_pixel) {
  return BytesPerSample(bits_per_sample) * samples_per_pixel;
}

void ForEachIntersectingTile(
    Dimensions2D level_dimensions, const ClampedRegion& region,
    const TileGeometry& geometry,
    const std::function<void(const IntersectingTile&)>& callback) {
  const uint32_t tile_w = geometry.tile_width;
  const uint32_t tile_h = geometry.tile_height;
  if (tile_w == 0 || tile_h == 0) {
    return;
  }

  const uint32_t first_tile_x = region.x / tile_w;
  const uint32_t first_tile_y = region.y / tile_h;
  const uint32_t last_tile_x = (region.x + region.width - 1) / tile_w;
  const uint32_t last_tile_y = (region.y + region.height - 1) / tile_h;

  const uint32_t tiles_across =
      geometry.is_tiled ? (level_dimensions.width + tile_w - 1) / tile_w : 0;

  for (uint32_t tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
    for (uint32_t tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
      const uint32_t tile_left = tile_x * tile_w;
      const uint32_t tile_top = tile_y * tile_h;
      const uint32_t tile_right =
          std::min(tile_left + tile_w, level_dimensions.width);
      const uint32_t tile_bottom =
          std::min(tile_top + tile_h, level_dimensions.height);

      const uint32_t inter_left = std::max(tile_left, region.x);
      const uint32_t inter_top = std::max(tile_top, region.y);
      const uint32_t inter_right =
          std::min(tile_right, region.x + region.width);
      const uint32_t inter_bottom =
          std::min(tile_bottom, region.y + region.height);

      if (inter_left >= inter_right || inter_top >= inter_bottom) {
        continue;
      }

      IntersectingTile it;
      it.tile_x = tile_x;
      it.tile_y = tile_y;
      it.tile_left = tile_left;
      it.tile_top = tile_top;
      it.inter_left = inter_left;
      it.inter_top = inter_top;
      it.inter_width = inter_right - inter_left;
      it.inter_height = inter_bottom - inter_top;
      it.tile_index =
          geometry.is_tiled
              ? (static_cast<uint64_t>(tile_y) * tiles_across + tile_x)
              : static_cast<uint64_t>(tile_y);
      callback(it);
    }
  }
}

std::vector<core::TileReadOp> BuildTileReadOps(
    const core::TileRequest& request, Dimensions2D level_dimensions,
    const ClampedRegion& region, const TileGeometry& geometry,
    uint32_t bytes_per_pixel, size_t num_iterations,
    const std::function<std::pair<uint32_t, TileCoordinate>(
        size_t, const IntersectingTile&)>& info_callback) {
  std::vector<core::TileReadOp> ops;
  if (region.outside) {
    return ops;
  }

  for (size_t i = 0; i < num_iterations; ++i) {
    ForEachIntersectingTile(
        level_dimensions, region, geometry, [&](const IntersectingTile& it) {
          auto [source_id, tile_coord] = info_callback(i, it);

          core::TileReadOp op;
          op.level = request.level;
          op.tile_coord = tile_coord;
          op.source_id = source_id;
          op.byte_offset = it.tile_index;
          op.byte_size =
              geometry.tile_width * geometry.tile_height * bytes_per_pixel;

          const uint32_t src_x = it.inter_left - it.tile_left;
          const uint32_t src_y = it.inter_top - it.tile_top;
          const uint32_t dst_x = it.inter_left - region.x;
          const uint32_t dst_y = it.inter_top - region.y;

          op.transform.source = {static_cast<double>(src_x),
                                 static_cast<double>(src_y), it.inter_width,
                                 it.inter_height};
          op.transform.dest = {static_cast<double>(dst_x),
                               static_cast<double>(dst_y), it.inter_width,
                               it.inter_height};
          ops.push_back(op);
        });
  }
  return ops;
}

}  // namespace simpletiff_plan
}  // namespace readers
}  // namespace fastslide
