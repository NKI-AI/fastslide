// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/runtime/tile_writer/direct/fills.h"

namespace fastslide::runtime {

void Canvas::InitOutputImage() {
  if (config_.dimensions[0] == 0 || config_.dimensions[1] == 0 ||
      config_.channels == 0 || config_.background.values.empty()) {
    throw std::invalid_argument("Invalid Canvas configuration");
  }

  out_w_ = static_cast<int>(config_.dimensions[0]);
  out_h_ = static_cast<int>(config_.dimensions[1]);

  use_rgb8_blending_ =
      (config_.channels == 3 && config_.data_type == DataType::kUInt8 &&
       config_.planar_config == PlanarConfig::kContiguous);

  use_rgb16_copy_blending_ =
      (config_.channels == 3 && config_.data_type == DataType::kUInt16 &&
       config_.planar_config == PlanarConfig::kContiguous);

  // For fluorescence slides we tag the output as kSpectral even when there
  // are 3 or 4 channels; the buffer layout is identical to RGB(A) but the
  // semantics are independent fluorophores, and downstream consumers
  // (FFI, viewers) need that distinction to route into the multi-channel
  // path.
  if (config_.force_spectral_image) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, config_.channels,
                                config_.data_type, config_.planar_config);
  } else if (config_.channels == 3) {
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

  uint8_t* buf = output_image_->GetData();

  if (use_rgb8_blending_) {
    const uint8_t bg_r =
        !config_.background.values.empty()
            ? static_cast<uint8_t>(config_.background.values[0])
            : 255;
    const uint8_t bg_g =
        config_.background.values.size() > 1
            ? static_cast<uint8_t>(config_.background.values[1])
            : bg_r;
    const uint8_t bg_b =
        config_.background.values.size() > 2
            ? static_cast<uint8_t>(config_.background.values[2])
            : bg_r;

    FillRGB8(buf, out_w_, out_h_, bg_r, bg_g, bg_b);

    const size_t pixel_count = static_cast<size_t>(out_w_) * out_h_;
    coverage_.resize(pixel_count, 0);
  } else if (use_rgb16_copy_blending_) {
    // 16-bit MRXS fluorescence: scale the 8-bit background up by 0x101 so
    // identical RGB triplets map to the equivalent 16-bit value (Cairo /
    // lodepng convention). For arbitrary channel-specific backgrounds we
    // fall back to plain channel-0 grey.
    const auto scale8to16 = [](double v) {
      const uint16_t v8 = static_cast<uint16_t>(v);
      return static_cast<uint16_t>(v8 * 0x0101U);
    };
    const uint16_t bg_r = scale8to16(config_.background.values[0]);
    const uint16_t bg_g = config_.background.values.size() > 1
                              ? scale8to16(config_.background.values[1])
                              : bg_r;
    const uint16_t bg_b = config_.background.values.size() > 2
                              ? scale8to16(config_.background.values[2])
                              : bg_r;

    FillRGB<uint16_t>(reinterpret_cast<uint16_t*>(buf), out_w_, out_h_, bg_r,
                      bg_g, bg_b);
    const size_t pixel_count = static_cast<size_t>(out_w_) * out_h_;
    coverage_.resize(pixel_count, 0);
  } else {
    ZeroInit(buf, output_image_->SizeBytes());
  }
}

aifocore::Status Canvas::FillBackground(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t* buffer = output_image_ ? output_image_->GetData() : nullptr;
  if (!buffer) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No output buffer available");
  }

  // For >4 channels (e.g. fluorescence with 5+ filters) we don't have a
  // meaningful 8-bit RGB(A) background, so just clear the buffer.
  if (config_.channels > 4) {
    const size_t total_bytes = static_cast<size_t>(config_.dimensions[0]) *
                               config_.dimensions[1] * config_.channels *
                               GetDataTypeSize(config_.data_type);
    std::memset(buffer, 0, total_bytes);
    return aifocore::Status::OkStatus();
  }

  // 16-bit 3-channel RGB (MRXS fluorescence): scale 8-bit background up
  // by 0x101 to keep grayscale values consistent with the InitOutputImage
  // background.
  if (config_.channels == 3 && config_.data_type == DataType::kUInt16) {
    const uint16_t r16 = static_cast<uint16_t>(r) * 0x0101U;
    const uint16_t g16 = static_cast<uint16_t>(g) * 0x0101U;
    const uint16_t b16 = static_cast<uint16_t>(b) * 0x0101U;
    FillRGB<uint16_t>(reinterpret_cast<uint16_t*>(buffer),
                      config_.dimensions[0], config_.dimensions[1], r16, g16,
                      b16);
    return aifocore::Status::OkStatus();
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
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("FillBackground not implemented for {} channels",
                              config_.channels));
  }

  return aifocore::Status::OkStatus();
}

Canvas::Config Canvas::AnalyzePlan(const core::TilePlan& plan) {
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

  config.force_spectral_image = plan.output.force_spectral_image;

  return config;
}

}  // namespace fastslide::runtime
