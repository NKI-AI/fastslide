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
// High-level reader functions for extracting image data

#ifndef SIMPLETIFF_READER_H_
#define SIMPLETIFF_READER_H_

#include <cstdint>
#include <vector>

#include "aifocore/status/result.h"
#include "simpletiff/index.h"

// Forward declarations for libjpeg to avoid exposing jpeglib.h in public API
struct jpeg_decompress_struct;
struct jpeg_error_mgr;

namespace simpletiff {

using aifocore::Result;

/// Decode context for buffer reuse during tile/strip reading
///
/// This makes allocation overhead explicit and eliminates hidden thread_local
/// state. Create one context per reading thread for best performance.
struct DecodeContext {
  std::vector<uint8_t>
      jpeg_stream_buffer;           ///< Reusable buffer for JPEG composition
  std::vector<uint8_t> temp_buffer; ///< General-purpose temporary buffer

  // JPEG decompressor state (opaque pointers to avoid exposing jpeglib.h)
  // Lazily initialized on first use
  jpeg_decompress_struct *jpeg_cinfo = nullptr;
  jpeg_error_mgr *jpeg_err = nullptr;

  /// Constructor
  DecodeContext();

  /// Destructor - cleans up JPEG resources
  ~DecodeContext();

  // Disable copy (JPEG state is non-copyable)
  DecodeContext(const DecodeContext &) = delete;
  DecodeContext &operator=(const DecodeContext &) = delete;

  // Enable move semantics
  DecodeContext(DecodeContext &&other) noexcept;
  DecodeContext &operator=(DecodeContext &&other) noexcept;
};

/// Read a single tile by index
///
/// Reuses buffers from the provided context to avoid allocations.
/// Thread-safe when each thread has its own DecodeContext.
///
/// @param index TIFF index
/// @param page_index Page/level index
/// @param tile_index Linear tile index (row-major order)
/// @param ctx Decode context for buffer reuse
/// @param dst Destination buffer (will be resized to fit decoded tile)
/// @param out_width Output tile width (actual decoded size)
/// @param out_height Output tile height (actual decoded size)
/// @return Result indicating success or descriptive error
Result<void> ReadTile(const TiffIndex &index, uint32_t page_index,
                      uint32_t tile_index, DecodeContext &ctx,
                      std::vector<uint8_t> &dst, int &out_width,
                      int &out_height);

/// Read raw compressed tile data without decompression
///
/// Used for applications that need raw compressed bytes (e.g.,
/// OpenSlide-compatible quickhash computation). This function reads the tile
/// data as-is without any decompression or processing.
///
/// Thread-safe as it only performs read operations.
///
/// @param index TIFF index
/// @param page_index Page/level index
/// @param tile_index Linear tile index (row-major order for tiles, strip index
/// for strips)
/// @param dst Destination buffer (will be resized to fit raw compressed data)
/// @return Result indicating success or descriptive error
Result<void> ReadRawTile(const TiffIndex &index, uint32_t page_index,
                         uint32_t tile_index, std::vector<uint8_t> &dst);

/// Read a tiled page into the provided buffer
///
/// @param index TIFF index
/// @param page_index Page/level index
/// @param roi Region of interest to read
/// @param ctx Decode context for buffer reuse
/// @param dst Destination buffer (must be pre-allocated)
/// @param dst_stride Row stride in bytes
/// @return Result indicating success or descriptive error
Result<void> ReadTiledPage(const TiffIndex &index, uint32_t page_index,
                           const Roi &roi, DecodeContext &ctx, uint8_t *dst,
                           int dst_stride);

/// Read and decompress a single strip
///
/// @param index TIFF index
/// @param page_index Page index
/// @param strip_index Strip index
/// @param ctx Decode context for buffer reuse
/// @param decompressed Output buffer (will be resized)
/// @return Result indicating success or descriptive error
Result<void> ReadStripe(const TiffIndex &index, uint32_t page_index,
                        uint32_t strip_index, DecodeContext &ctx,
                        std::vector<uint8_t> &decompressed);

/// Read a strip-based page into the provided buffer
///
/// @param index TIFF index
/// @param page_index Page index
/// @param roi Region of interest
/// @param ctx Decode context for buffer reuse
/// @param dst Destination buffer (must be pre-allocated for full page)
/// @param dst_stride Row stride in bytes
/// @return Result indicating success or descriptive error
Result<void> ReadStripedPage(const TiffIndex &index, uint32_t page_index,
                             const Roi &roi, DecodeContext &ctx, uint8_t *dst,
                             int dst_stride);

/// Read a single JPEG page into the provided buffer
///
/// @param index TIFF index
/// @param page_index Page index
/// @param ctx Decode context for buffer reuse
/// @param dst Destination buffer (will be resized)
/// @param dst_stride Row stride in bytes
/// @param out_width Output width
/// @param out_height Output height
/// @return Result indicating success or descriptive error
Result<void> ReadSingleJpegPage(const TiffIndex &index, uint32_t page_index,
                                DecodeContext &ctx, std::vector<uint8_t> &dst,
                                int &dst_stride, int &out_width,
                                int &out_height);

/// Read any page (dispatches based on storage type)
///
/// @param index TIFF index
/// @param page_index Page index
/// @param roi Region of interest (for tiled pages)
/// @param ctx Decode context for buffer reuse
/// @param dst Destination buffer
/// @param dst_stride Row stride in bytes
/// @return Result indicating success or descriptive error
Result<void> ReadPage(const TiffIndex &index, uint32_t page_index,
                      const Roi &roi, DecodeContext &ctx, uint8_t *dst,
                      int dst_stride);

} // namespace simpletiff

#endif // SIMPLETIFF_READER_H_
