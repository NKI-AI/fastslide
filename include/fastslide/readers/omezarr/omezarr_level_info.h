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

  /// @brief Number of chunks along Y, X and C for this level.
  [[nodiscard]] uint64_t ChunkCountY() const {
    return chunk_y == 0 ? 0 : (y_size + chunk_y - 1) / chunk_y;
  }

  [[nodiscard]] uint64_t ChunkCountX() const {
    return chunk_x == 0 ? 0 : (x_size + chunk_x - 1) / chunk_x;
  }

  [[nodiscard]] uint64_t ChunkCountC() const {
    if (c_axis == static_cast<size_t>(-1) || chunk_c == 0) {
      return 1;
    }
    return (c_size + chunk_c - 1) / chunk_c;
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

/// @brief On-disk chunk path relative to `level.array_dir`.
///
/// Zarr V3 default chunk-key encoding: a "c" prefix followed by one index per
/// array axis, joined by the array's `chunk_key_separator`. Axes that are
/// neither Y, X nor C index as 0.
///
/// @param level Level whose array metadata supplies the rank and separator.
/// @param chunk_y Chunk index along the Y axis.
/// @param chunk_x Chunk index along the X axis.
/// @param chunk_c Chunk index along the channel axis; ignored when absent.
/// @return Relative path, e.g. "c/0/3/7".
[[nodiscard]] inline std::string BuildChunkRelativePath(
    const OmeZarrLevelInfo& level, uint64_t chunk_y, uint64_t chunk_x,
    uint64_t chunk_c) {
  const char sep = level.array_metadata.chunk_key_separator;
  std::string path = "c";
  const auto rank = level.array_metadata.shape.size();
  for (size_t i = 0; i < rank; ++i) {
    uint64_t idx = 0;
    if (i == level.y_axis) {
      idx = chunk_y;
    } else if (i == level.x_axis) {
      idx = chunk_x;
    } else if (i == level.c_axis) {
      idx = chunk_c;
    }
    path.push_back(sep);
    path += std::to_string(idx);
  }
  return path;
}

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_LEVEL_INFO_H_
