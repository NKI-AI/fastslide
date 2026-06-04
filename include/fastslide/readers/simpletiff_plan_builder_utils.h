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
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
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

Dimensions2D ToDimensions2D(const ImageDimensions& dims);

Dimensions2D ToDimensions2D(const core::ImageDimensions& dims);

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

RegionBounds DetermineRegionBounds(const core::TileRequest& request,
                                   Dimensions2D level_dimensions);

ClampedRegion ClampRegionToLevel(const RegionBounds& bounds,
                                 Dimensions2D level_dimensions);

TileGeometry QueryTileGeometry(const simpletiff::TiffIndex& tiff_index,
                               uint32_t page, Dimensions2D level_dimensions);

core::OutputSpec::PixelFormat PixelFormatFromBitsPerSample(
    uint16_t bits_per_sample);

uint32_t BytesPerSample(uint16_t bits_per_sample);

uint32_t BytesPerPixel(uint16_t bits_per_sample, uint16_t samples_per_pixel);

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
  uint64_t tile_index = 0;  // linear tile index (tiles) or strip index
};

void ForEachIntersectingTile(
    Dimensions2D level_dimensions, const ClampedRegion& region,
    const TileGeometry& geometry,
    const std::function<void(const IntersectingTile&)>& callback);

// Builds TileReadOps for generic iterated layouts (e.g. channels, or just
// generic spatial tiles).
//
// `info_callback(iteration, tile)` returns {source_id, tile_coord} for the
// ReadOp.
std::vector<core::TileReadOp> BuildTileReadOps(
    const core::TileRequest& request, Dimensions2D level_dimensions,
    const ClampedRegion& region, const TileGeometry& geometry,
    uint32_t bytes_per_pixel, size_t num_iterations,
    const std::function<std::pair<uint32_t, TileCoordinate>(
        size_t, const IntersectingTile&)>& info_callback);

// ---- Shared single-page plan builder ----

/// @brief Build a TilePlan for a single-page, single-channel TIFF pyramid level
///        (Generic / NDPI / Philips style).
///
/// Each level corresponds to one TIFF page with packed RGB(A) samples. The
/// caller passes the pyramid as a span of structs that expose `page` (the TIFF
/// page index for the level) and `size` (an `ImageDimensions` describing the
/// level's pixel dimensions).
///
/// @tparam PyramidLevel Struct with `.page` and `.size` members.
/// @param request       Caller's tile request (level-native coordinates).
/// @param pyramid       Pyramid level descriptors.
/// @param tiff_index    Underlying TIFF index for tile/strip geometry queries.
/// @param format_name   Display name used in error messages (e.g. "NDPI").
/// @return A populated `core::TilePlan` or an error status.
template <typename PyramidLevel>
aifocore::Result<core::TilePlan> BuildSinglePagePlan(
    const core::TileRequest& request, std::span<const PyramidLevel> pyramid,
    const simpletiff::TiffIndex& tiff_index, std::string_view format_name) {
  core::TilePlan plan;
  plan.request = request;

  if (pyramid.empty()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("{} has no pyramid levels", format_name));
  }
  if (request.level < 0 ||
      static_cast<size_t>(request.level) >= pyramid.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }

  const auto& level_info = pyramid[request.level];
  const uint16_t page = level_info.page;
  if (page >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Page {} out of range", page));
  }

  const auto& page_header = tiff_index.Page(page);
  const uint16_t bits_per_sample = page_header.bits_per_sample;
  const uint16_t samples_per_pixel = page_header.samples_per_pixel;
  const auto level_dims = ToDimensions2D(level_info.size);

  plan.output.pixel_format = PixelFormatFromBitsPerSample(bits_per_sample);

  const auto bounds = DetermineRegionBounds(request, level_dims);
  const auto region = ClampRegionToLevel(bounds, level_dims);

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

  const auto geom = QueryTileGeometry(tiff_index, page, level_dims);
  const uint32_t bytes_per_pixel =
      BytesPerPixel(bits_per_sample, samples_per_pixel);

  plan.operations = BuildTileReadOps(
      request, level_dims, region, geom, bytes_per_pixel, /*num_iterations=*/1,
      [page](size_t /*iter*/, const IntersectingTile& it) {
        return std::make_pair(static_cast<uint32_t>(page),
                              TileCoordinate{it.tile_x, it.tile_y});
      });

  return plan;
}

// ---- Shared multi-channel plan builder ----

/// @brief Build a TilePlan for a multi-channel TIFF pyramid level (QPTIFF /
///        OME-TIFF style).
///
/// Each pyramid level holds one TIFF page per channel. The caller passes the
/// pyramid as a span of structs that expose `.pages` (a vector of integral
/// per-channel TIFF page indices) and `.size` (level pixel dimensions).
///
/// `output_planar_config` selects between an interleaved RGB output (uses
/// `samples_per_pixel` from the first channel page) and a separated, planar
/// per-channel output (uses one channel per page).
///
/// @tparam LevelInfo  Struct with `.pages` (range of integral page indices) and
///                    `.size` members.
/// @param request               Caller's tile request (level-native coords).
/// @param pyramid               Pyramid level descriptors.
/// @param output_planar_config  Output layout (interleaved vs separated).
/// @param tiff_index            Underlying TIFF index for tile geometry.
/// @param max_channels          Safety upper bound on the channel count.
/// @return A populated `core::TilePlan` or an error status.
template <typename LevelInfo>
aifocore::Result<core::TilePlan> BuildMultiChannelPlan(
    const core::TileRequest& request, std::span<const LevelInfo> pyramid,
    PlanarConfig output_planar_config, const simpletiff::TiffIndex& tiff_index,
    size_t max_channels = 1000) {
  core::TilePlan plan;
  plan.request = request;

  if (request.level < 0 ||
      static_cast<size_t>(request.level) >= pyramid.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", request.level));
  }

  const auto& level_info = pyramid[request.level];
  const size_t num_channels = level_info.pages.size();

  if (num_channels == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Level {} has no pages/channels", request.level));
  }
  if (num_channels > max_channels) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Level {} has too many channels: {} (max {})",
                              request.level, num_channels, max_channels));
  }

  const auto& page_header = tiff_index.Page(level_info.pages[0]);
  const size_t output_channels =
      (output_planar_config == PlanarConfig::kContiguous)
          ? page_header.samples_per_pixel
          : num_channels;

  const uint16_t bits_per_sample = page_header.bits_per_sample;
  const auto level_dims = ToDimensions2D(level_info.size);

  plan.output.pixel_format = PixelFormatFromBitsPerSample(bits_per_sample);

  const auto bounds = DetermineRegionBounds(request, level_dims);
  const auto region = ClampRegionToLevel(bounds, level_dims);

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

  const auto geom =
      QueryTileGeometry(tiff_index, level_info.pages[0], level_dims);
  const uint32_t bytes_per_pixel =
      BytesPerPixel(bits_per_sample, page_header.samples_per_pixel);

  plan.operations = BuildTileReadOps(
      request, level_dims, region, geom, bytes_per_pixel, num_channels,
      [&level_info](size_t ch, const IntersectingTile&) {
        return std::make_pair(static_cast<uint32_t>(level_info.pages[ch]),
                              TileCoordinate{static_cast<uint32_t>(ch), 0});
      });

  return plan;
}

}  // namespace simpletiff_plan
}  // namespace readers
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_PLAN_BUILDER_UTILS_H_
