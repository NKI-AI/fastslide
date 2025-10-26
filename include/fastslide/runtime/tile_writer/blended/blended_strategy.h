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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_BLENDED_STRATEGY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_BLENDED_STRATEGY_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/runtime/tile_writer/tile_writer_strategy.h"
#include "hwy/aligned_allocator.h"

namespace fastslide::runtime {

// SIMD padding for buffer allocations (AVX-512 = 16 floats = 64 bytes)
// Prevents buffer overruns with unaligned vector stores
constexpr size_t kSimdPadding = 16;

/// @brief Blended tile writing strategy (weighted composition in linear RGB)
class BlendedStrategy : public ITileWriterStrategy {
 public:
  explicit BlendedStrategy(const TileWriter::Config& config);

  absl::Status WriteTile(const core::TileReadOp& op,
                         std::span<const uint8_t> pixel_data,
                         uint32_t tile_width, uint32_t tile_height,
                         uint32_t tile_channels) override;

  /// @brief Write tile with explicit mutex for thread-safe accumulation
  absl::Status WriteTile(const core::TileReadOp& op,
                         std::span<const uint8_t> pixel_data,
                         uint32_t tile_width, uint32_t tile_height,
                         uint32_t tile_channels,
                         absl::Mutex& accumulator_mutex);

  absl::Status Finalize() override;
  ImageDimensions GetDimensions() const override;
  uint32_t GetChannels() const override;
  absl::StatusOr<Image> GetOutput() override;
  std::string GetName() const override;
  uint8_t* GetOutputBuffer() override;
  size_t GetOutputBufferSize() const override;

 private:
  absl::Status WriteRGBTileBlended(const core::TileReadOp& op,
                                   std::span<const uint8_t> pixel_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   absl::Mutex& accumulator_mutex);

  absl::Status WriteMultiChannelTileSimple(const core::TileReadOp& op,
                                           std::span<const uint8_t> pixel_data,
                                           uint32_t tile_width,
                                           uint32_t tile_height,
                                           uint32_t tile_channels);

  TileWriter::Config config_;
  std::unique_ptr<Image> output_image_;
  // Use aligned allocator for SIMD (AVX-512 requires 64-byte alignment)
  // Planar storage for better cache locality and sequential memory access
  hwy::AlignedVector<float> accumulator_r_;  // Linear R channel accumulator
  hwy::AlignedVector<float> accumulator_g_;  // Linear G channel accumulator
  hwy::AlignedVector<float> accumulator_b_;  // Linear B channel accumulator
  hwy::AlignedVector<float> weight_sum_;     // Weight sum per pixel
  absl::Mutex internal_mutex_;  // Mutex for non-parallel WriteTile calls
  bool finalized_ = false;
};

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_BLENDED_BLENDED_STRATEGY_H_
