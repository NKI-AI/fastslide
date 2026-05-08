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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_LEVEL_INFO_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "fastslide/image.h"
#include "fastslide/readers/omezarr/omezarr_codec.h"
#include "fastslide/readers/omezarr/omezarr_metadata.h"

namespace fastslide {

/// @brief Per-level OME-Zarr metadata, prepared for tile reads.
///
/// Lives in its own header so that the planner and tile executor can refer to
/// the level descriptor without depending on `OmeZarrReader` itself. Keeping
/// the helpers reader-agnostic is what breaks the historical
/// `omezarr.cpp <-> omezarr_tile_executor.cpp` call-graph cycle: the executor
/// now operates on data only, never on the reader object.
struct OmeZarrLevelInfo {
  std::string array_dir;  ///< Absolute path to the level directory
  formats::omezarr::ZarrArrayMetadata
      array_metadata;  ///< Parsed Zarr V3 array metadata
  formats::omezarr::ZarrCodecChain codec_chain;

  /// @brief Axis indices into the Zarr `shape` and `chunk_shape` arrays.
  /// `c_axis` is `SIZE_MAX` when the array has no channel axis.
  size_t y_axis = 0;
  size_t x_axis = 0;
  size_t c_axis = static_cast<size_t>(-1);

  uint64_t y_size = 0;
  uint64_t x_size = 0;
  uint64_t c_size = 1;
  uint64_t chunk_y = 0;
  uint64_t chunk_x = 0;
  uint64_t chunk_c = 1;

  /// @brief Level dimensions (X, Y).
  ImageDimensions size = {0, 0};

  /// @brief Bytes per scalar pixel for this level.
  [[nodiscard]] uint32_t BytesPerSample() const {
    return array_metadata.dtype.BytesPerElement();
  }

  /// @brief Bytes for one (channel, y, x) plane within a chunk.
  [[nodiscard]] uint64_t ChunkSliceBytes() const {
    return chunk_y * chunk_x * BytesPerSample();
  }

  /// @brief Total decompressed bytes of one on-disk chunk (all channels).
  [[nodiscard]] uint64_t BytesPerChunk() const {
    return ChunkSliceBytes() * (chunk_c == 0 ? 1 : chunk_c);
  }
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_LEVEL_INFO_H_
