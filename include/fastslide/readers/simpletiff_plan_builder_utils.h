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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_PLAN_BUILDER_UTILS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_PLAN_BUILDER_UTILS_H_

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "simpletiff/index.h"

namespace fastslide {
namespace readers {
namespace simpletiff_plan {

struct Dimensions2D {
  uint32_t width = 0;
  uint32_t height = 0;
};

Dimensions2D ToDimensions2D(const ImageDimensions &dims);

Dimensions2D ToDimensions2D(const core::ImageDimensions &dims);

struct RegionBounds {
  double x = 0.0;
  double y = 0.0;
  uint32_t width = 0;
  uint32_t height = 0;
};

struct ClampedRegion {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool outside = false;
};

struct TileGeometry {
  uint32_t tile_width = 0;
  uint32_t tile_height = 0;
  bool is_tiled = false;
};

// ---- Shared request/geometry helpers ----

RegionBounds DetermineRegionBounds(const core::TileRequest &request,
                                   Dimensions2D level_dimensions);

ClampedRegion ClampRegionToLevel(const RegionBounds &bounds,
                                 Dimensions2D level_dimensions);

TileGeometry QueryTileGeometry(const simpletiff::TiffIndex &tiff_index,
                               uint32_t page, Dimensions2D level_dimensions);

core::OutputSpec::PixelFormat
PixelFormatFromBitsPerSample(uint16_t bits_per_sample);

uint32_t BytesPerSample(uint16_t bits_per_sample);

uint32_t BytesPerPixel(uint16_t bits_per_sample, uint16_t samples_per_pixel);

core::TilePlan::Cost EstimateCosts(std::span<const core::TileReadOp> ops);

// ---- Shared tile intersection iteration ----

struct IntersectingTile {
  uint32_t tile_x = 0;
  uint32_t tile_y = 0;
  uint32_t tile_left = 0;
  uint32_t tile_top = 0;
  uint32_t inter_left = 0;
  uint32_t inter_top = 0;
  uint32_t inter_width = 0;
  uint32_t inter_height = 0;
  uint64_t tile_index = 0; // linear tile index (tiles) or strip index
};

void ForEachIntersectingTile(
    Dimensions2D level_dimensions, const ClampedRegion &region,
    const TileGeometry &geometry,
    const std::function<void(const IntersectingTile &)> &callback);

// Builds TileReadOps for the common "one page per channel" layout.
//
// `page_for_channel(ch)` returns the TIFF page index (IFD index) that stores
// the channel for this level.
std::vector<core::TileReadOp> BuildChannelPageTileReadOps(
    const core::TileRequest &request, Dimensions2D level_dimensions,
    const ClampedRegion &region, const TileGeometry &geometry,
    uint32_t bytes_per_pixel, size_t num_channels,
    const std::function<uint32_t(size_t)> &page_for_channel);

} // namespace simpletiff_plan
} // namespace readers
} // namespace fastslide

#endif // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_PLAN_BUILDER_UTILS_H_
