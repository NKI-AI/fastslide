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
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/self_image_view.h"
#include "fastslide/utilities/color_transform.h"

namespace fastslide {

SlideReader::SlideReader() = default;
SlideReader::~SlideReader() = default;

aifocore::Status SlideReader::SetColorTransform(ColorSpace target,
                                                RenderingIntent intent) {
  auto profile_or = GetIccProfile();
  if (!profile_or.ok()) {
    // Leave color management disabled and keep returning native pixels. Two
    // outcomes are non-fatal here:
    //   - kNotFound: the format supports ICC extraction but this slide has no
    //     embedded profile. Expected and silent.
    //   - kUnimplemented: the format has no ICC support at all. The caller
    //     explicitly asked for color management, so warn that it was skipped.
    color_transform_.reset();
    if (profile_or.status().code() == aifocore::StatusCode::kUnimplemented) {
      std::cerr << "[FastSlide] ICC color management was requested but is not "
                   "implemented for the '"
                << GetFormatName()
                << "' format; returning native (unmanaged) pixels.\n";
    }
    return aifocore::Status::OkStatus();
  }

  const std::vector<uint8_t>& profile = profile_or.value();
  std::unique_ptr<IccTransform> transform;
  AIFOCORE_ASSIGN_OR_RETURN(transform,
                            IccTransform::Create(profile, target, intent));
  color_transform_ = std::move(transform);

  // Propagate to every navigable image so per-image `ReadRegion` (used by the
  // multi-image API and the Python per-image reads) applies the same
  // transform. Images that fail to resolve are skipped.
  std::shared_ptr<const IccTransform> shared = color_transform_;
  const int image_count = GetImageCount();
  for (int i = 0; i < image_count; ++i) {
    auto image_or = GetImage(i);
    if (image_or.ok() && image_or.value() != nullptr) {
      image_or.value()->SetColorTransform(shared);
    }
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status SlideReader::MaybeApplyColorTransform(Image& image) const {
  if (color_transform_ == nullptr) {
    return aifocore::Status::OkStatus();
  }
  return color_transform_->ApplyInPlace(image);
}

std::vector<std::string> SlideReader::GetImageNames() const {
  std::vector<std::string> names;
  const int count = GetImageCount();
  names.reserve(static_cast<size_t>(std::max(count, 0)));
  for (int i = 0; i < count; ++i) {
    names.emplace_back("image " + std::to_string(i));
  }
  return names;
}

aifocore::Result<const SlideImage*> SlideReader::GetImage(int index) const {
  if (index < 0 || index >= GetImageCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Image index {} out of range [0, {})", index,
                              GetImageCount()));
  }
  // Default container behaviour: lazily create a `SelfImageView` that
  // forwards back to this reader. Multi-image readers override `GetImage`
  // entirely and never reach this code path.
  std::call_once(self_image_view_once_, [this]() {
    self_image_view_ = std::make_unique<SelfImageView>(*this);
  });
  return self_image_view_.get();
}

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

StackInfo SlideReader::GetStackInfo() const {
  // No Z/T stack by default. Readers that expose focal/time planes override
  // this (e.g. OmeTiffReader from its parsed metadata, CziReader by forwarding
  // to the primary scene image). The default single-image view
  // (SelfImageView::GetStackInfo) forwards here, so this must not delegate back
  // to the primary image or it would recurse for plain readers.
  return {};
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

aifocore::Result<core::TileRequest> SlideReader::RegionToTileRequest(
    const RegionSpec& region) const {
  // Validate region
  if (!region.IsValid()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid region specification");
  }

  // Get level info to validate level exists
  LevelInfo level_info;
  AIFOCORE_ASSIGN_OR_RETURN(level_info, GetLevelInfo(region.level));

  // Check for complete out-of-bounds
  if (region.top_left[0] >= level_info.dimensions[0] ||
      region.top_left[1] >= level_info.dimensions[1]) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format(
            "Requested region at ({}, {}) is completely outside image "
            "bounds ({}, {}) at level {}",
            region.top_left[0], region.top_left[1], level_info.dimensions[0],
            level_info.dimensions[1], region.level));
  }

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

  // Forward the focal/time plane selector to the format's PrepareRequest.
  request.plane = region.plane;

  return request;
}

aifocore::Result<Image> SlideReader::ReadRegion(
    const RegionSpec& region) const {
  core::TileRequest request;
  AIFOCORE_ASSIGN_OR_RETURN(request, RegionToTileRequest(region));

  core::TilePlan plan;
  AIFOCORE_ASSIGN_OR_RETURN(plan, PrepareRequest(request));

  if (plan.output.dimensions[0] == 0 || plan.output.dimensions[1] == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("PrepareRequest returned invalid plan with zero "
                              "dimensions: [{},{}]",
                              plan.output.dimensions[0],
                              plan.output.dimensions[1]));
  }
  if (plan.output.channels == 0 || plan.output.channels > 10000) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format(
            "PrepareRequest returned invalid plan with bad channel count: {}",
            plan.output.channels));
  }

  runtime::Canvas canvas(plan);

  AIFOCORE_RETURN_IF_ERROR(ExecutePlan(plan, canvas));
  AIFOCORE_RETURN_IF_ERROR(canvas.Finalize());

  Image output;
  AIFOCORE_ASSIGN_OR_RETURN(output, canvas.GetOutput());

  AIFOCORE_RETURN_IF_ERROR(MaybeApplyColorTransform(output));

  return output;
}

}  // namespace fastslide
