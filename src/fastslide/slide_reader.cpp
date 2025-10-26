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

#include "fastslide/slide_reader.h"

#include <algorithm>
#include <cstring>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

RegionSpec SlideReader::ClampRegion(const RegionSpec& region,
                                    const ImageDimensions& image_dims) {
  // Handle edge case of zero-sized image
  if (image_dims[0] == 0 || image_dims[1] == 0) {
    RegionSpec clamped = region;
    clamped.top_left[0] = 0;
    clamped.top_left[1] = 0;
    clamped.size[0] = 0;
    clamped.size[1] = 0;
    return clamped;
  }

  RegionSpec clamped = region;

  // Clamp coordinates to image bounds using std::clamp for safety
  clamped.top_left[0] =
      std::clamp(clamped.top_left[0], uint32_t{0}, image_dims[0]);
  clamped.top_left[1] =
      std::clamp(clamped.top_left[1], uint32_t{0}, image_dims[1]);

  // Calculate remaining image area with overflow protection
  const uint32_t remaining_width = image_dims[0] - clamped.top_left[0];
  const uint32_t remaining_height = image_dims[1] - clamped.top_left[1];

  // Clamp size to remaining image area
  clamped.size[0] = std::min(clamped.size[0], remaining_width);
  clamped.size[1] = std::min(clamped.size[1], remaining_height);

  return clamped;
}

int SlideReader::GetBestLevelForDownsample(double downsample) const {
  if (downsample <= 1.0) {
    return 0;
  }

  const int level_count = GetLevelCount();
  if (level_count == 0) {
    return 0;
  }

  int best_level = 0;
  double best_diff = std::abs(1.0 - downsample);

  for (int level = 0; level < level_count; ++level) {
    auto level_info_result = GetLevelInfo(level);
    if (!level_info_result.ok()) {
      continue;  // Skip invalid levels
    }

    double level_downsample = level_info_result.value().downsample_factor;
    double diff = std::abs(level_downsample - downsample);
    if (diff < best_diff) {
      best_diff = diff;
      best_level = level;
    }
  }

  return best_level;
}

// ============================================================================
// Two-Stage Pipeline Helpers
// ============================================================================

absl::StatusOr<core::TileRequest> SlideReader::RegionToTileRequest(
    const RegionSpec& region) const {
  // Validate region
  if (!region.IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Invalid region specification");
  }

  // Get level info to validate level exists
  DECLARE_ASSIGN_OR_RETURN_STATUSOR([[maybe_unused]] LevelInfo, level_info,
                                    GetLevelInfo(region.level));

  // Create tile request for the region
  // The PrepareRequest implementation will use region_bounds to determine
  // the actual tiles needed and their transforms.
  core::TileRequest request;
  request.level = region.level;

  // Populate fractional region bounds for formats that need precise positioning
  // Convert uint32 coordinates to double for fractional precision support
  core::FractionalRegionBounds bounds;
  bounds.x = static_cast<double>(region.top_left[0]);
  bounds.y = static_cast<double>(region.top_left[1]);
  bounds.width = static_cast<double>(region.size[0]);
  bounds.height = static_cast<double>(region.size[1]);
  request.region_bounds = bounds;

  // Tile coordinates are not meaningful for region requests (set to 0,0)
  // PrepareRequest implementations should use region_bounds instead
  request.tile_coord = {0, 0};

  // Include visible channel indices if set
  request.channel_indices = visible_channels_;

  return request;
}

absl::StatusOr<Image> SlideReader::ReadRegionViaPipeline(
    const RegionSpec& region) const {

  // Convert RegionSpec to TileRequest
  core::TileRequest request;
  ASSIGN_OR_RETURN(request, RegionToTileRequest(region));

  // Call PrepareRequest to get execution plan
  DECLARE_ASSIGN_OR_RETURN_STATUSOR(core::TilePlan, plan,
                                    PrepareRequest(request));

  // Validate plan before creating TileWriter to prevent bad_array_new_length
  if (plan.output.dimensions[0] == 0 || plan.output.dimensions[1] == 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        absl::StrFormat("PrepareRequest returned invalid plan with zero "
                        "dimensions: [%u,%u]",
                        plan.output.dimensions[0], plan.output.dimensions[1]));
  }
  if (plan.output.channels == 0 || plan.output.channels > 10000) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        absl::StrFormat(
            "PrepareRequest returned invalid plan with bad channel count: %u",
            plan.output.channels));
  }

  runtime::TileWriter writer(plan);  // Auto-detects blending, channels, etc.

  // Execute the plan (same interface for all formats)
  RETURN_IF_ERROR(ExecutePlan(plan, writer), "Failed to execute plan");
  RETURN_IF_ERROR(writer.Finalize(), "Failed to finalize writer");

  Image output;
  ASSIGN_OR_RETURN(output, writer.GetOutput());

  // Return the Image directly (unified interface always returns Image)
  return output;
}

}  // namespace fastslide
