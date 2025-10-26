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

#include "fastslide/runtime/tile_writer/direct/direct_strategy.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/runtime/tile_writer/direct/copy_planar.h"
#include "fastslide/runtime/tile_writer/direct/copy_rgb8.h"
#include "fastslide/runtime/tile_writer/direct/fills.h"
#include "fastslide/runtime/tile_writer/tile_writer_strategy.h"

namespace fastslide::runtime {

DirectStrategy::DirectStrategy(const TileWriter::Config& config)
    : config_(config) {
  if (config_.dimensions[0] == 0 || config_.dimensions[1] == 0 ||
      config_.channels == 0 || config_.background.values.empty()) {
    throw std::invalid_argument("Invalid TileWriter configuration");
  }

  // Create appropriate Image based on channels
  if (config_.channels == 3) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kRGB,
                                config_.data_type, config_.planar_config);
  } else if (config_.channels == 4) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kRGBA,
                                config_.data_type, config_.planar_config);
  } else if (config_.channels == 1) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kGray,
                                config_.data_type, config_.planar_config);
  } else {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, config_.channels,
                                config_.data_type, config_.planar_config);
  }

  ZeroInit(output_image_->GetData(), output_image_->SizeBytes());
}

absl::Status DirectStrategy::WriteTile(const core::TileReadOp& op,
                                       std::span<const uint8_t> pixel_data,
                                       uint32_t tile_width,
                                       uint32_t tile_height,
                                       uint32_t tile_channels) {
  if (!output_image_) {
    return absl::InternalError("DirectStrategy has null image pointer");
  }

  const auto& dst = op.transform.dest;

  if (dst.x + dst.width > config_.dimensions[0] ||
      dst.y + dst.height > config_.dimensions[1]) {
    return absl::OutOfRangeError(absl::StrFormat(
        "Destination region [%d,%d,%d,%d] exceeds image bounds [%d,%d]", dst.x,
        dst.y, dst.width, dst.height, config_.dimensions[0],
        config_.dimensions[1]));
  }

  if (config_.planar_config == PlanarConfig::kSeparate) {
    return WriteTilePlanar(op, pixel_data, tile_width, tile_height,
                           tile_channels);
  } else {
    return WriteTileInterleaved(op, pixel_data, tile_width, tile_height,
                                tile_channels);
  }
}

absl::Status DirectStrategy::Finalize() {
  finalized_ = true;
  return absl::OkStatus();
}

ImageDimensions DirectStrategy::GetDimensions() const {
  return config_.dimensions;
}

uint32_t DirectStrategy::GetChannels() const {
  return config_.channels;
}

absl::StatusOr<Image> DirectStrategy::GetOutput() {
  if (!output_image_)
    return absl::InternalError("No output image available");
  return std::move(*output_image_);
}

std::string DirectStrategy::GetName() const {
  return "NativeDirect";
}

uint8_t* DirectStrategy::GetOutputBuffer() {
  return output_image_ ? output_image_->GetData() : nullptr;
}

size_t DirectStrategy::GetOutputBufferSize() const {
  return output_image_ ? output_image_->SizeBytes() : 0;
}

absl::Status DirectStrategy::WriteTilePlanar(
    const core::TileReadOp& op, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, uint32_t tile_channels) {
  if (!output_image_) {
    return absl::InternalError("DirectStrategy has null image pointer");
  }

  const auto& src = op.transform.source;
  const auto& dst = op.transform.dest;
  uint8_t* image_data = output_image_->GetData();
  const uint32_t bytes_per_sample = output_image_->GetBytesPerSample();
  const uint32_t target_channel = op.tile_coord.x;

  if (target_channel >= config_.channels) {
    return absl::OutOfRangeError(
        absl::StrFormat("Target channel %d exceeds image channel count %d",
                        target_channel, config_.channels));
  }

  CopyTilePlanar(pixel_data.data(), tile_width, tile_height, src.x, src.y,
                 image_data, config_.dimensions[0], config_.dimensions[1],
                 dst.x, dst.y, dst.width, dst.height, target_channel,
                 config_.channels, bytes_per_sample);

  return absl::OkStatus();
}

absl::Status DirectStrategy::WriteTileInterleaved(
    const core::TileReadOp& op, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, uint32_t tile_channels) {
  if (!output_image_) {
    return absl::InternalError("DirectStrategy has null image pointer");
  }

  const auto& src = op.transform.source;
  const auto& dst = op.transform.dest;
  uint8_t* image_data = output_image_->GetData();
  const uint32_t bytes_per_sample = output_image_->GetBytesPerSample();

  // HOT PATH: RGB8, channels match, uint8, contiguous copy
  const bool fast_path = (config_.channels == 3) && (tile_channels == 3) &&
                         (bytes_per_sample == 1) && (src.x == 0) &&
                         (src.y == 0) && (dst.width == tile_width) &&
                         (dst.height == tile_height);

  if (fast_path) {
    CopyTileRectRGB8(pixel_data.data(), tile_width, tile_height, 0, 0,
                     image_data, config_.dimensions[0], config_.dimensions[1],
                     dst.x, dst.y, tile_width, tile_height);
    return absl::OkStatus();
  }

  // SLOW PATH: Generic interleaved copy
  CopyRectGeneral(pixel_data.data(), tile_width, tile_height, tile_channels,
                  src.x, src.y, image_data, config_.dimensions[0],
                  config_.dimensions[1], config_.channels, dst.x, dst.y,
                  dst.width, dst.height, bytes_per_sample);

  return absl::OkStatus();
}

}  // namespace fastslide::runtime
