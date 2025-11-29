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

// I/O utilities for reading TIFF files

#ifndef AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_IO_UTILS_H_
#define AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_IO_UTILS_H_

#include <cstdint>
#include <span>
#include <vector>

namespace simpletiff {

// Forward declaration
struct DecodeContext;

/// Read bytes from file into vector using pread
///
/// @param fd File descriptor
/// @param file_size Size of the file
/// @param offset Offset in file
/// @param length Number of bytes to read
/// @param out Output buffer (will be resized)
/// @return true on success, false on failure
bool ReadBytes(int fd, size_t file_size, uint64_t offset, uint64_t length,
               std::vector<uint8_t>& out);

/// Read bytes from file into vector using pread (returns span to data)
///
/// This is a convenience wrapper that reads into a buffer and returns a span.
/// Note: Unlike the old mmap version, this DOES allocate and copy data.
///
/// @param fd File descriptor
/// @param file_size Size of the file
/// @param offset Offset in file
/// @param length Number of bytes to read
/// @param buffer Thread-local buffer for reading (reused to avoid allocations)
/// @return Span view into buffer, empty span on failure
std::span<const uint8_t> ReadBytesSpan(int fd, size_t file_size,
                                       uint64_t offset, uint64_t length,
                                       std::vector<uint8_t>& buffer);

/// Compose a complete JPEG bitstream from tables and payload
///
/// For OJPEG format, this combines:
/// - SOI marker
/// - JPEGTables (if present)
/// - Tile/strip payload
/// - EOI marker
///
/// Handles cases where payload already has SOI/EOI markers.
/// Optimized to reuse the destination buffer to avoid allocations.
///
/// @param tables JPEG tables data (may be empty)
/// @param payload Tile or strip data
/// @param out Output buffer (will be resized and filled)
void ComposeJpegStream(std::span<const uint8_t> tables,
                       std::span<const uint8_t> payload,
                       std::vector<uint8_t>& out);

/// Decode JPEG data to native format
///
/// Preserves the native color space:
/// - Grayscale JPEG → 1-channel output
/// - RGB JPEG → 3-channel RGB output
/// - YCbCr JPEG → 3-channel RGB output (treated as RGB for OJPEG compatibility)
///
/// @param ctx Decode context (owns JPEG decompressor state)
/// @param jpeg_data JPEG compressed data
/// @param out_width Output width (will be set)
/// @param out_height Output height (will be set)
/// @param out_rgb Output buffer (will be resized to width*height*channels)
/// @return true on success, false on failure
bool DecodeJpeg(DecodeContext& ctx, std::span<const uint8_t> jpeg_data,
                int& out_width, int& out_height, std::vector<uint8_t>& out_rgb);

/// Copy a tile into destination buffer with clipping
///
/// Handles edge tiles that may be smaller than the nominal tile size.
///
/// @param dst Destination buffer
/// @param dst_stride Destination row stride in bytes
/// @param tile_data Source tile data
/// @param tile_width Tile width
/// @param tile_height Tile height
/// @param dst_x X position in destination
/// @param dst_y Y position in destination
/// @param roi_width Total ROI width
/// @param roi_height Total ROI height
/// @param samples_per_pixel Number of samples per pixel (channels)
void CopyTileInto(uint8_t* dst, int dst_stride, const uint8_t* tile_data,
                  int tile_width, int tile_height, int dst_x, int dst_y,
                  int roi_width, int roi_height, int samples_per_pixel);

}  // namespace simpletiff

#endif  // AIFO_SIMPLETIFF_INCLUDE_SIMPLETIFF_IO_UTILS_H_
