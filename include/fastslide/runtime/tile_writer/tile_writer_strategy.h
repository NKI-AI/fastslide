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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_TILE_WRITER_STRATEGY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_TILE_WRITER_STRATEGY_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

namespace fastslide::runtime {

/// @brief Pure virtual interface for tile writing strategies
class ITileWriterStrategy {
 public:
  virtual ~ITileWriterStrategy() = default;

  virtual absl::Status WriteTile(const core::TileReadOp& op,
                                 std::span<const uint8_t> pixel_data,
                                 uint32_t tile_width, uint32_t tile_height,
                                 uint32_t tile_channels) = 0;

  virtual absl::Status Finalize() = 0;
  virtual ImageDimensions GetDimensions() const = 0;
  virtual uint32_t GetChannels() const = 0;
  virtual absl::StatusOr<Image> GetOutput() = 0;
  virtual std::string GetName() const = 0;

  virtual uint8_t* GetOutputBuffer() = 0;
  virtual size_t GetOutputBufferSize() const = 0;
};

}  // namespace fastslide::runtime

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_TILE_WRITER_STRATEGY_H_
