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

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INTERNAL_NDPI_MCU_TILING_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INTERNAL_NDPI_MCU_TILING_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace simpletiff::internal {

struct NdpiMcuTileGeometry {
  uint16_t tile_w = 0;
  uint16_t tile_h = 0;
  uint32_t tiles_x = 0;
  uint32_t tiles_y = 0;
};

// NDPI "MCU tiling" pages store a single JPEG strip, with per-tile start
// offsets described by tag NDPI_MCU_STARTS (65426). The actual tile size is
// determined from the JPEG header:
// - DRI (restart interval)
// - SOF sampling factors (MCU size)
//
// This mirrors OpenSlide's approach but avoids depending on libjpeg for header
// parsing.
[[nodiscard]] bool ComputeNdpiMcuTileGeometryFromJpegHeader(
    int file_descriptor, size_t file_size, uint64_t jpeg_start_offset,
    uint32_t image_width, uint32_t image_height, NdpiMcuTileGeometry& out);

// Build per-tile TIFF-like offset/bytecount arrays from NDPI_MCU_STARTS values.
//
// NDPI_MCU_STARTS (65426) is an array of N start offsets *within* the single
// JPEG strip bitstream, relative to StripOffsets[0]. The strip itself is a
// single (very large) JPEG stream.
//
// This helper constructs:
// - offsets[i]   = strip_start_offset + mcu_starts[i]
// - bytecounts[i]= mcu_starts[i+1] - mcu_starts[i] (last uses strip_byte_count)
[[nodiscard]] bool BuildOffsetsAndBytecountsFromNdpiMcuStarts(
    uint64_t strip_start_offset, uint64_t strip_byte_count,
    std::span<const uint64_t> mcu_starts, std::span<uint64_t> offsets_out,
    std::span<uint64_t> bytecounts_out);

// Convenience helper: compute tile geometry (from JPEG header) and construct
// offsets/bytecounts (from MCU_STARTS) in one call.
[[nodiscard]] bool BuildNdpiMcuTilesFromMcuStarts(
    int file_descriptor, size_t file_size, uint64_t strip_start_offset,
    uint64_t strip_byte_count, uint32_t image_width, uint32_t image_height,
    std::span<const uint64_t> mcu_starts, NdpiMcuTileGeometry& geometry_out,
    std::vector<uint64_t>& offsets_out, std::vector<uint64_t>& bytecounts_out);

}  // namespace simpletiff::internal

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_INTERNAL_NDPI_MCU_TILING_H_
