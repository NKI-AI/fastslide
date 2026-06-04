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

#include "fastslide/runtime/tile_writer.h"

#include <cstdint>
#include <utility>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

namespace fastslide::runtime {

Canvas::Canvas(const core::TilePlan& plan) : config_(AnalyzePlan(plan)) {
  InitOutputImage();
}

Canvas::Canvas(const Config& config) : config_(config) {
  InitOutputImage();
}

Canvas::Canvas(ImageDimensions dimensions, BackgroundColor background,
               bool enable_blending) {
  config_.dimensions = dimensions;
  config_.channels = 3;
  config_.data_type = DataType::kUInt8;
  config_.planar_config = PlanarConfig::kContiguous;
  config_.background = std::move(background);
  config_.enable_blending = enable_blending;
  InitOutputImage();
}

aifocore::Status Canvas::Finalize() {
  finalized_ = true;
  return aifocore::Status::OkStatus();
}

ImageDimensions Canvas::GetDimensions() const {
  return config_.dimensions;
}

uint32_t Canvas::GetChannels() const {
  return config_.channels;
}

aifocore::Result<Image> Canvas::GetOutput() {
  if (!finalized_) {
    AIFOCORE_RETURN_IF_ERROR(Finalize());
  }
  if (!output_image_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No output image available");
  }
  return std::move(*output_image_);
}

bool Canvas::IsBlendingEnabled() const {
  return config_.enable_blending;
}

}  // namespace fastslide::runtime
