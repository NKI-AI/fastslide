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

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/runtime/tile_writer/blended/blended_strategy.h"
#include "fastslide/runtime/tile_writer/direct/direct_strategy.h"
#include "fastslide/runtime/tile_writer/direct/fills.h"
#include "fastslide/runtime/tile_writer/tile_writer_strategy.h"

namespace fastslide::runtime {

TileWriter::~TileWriter() = default;

TileWriter::TileWriter(const core::TilePlan& plan)
    : config_(AnalyzePlan(plan)) {
  strategy_ = CreateStrategy(config_);
}

TileWriter::TileWriter(const Config& config) : config_(config) {
  strategy_ = CreateStrategy(config_);
}

TileWriter::TileWriter(uint32_t width, uint32_t height,
                       BackgroundColor background, bool enable_blending) {
  config_.dimensions = {width, height};
  config_.channels = 3;
  config_.data_type = DataType::kUInt8;
  config_.planar_config = PlanarConfig::kContiguous;
  config_.background = std::move(background);
  config_.enable_blending = enable_blending;
  strategy_ = CreateStrategy(config_);
}

absl::Status TileWriter::WriteTile(const core::TileReadOp& op,
                                   std::span<const uint8_t> pixel_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   uint32_t tile_channels) {
  return strategy_->WriteTile(op, pixel_data, tile_width, tile_height,
                              tile_channels);
}

absl::Status TileWriter::WriteTile(const core::TileReadOp& op,
                                   std::span<const uint8_t> pixel_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   uint32_t tile_channels,
                                   absl::Mutex& accumulator_mutex) {
  // Try to cast to BlendedStrategy to use mutex-aware version
  auto* blended = dynamic_cast<BlendedStrategy*>(strategy_.get());
  if (blended) {
    return blended->WriteTile(op, pixel_data, tile_width, tile_height,
                              tile_channels, accumulator_mutex);
  }
  // Fall back to regular WriteTile for non-blended strategies
  return strategy_->WriteTile(op, pixel_data, tile_width, tile_height,
                              tile_channels);
}

absl::Status TileWriter::FillWithColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* buffer = strategy_->GetOutputBuffer();
  if (!buffer) {
    return absl::InternalError("No output buffer available");
  }

  if (config_.channels == 3) {
    FillRGB8(buffer, config_.dimensions[0], config_.dimensions[1], r, g, b);
  } else if (config_.channels == 1) {
    const uint8_t gray = static_cast<uint8_t>((r + g + b) / 3);
    FillGray8(buffer, config_.dimensions[0], config_.dimensions[1], gray);
  } else if (config_.channels == 4) {
    FillRGBA8(buffer, config_.dimensions[0], config_.dimensions[1], r, g, b,
              255);
  } else {
    return absl::UnimplementedError(absl::StrFormat(
        "FillWithColor not implemented for %d channels", config_.channels));
  }

  return absl::OkStatus();
}

absl::Status TileWriter::Finalize() {
  return strategy_->Finalize();
}

ImageDimensions TileWriter::GetDimensions() const {
  return strategy_->GetDimensions();
}

uint32_t TileWriter::GetChannels() const {
  return strategy_->GetChannels();
}

absl::StatusOr<TileWriter::OutputImage> TileWriter::GetOutput() {
  return strategy_->GetOutput();
}

bool TileWriter::IsBlendingEnabled() const {
  return config_.enable_blending;
}

std::string TileWriter::GetStrategyName() const {
  return strategy_->GetName();
}

std::unique_ptr<ITileWriterStrategy> TileWriter::CreateStrategy(
    const Config& config) {
  if (config.enable_blending) {
    return std::make_unique<BlendedStrategy>(config);
  } else {
    return std::make_unique<DirectStrategy>(config);
  }
}

TileWriter::Config TileWriter::AnalyzePlan(const core::TilePlan& plan) {
  Config config;

  config.dimensions = plan.output.dimensions;

  if (config.dimensions[0] == 0 || config.dimensions[1] == 0 ||
      config.dimensions[0] > 100000 || config.dimensions[1] > 100000) {
    config.dimensions = {1, 1};
  }

  config.channels = plan.output.channels;
  if (config.channels == 0 || config.channels > 1000) {
    config.channels = 3;
  }

  switch (plan.output.pixel_format) {
    case core::OutputSpec::PixelFormat::kUInt8:
      config.data_type = DataType::kUInt8;
      break;
    case core::OutputSpec::PixelFormat::kUInt16:
      config.data_type = DataType::kUInt16;
      break;
    case core::OutputSpec::PixelFormat::kFloat32:
      config.data_type = DataType::kFloat32;
      break;
    default:
      config.data_type = DataType::kUInt8;
      break;
  }

  config.planar_config = plan.output.planar_config;

  config.background.values.clear();
  config.background.values.reserve(std::min(config.channels, 4u));

  config.background.values.push_back(
      static_cast<double>(plan.output.background.r));
  if (config.channels > 1) {
    config.background.values.push_back(
        static_cast<double>(plan.output.background.g));
  }
  if (config.channels > 2) {
    config.background.values.push_back(
        static_cast<double>(plan.output.background.b));
  }
  if (config.channels > 3) {
    config.background.values.push_back(
        static_cast<double>(plan.output.background.a));
  }

  config.enable_blending = false;
  for (const auto& op : plan.operations) {
    if (op.blend_metadata.has_value()) {
      config.enable_blending = true;
      break;
    }
  }

  return config;
}

}  // namespace fastslide::runtime
