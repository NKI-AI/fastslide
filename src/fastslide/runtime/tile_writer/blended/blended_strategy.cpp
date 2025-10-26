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

#include "fastslide/runtime/tile_writer/blended/blended_strategy.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/runtime/tile_writer/blended/accumulate.h"
#include "fastslide/runtime/tile_writer/blended/mks_kernel.h"
#include "fastslide/runtime/tile_writer/blended/resample_mks.h"
#include "fastslide/runtime/tile_writer/blended/srgb_linear.h"
#include "fastslide/runtime/tile_writer/direct/copy_rgb8.h"
#include "fastslide/runtime/tile_writer/direct/fills.h"
#include "fastslide/runtime/tile_writer/tile_writer_strategy.h"
#include "hwy/aligned_allocator.h"

namespace fastslide::runtime {

BlendedStrategy::BlendedStrategy(const TileWriter::Config& config)
    : config_(config) {
  if (config_.dimensions[0] == 0 || config_.dimensions[1] == 0 ||
      config_.channels == 0 || config_.background.values.empty()) {
    throw std::invalid_argument("Invalid TileWriter configuration");
  }

  const size_t pixel_count =
      static_cast<size_t>(config_.dimensions[0]) * config_.dimensions[1];

  if (config_.channels == 3) {
    // Use planar float accumulators for better cache locality
    // Padding prevents buffer overruns from vectorized stores
    accumulator_r_.resize(pixel_count + kSimdPadding, 0.0f);
    accumulator_g_.resize(pixel_count + kSimdPadding, 0.0f);
    accumulator_b_.resize(pixel_count + kSimdPadding, 0.0f);
    weight_sum_.resize(pixel_count + kSimdPadding, 0.0f);

    // Verify allocations in debug builds
    DCHECK_EQ(accumulator_r_.size(), pixel_count + kSimdPadding);
    DCHECK_EQ(accumulator_g_.size(), pixel_count + kSimdPadding);
    DCHECK_EQ(accumulator_b_.size(), pixel_count + kSimdPadding);
    DCHECK_EQ(weight_sum_.size(), pixel_count + kSimdPadding);
  } else {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, config_.channels,
                                config_.data_type, config_.planar_config);
    ZeroInit(output_image_->GetData(), output_image_->SizeBytes());
  }
}

absl::Status BlendedStrategy::WriteTile(const core::TileReadOp& op,
                                        std::span<const uint8_t> pixel_data,
                                        uint32_t tile_width,
                                        uint32_t tile_height,
                                        uint32_t tile_channels) {
  // Use internal mutex for non-parallel calls
  return WriteTile(op, pixel_data, tile_width, tile_height, tile_channels,
                   internal_mutex_);
}

absl::Status BlendedStrategy::WriteTile(const core::TileReadOp& op,
                                        std::span<const uint8_t> pixel_data,
                                        uint32_t tile_width,
                                        uint32_t tile_height,
                                        uint32_t tile_channels,
                                        absl::Mutex& accumulator_mutex) {
  if (config_.channels == 3 && tile_channels == 3) {
    return WriteRGBTileBlended(op, pixel_data, tile_width, tile_height,
                               accumulator_mutex);
  } else {
    return WriteMultiChannelTileSimple(op, pixel_data, tile_width, tile_height,
                                       tile_channels);
  }
}

absl::Status BlendedStrategy::Finalize() {
  if (finalized_)
    return absl::OkStatus();

  if (config_.channels == 3) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kRGB,
                                config_.data_type, config_.planar_config);

    uint8_t* out = output_image_->GetData();

    FinalizeLinearToSrgb8(accumulator_r_.data(), accumulator_g_.data(),
                          accumulator_b_.data(), weight_sum_.data(),
                          config_.dimensions[0], config_.dimensions[1], out);
  }

  finalized_ = true;
  return absl::OkStatus();
}

ImageDimensions BlendedStrategy::GetDimensions() const {
  return config_.dimensions;
}

uint32_t BlendedStrategy::GetChannels() const {
  return config_.channels;
}

absl::StatusOr<Image> BlendedStrategy::GetOutput() {
  if (!finalized_) {
    RETURN_IF_ERROR(Finalize(), "Failed to finalize output");
  }
  if (!output_image_) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "No output image available");
  }
  return std::move(*output_image_);
}

std::string BlendedStrategy::GetName() const {
  return "NativeBlended";
}

uint8_t* BlendedStrategy::GetOutputBuffer() {
  if (!output_image_ && config_.channels == 3) {
    output_image_ =
        std::make_unique<Image>(config_.dimensions, ImageFormat::kRGB,
                                config_.data_type, config_.planar_config);
    ZeroInit(output_image_->GetData(), output_image_->SizeBytes());
  }
  return output_image_ ? output_image_->GetData() : nullptr;
}

size_t BlendedStrategy::GetOutputBufferSize() const {
  const size_t pixel_count =
      static_cast<size_t>(config_.dimensions[0]) * config_.dimensions[1];
  return pixel_count * config_.channels;
}

absl::Status BlendedStrategy::WriteRGBTileBlended(
    const core::TileReadOp& op, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, absl::Mutex& accumulator_mutex) {
  const bool has_blend_meta = op.blend_metadata.has_value();
  const double frac_x = has_blend_meta ? op.blend_metadata->fractional_x : 0.0;
  const double frac_y = has_blend_meta ? op.blend_metadata->fractional_y : 0.0;
  const float weight =
      has_blend_meta ? static_cast<float>(op.blend_metadata->weight) : 1.0f;
  const bool enable_subpixel =
      has_blend_meta && op.blend_metadata->enable_subpixel_resampling &&
      (std::abs(frac_x) > 1e-12 || std::abs(frac_y) > 1e-12);

  // Step 1: Convert sRGB → linear planar
  const size_t plane = static_cast<size_t>(tile_width) * tile_height;
  // Pad each plane to SIMD vector boundary to avoid buffer overruns
  const size_t padded_plane = plane + kSimdPadding;
  hwy::AlignedVector<float> src_linear(padded_plane * 3, 0.0f);
  ConvertSrgb8ToLinearPlanar(pixel_data.data(), tile_width, tile_height,
                             src_linear.data());

  // Step 1.5: Apply intensity gain correction (if gain != 1.0)
  const float gain = op.blend_metadata.value_or(core::BlendMetadata{}).gain;
  if (std::abs(gain - 1.0f) > 0.0001f) {
    GainCorrectionLinearPlanar(src_linear.data(), plane, gain);
  }

  // Step 2: Apply subpixel shift if needed
  const float* use_linear = src_linear.data();
  hwy::AlignedVector<float> resampled_linear;
  if (enable_subpixel) {
    // Skip for very small tiles
    if (tile_width < 2 * kMksRadius || tile_height < 2 * kMksRadius) {
      use_linear = src_linear.data();
    } else {
      resampled_linear.resize(padded_plane * 3, 0.0f);
      ResampleTileSubpixel(src_linear.data(), tile_width, tile_height, frac_x,
                           frac_y, resampled_linear.data());
      use_linear = resampled_linear.data();
    }
  }

  // Step 3: Accumulate weighted pixels (thread-safe with mutex)
  const int32_t base_x = static_cast<int32_t>(op.transform.dest.x);
  const int32_t base_y = static_cast<int32_t>(op.transform.dest.y);

  AccumulateLinearTile(use_linear, tile_width, tile_height, base_x, base_y,
                       weight, accumulator_r_.data(), accumulator_g_.data(),
                       accumulator_b_.data(), weight_sum_.data(),
                       config_.dimensions[0], config_.dimensions[1],
                       accumulator_mutex);

  return absl::OkStatus();
}

absl::Status BlendedStrategy::WriteMultiChannelTileSimple(
    const core::TileReadOp& op, std::span<const uint8_t> pixel_data,
    uint32_t tile_width, uint32_t tile_height, uint32_t tile_channels) {
  if (!output_image_)
    return absl::InternalError("No output image for multi-channel");

  const auto& src = op.transform.source;
  const auto& dst = op.transform.dest;
  uint8_t* image_data = output_image_->GetData();
  const uint32_t bytes_per_sample = output_image_->GetBytesPerSample();

  CopyRectGeneral(pixel_data.data(), tile_width, tile_height, tile_channels,
                  src.x, src.y, image_data, config_.dimensions[0],
                  config_.dimensions[1], config_.channels, dst.x, dst.y,
                  dst.width, dst.height, bytes_per_sample);

  return absl::OkStatus();
}

}  // namespace fastslide::runtime
