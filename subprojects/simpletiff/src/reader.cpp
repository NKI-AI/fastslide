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

#include "simpletiff/reader.h"

#include "aifocore/platform/portability.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

#include "aifocore/status/result.h"
#include "simpletiff/decompression.h"
#include "simpletiff/errors.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/tiff_constants.h"
#include "simpletiff/tiff_parser.h"

namespace simpletiff {

using aifocore::Error;
using aifocore::Result;

// =============================================================================
// Internal JPEG helpers
// =============================================================================

namespace {

/// Decompress JPEG data with optional JPEG tables
///
/// This helper handles the common pattern of:
/// 1. Reading JPEG tables (if present)
/// 2. Composing a complete JPEG stream (tables + compressed data)
/// 3. Decoding to RGB
///
/// This eliminates duplication between ReadTile and ReadStripe.
///
/// @param compressed_data Compressed JPEG data span
/// @param jpeg_tables_span Optional JPEG tables (can be empty)
/// @param jpeg_stream_buffer Thread-local buffer for composed stream (reused)
/// @param out_width Output image width
/// @param out_height Output image height
/// @param dst Output RGB buffer (will be resized)
/// @return Result indicating success or error
Result<void> DecompressJpegWithTables(std::span<const uint8_t> compressed_data,
                                      std::span<const uint8_t> jpeg_tables_span,
                                      std::vector<uint8_t> &jpeg_stream_buffer,
                                      DecodeContext &ctx, int &out_width,
                                      int &out_height,
                                      std::vector<uint8_t> &dst) {
  std::span<const uint8_t> jpeg_data_span;

  // Compose JPEG stream if tables are present
  if (!jpeg_tables_span.empty()) {
    ComposeJpegStream(jpeg_tables_span, compressed_data, jpeg_stream_buffer);
    jpeg_data_span = jpeg_stream_buffer;
  } else {
    jpeg_data_span = compressed_data;
  }

  // Decode JPEG to native format (preserves grayscale as 1-channel, RGB as
  // 3-channel)
  if (!DecodeJpeg(ctx, jpeg_data_span, out_width, out_height, dst)) {
    return Error("JPEG decompression failed");
  }
  return Result<void>();
}

/// Load JPEG tables with thread-safe lazy caching
///
/// Both TilesRec and StripsRec share the same JPEG table cache structure.
/// This template extracts the common loading logic to avoid duplication.
///
/// @tparam CacheRec Type with jpeg_tables_* fields (TilesRec or StripsRec)
/// @param index TIFF index (for mmap and mutex)
/// @param cache_rec Tiles or strips record containing table cache
/// @return Result containing span to cached JPEG tables
template <typename CacheRec>
Result<std::span<const uint8_t>>
LoadJpegTablesFromCache(const TiffIndex &index, const CacheRec &cache_rec) {
  auto state = cache_rec.jpeg_tables_state.load(std::memory_order_acquire);

  if (state == JpegTablesState::kLoaded) {
    return std::span<const uint8_t>(cache_rec.jpeg_tables_cache);
  }

  if (cache_rec.jpeg_tables_len == 0) {
    return std::span<const uint8_t>();
  }

  // Single thread performs the I/O while others wait by spinning briefly.
  JpegTablesState expected = JpegTablesState::kUninitialized;
  if (state == JpegTablesState::kUninitialized &&
      cache_rec.jpeg_tables_state.compare_exchange_strong(
          expected, JpegTablesState::kLoading, std::memory_order_acq_rel)) {
    if (!ReadBytes(index.Fd(), index.FileSize(), cache_rec.jpeg_tables_off,
                   cache_rec.jpeg_tables_len, cache_rec.jpeg_tables_cache)) {
      cache_rec.jpeg_tables_state.store(JpegTablesState::kFailed,
                                        std::memory_order_release);
      return Error("Failed to load JPEG tables from file");
    }
    cache_rec.jpeg_tables_state.store(JpegTablesState::kLoaded,
                                      std::memory_order_release);
    return std::span<const uint8_t>(cache_rec.jpeg_tables_cache);
  }

  // Wait for the loading thread to finish.
  while ((state = cache_rec.jpeg_tables_state.load(
              std::memory_order_acquire)) == JpegTablesState::kLoading) {
    std::this_thread::yield();
  }

  if (state == JpegTablesState::kLoaded) {
    return std::span<const uint8_t>(cache_rec.jpeg_tables_cache);
  }

  if (state == JpegTablesState::kFailed) {
    return Error("Failed to load JPEG tables from file");
  }

  return std::span<const uint8_t>();
}

} // namespace

// =============================================================================
// Public API
// =============================================================================

Result<void> ReadTile(const TiffIndex &index, uint32_t page_index,
                      uint32_t tile_index, DecodeContext &ctx,
                      std::vector<uint8_t> &dst, int &out_width,
                      int &out_height) {
  if (page_index >= index.NumPages()) {
    return Error("Page index " + std::to_string(page_index) +
                 " out of range (file has " + std::to_string(index.NumPages()) +
                 " pages)");
  }

  const auto &page = index.Page(page_index);
  if (page.storage != Storage::kTiles) {
    return Error("Page " + std::to_string(page_index) +
                 " is not tiled (cannot use ReadTile)");
  }

  const auto &tiles = index.Tiles(page.payload_id);

  // Use optimized single-tile loader (avoids loading entire offset/bytecount
  // arrays)
  uint64_t offset = 0;
  uint64_t bytecount = 0;
  if (!EnsureTileLoaded(index, page_index, tile_index, offset, bytecount)) {
    return Error("Failed to load tile " + std::to_string(tile_index) +
                 " on page " + std::to_string(page_index));
  }

  // Read tile data using pread
  auto tile_data_span = ReadBytesSpan(index.Fd(), index.FileSize(), offset,
                                      bytecount, ctx.temp_buffer);
  if (tile_data_span.empty()) {
    return Error("Failed to read tile data from file");
  }

  if (IsCompression(page.compression, Compression::kZstd)) {
    // ZSTD compression
    if (!DecompressZstd(tile_data_span, dst)) {
      return Error("ZSTD decompression failed for tile " +
                   std::to_string(tile_index) + " on page " +
                   std::to_string(page_index));
    }

    // Set dimensions from tile metadata
    out_width = static_cast<int>(tiles.tile_w);
    out_height = static_cast<int>(tiles.tile_h);

    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kLzw)) {
    // LZW compression
    if (!DecompressLzw(tile_data_span, dst)) {
      return Error("LZW decompression failed for tile " +
                   std::to_string(tile_index) + " on page " +
                   std::to_string(page_index));
    }

    // Set dimensions from tile metadata
    out_width = static_cast<int>(tiles.tile_w);
    out_height = static_cast<int>(tiles.tile_h);

    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kJpeg)) {
    // JPEG compression - load tables from cache and decompress
    AIFOCORE_ASSIGN_OR_RETURN(auto jpeg_tables_span,
                              LoadJpegTablesFromCache(index, tiles));

    // Decompress JPEG using common helper with explicit context
    auto decompress_result = DecompressJpegWithTables(
        tile_data_span, jpeg_tables_span, ctx.jpeg_stream_buffer, ctx,
        out_width, out_height, dst);
    if (!decompress_result) {
      return Error("JPEG decompression failed for tile " +
                   std::to_string(tile_index) + " on page " +
                   std::to_string(page_index) + ": " +
                   decompress_result.error().message());
    }

    return Result<void>();
  } else {
    return Error(
        UnsupportedFormatError::Compression(page.compression, page_index)
            .what());
  }
}

Result<void> ReadRawTile(const TiffIndex &index, uint32_t page_index,
                         uint32_t tile_index, std::vector<uint8_t> &dst) {
  if (page_index >= index.NumPages()) {
    return Error("Page index " + std::to_string(page_index) +
                 " out of range (file has " + std::to_string(index.NumPages()) +
                 " pages)");
  }

  const auto &page = index.Page(page_index);

  // Support both tiled and strip storage for raw reading
  uint64_t offset = 0;
  uint64_t bytecount = 0;

  if (page.storage == Storage::kTiles) {
    // Use optimized single-tile loader
    if (!EnsureTileLoaded(index, page_index, tile_index, offset, bytecount)) {
      return Error("Failed to load tile " + std::to_string(tile_index) +
                   " on page " + std::to_string(page_index));
    }
  } else if (page.storage == Storage::kStrips) {
    // Use optimized single-strip loader
    if (!EnsureTileLoaded(index, page_index, tile_index, offset, bytecount)) {
      return Error("Failed to load strip " + std::to_string(tile_index) +
                   " on page " + std::to_string(page_index));
    }
  } else {
    return Error("Page " + std::to_string(page_index) +
                 " uses unsupported storage type for raw reading");
  }

  // Skip zero-length tiles (can happen with malformed files)
  if (bytecount == 0) {
    dst.clear();
    return Result<void>();
  }

  // Read raw compressed data directly into destination buffer
  dst.resize(bytecount);

  ssize_t bytes_read =
      aifocore::portable_pread(index.Fd(), dst.data(), bytecount, offset);

  if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != bytecount) {
    return Error("Failed to read raw tile/strip data from file at offset " +
                 std::to_string(offset));
  }

  return Result<void>();
}

Result<void> ReadTiledPage(const TiffIndex &index, uint32_t page_index,
                           const Roi &roi, DecodeContext &ctx, uint8_t *dst,
                           int dst_stride) {
  if (page_index >= index.NumPages()) {
    return Error("Page index " + std::to_string(page_index) +
                 " out of range (file has " + std::to_string(index.NumPages()) +
                 " pages)");
  }

  // Ensure page data is loaded (lazy load if needed)
  if (!EnsurePageLoaded(index, page_index)) {
    return Error("Failed to load page " + std::to_string(page_index) +
                 " metadata");
  }

  const auto &page = index.Page(page_index);
  if (page.storage != Storage::kTiles) {
    return Error("Page " + std::to_string(page_index) +
                 " is not tiled (cannot use ReadTiledPage)");
  }

  // Check if compression is supported
  if (!IsCompression(page.compression, Compression::kJpeg) &&
      !IsCompression(page.compression, Compression::kZstd)) {
    return Error(
        UnsupportedFormatError::Compression(page.compression, page_index)
            .what());
  }

  const auto &tiles = index.Tiles(page.payload_id);

  // Compute tile range intersecting ROI
  const uint32_t tx0 = roi.x / tiles.tile_w;
  const uint32_t ty0 = roi.y / tiles.tile_h;
  const uint32_t tx1 = (roi.x + roi.width - 1) / tiles.tile_w;
  const uint32_t ty1 = (roi.y + roi.height - 1) / tiles.tile_h;

  // Reuse buffers while walking tiles to avoid repeated allocations
  std::vector<uint8_t> tile_data;
  int tile_w = 0;
  int tile_h = 0;

  // Process each tile in the ROI
  for (uint32_t ty = ty0; ty <= std::min(ty1, tiles.tiles_y - 1); ++ty) {
    for (uint32_t tx = tx0; tx <= std::min(tx1, tiles.tiles_x - 1); ++tx) {
      const uint32_t tile_idx = ty * tiles.tiles_x + tx;

      // Read and decode the tile (get actual dimensions for edge tiles)
      AIFOCORE_RETURN_IF_ERROR(ReadTile(index, page_index, tile_idx, ctx,
                                        tile_data, tile_w, tile_h));

      // Copy tile into destination buffer (using actual tile dimensions)
      const int dst_x = static_cast<int>(tx * tiles.tile_w - roi.x);
      const int dst_y = static_cast<int>(ty * tiles.tile_h - roi.y);
      CopyTileInto(dst, dst_stride, tile_data.data(), tile_w, tile_h, dst_x,
                   dst_y, static_cast<int>(roi.width),
                   static_cast<int>(roi.height), page.samples_per_pixel);
    }
  }

  return Result<void>();
}

Result<void> ReadStripe(const TiffIndex &index, uint32_t page_index,
                        uint32_t strip_index, DecodeContext &ctx,
                        std::vector<uint8_t> &decompressed) {
  if (page_index >= index.NumPages()) {
    return Error("Page index " + std::to_string(page_index) +
                 " out of range (file has " + std::to_string(index.NumPages()) +
                 " pages)");
  }

  // Ensure page data is loaded (lazy load if needed)
  if (!EnsurePageLoaded(index, page_index)) {
    return Error("Failed to load page " + std::to_string(page_index) +
                 " metadata");
  }

  const auto &page = index.Page(page_index);
  if (page.storage != Storage::kStrips) {
    return Error("Page " + std::to_string(page_index) +
                 " is not striped (cannot use ReadStripe)");
  }

  const auto &strips = index.Strips(page.payload_id);
  auto offsets = index.Offsets(strips.offsets);
  auto bytecounts = index.Bytecounts(strips.bytecounts);

  if (strip_index >= offsets.size()) {
    return Error("Strip index " + std::to_string(strip_index) +
                 " out of range on page " + std::to_string(page_index) +
                 " (has " + std::to_string(offsets.size()) + " strips)");
  }

  // Read strip data using pread
  auto strip_data_span =
      ReadBytesSpan(index.Fd(), index.FileSize(), offsets[strip_index],
                    bytecounts[strip_index], ctx.temp_buffer);
  if (strip_data_span.empty()) {
    return Error("Failed to read strip data from file");
  }

  if (IsCompression(page.compression, Compression::kJpeg)) {
    // JPEG compression - load tables from cache and decompress
    AIFOCORE_ASSIGN_OR_RETURN(auto jpeg_tables_span,
                              LoadJpegTablesFromCache(index, strips));

    // Decompress JPEG using common helper with explicit context
    int strip_w = 0, strip_h = 0;
    auto decompress_result = DecompressJpegWithTables(
        strip_data_span, jpeg_tables_span, ctx.jpeg_stream_buffer, ctx, strip_w,
        strip_h, decompressed);
    if (!decompress_result) {
      return Error(
          DecompressionError::Codec("JPEG", page_index, strip_index).what());
    }
  } else if (IsCompression(page.compression, Compression::kLzw)) {
    // LZW compression
    if (!DecompressLzw(strip_data_span, decompressed)) {
      return Error(
          DecompressionError::Codec("LZW", page_index, strip_index).what());
    }
  } else if (IsCompression(page.compression, Compression::kZstd)) {
    // ZSTD compression
    if (!DecompressZstd(strip_data_span, decompressed)) {
      return Error(
          DecompressionError::Codec("ZSTD", page_index, strip_index).what());
    }
  } else if (IsCompression(page.compression, Compression::kNone)) {
    // Uncompressed - copy data
    decompressed.assign(strip_data_span.begin(), strip_data_span.end());
  } else {
    return Error(
        UnsupportedFormatError::Compression(page.compression, page_index)
            .what());
  }

  // Apply predictor if needed (AFTER decompression)
  const uint32_t bytes_per_sample = ComputeBytesPerSample(page.bits_per_sample);
  if (bytes_per_sample == 0) {
    return Error(
        UnsupportedFormatError::BitsPerSample(page.bits_per_sample, page_index)
            .what());
  }

  const uint32_t bytes_per_pixel = bytes_per_sample * page.samples_per_pixel;
  const uint32_t full_row_bytes = page.width * bytes_per_pixel;

  if (bytes_per_pixel == 0) {
    return Error(InvalidPageError::Parameters(
                     page_index, page.samples_per_pixel, bytes_per_sample)
                     .what());
  }

  const int strip_h = static_cast<int>(decompressed.size() / full_row_bytes);

  // IMPORTANT: Predictor is applied PER-STRIP, not across strips!
  if (page.predictor == 2 &&
      (IsCompression(page.compression, Compression::kLzw) ||
       IsCompression(page.compression, Compression::kNone) ||
       IsCompression(page.compression, Compression::kZstd))) {
    ApplyHorizontalPredictor(decompressed, page.width, strip_h,
                             page.samples_per_pixel, page.bits_per_sample,
                             !index.IsLittleEndian(), // file_big_endian
                             1); // planar_configuration (CONTIG)
  }

  return Result<void>();
}

// =============================================================================
// Internal helpers for strip-based reading
// =============================================================================

namespace {

/// Parameters for ROI-based strip row extraction
struct StripRoiParams {
  uint32_t roi_x;
  uint32_t roi_y;
  uint32_t roi_w;
  uint32_t roi_h;
  uint32_t full_row_bytes;
  uint32_t roi_row_bytes;
  uint32_t roi_row_offset;
};

/// Compute clamped ROI parameters for a page
///
/// @param page Page header with dimensions
/// @param roi Requested region of interest
/// @param bytes_per_pixel Bytes per pixel (for computing byte offsets)
/// @param params Output parameters (will be filled)
/// @return true if ROI is valid (non-zero area), false otherwise
bool ComputeStripRoiParams(const PageHeader &page, const Roi &roi,
                           uint32_t bytes_per_pixel, StripRoiParams &params) {
  // Clamp ROI to page bounds
  params.roi_x = std::min(roi.x, page.width);
  params.roi_y = std::min(roi.y, page.height);
  const uint32_t max_roi_w = page.width - params.roi_x;
  const uint32_t max_roi_h = page.height - params.roi_y;
  params.roi_w = std::min(roi.width, max_roi_w);
  params.roi_h = std::min(roi.height, max_roi_h);

  if (params.roi_w == 0 || params.roi_h == 0) {
    return false;
  }

  // Compute byte-level parameters
  params.full_row_bytes = page.width * bytes_per_pixel;
  params.roi_row_bytes = params.roi_w * bytes_per_pixel;
  params.roi_row_offset = params.roi_x * bytes_per_pixel;

  return true;
}

/// Copy rows from a strip into the destination buffer, honoring ROI
///
/// This function extracts only the rows that intersect with the ROI
/// and copies the relevant portion of each row to the destination.
///
/// @param strip_data Decompressed strip data
/// @param strip_height Height of the strip in pixels
/// @param y_offset Y coordinate of the strip's first row in the full image
/// @param params ROI parameters
/// @param dst Destination buffer
/// @param dst_stride Destination row stride
/// @return Number of rows written to destination
uint32_t CopyStripRowsToRoi(const std::vector<uint8_t> &strip_data,
                            uint32_t strip_height, uint32_t y_offset,
                            const StripRoiParams &params, uint8_t *dst,
                            int dst_stride) {
  uint32_t rows_written = 0;
  const int row_bytes = static_cast<int>(params.full_row_bytes);

  for (uint32_t r = 0; r < strip_height; ++r) {
    const uint32_t global_y = y_offset + r;

    // Skip rows outside the ROI
    if (global_y < params.roi_y || global_y >= params.roi_y + params.roi_h) {
      continue;
    }

    // Compute source and destination pointers
    const uint8_t *src_row =
        strip_data.data() + r * row_bytes + params.roi_row_offset;
    uint8_t *dst_row = dst + (global_y - params.roi_y) * dst_stride;

    // Copy the ROI portion of the row
    const uint32_t available = params.full_row_bytes - params.roi_row_offset;
    const uint32_t copy_bytes = std::min(params.roi_row_bytes, available);
    const size_t max_copy = static_cast<size_t>(dst_stride);
    std::memcpy(dst_row, src_row,
                std::min(static_cast<size_t>(copy_bytes), max_copy));

    rows_written++;
  }

  return rows_written;
}

} // namespace

// =============================================================================
// Public API
// =============================================================================

Result<void> ReadStripedPage(const TiffIndex &index, uint32_t page_index,
                             const Roi &roi, DecodeContext &ctx, uint8_t *dst,
                             int dst_stride) {
  // ========================================
  // 1. Validation
  // ========================================
  if (page_index >= index.NumPages()) {
    return Error("Page index " + std::to_string(page_index) +
                 " out of range (file has " + std::to_string(index.NumPages()) +
                 " pages)");
  }

  if (!EnsurePageLoaded(index, page_index)) {
    return Error("Failed to load page " + std::to_string(page_index) +
                 " metadata");
  }

  const auto &page = index.Page(page_index);
  if (page.storage != Storage::kStrips) {
    return Error("Page " + std::to_string(page_index) +
                 " is not striped (cannot use ReadStripedPage)");
  }

  // Check compression support
  if (!IsCompression(page.compression, Compression::kJpeg) &&
      !IsCompression(page.compression, Compression::kLzw) &&
      !IsCompression(page.compression, Compression::kNone) &&
      !IsCompression(page.compression, Compression::kZstd)) {
    return Error(
        UnsupportedFormatError::Compression(page.compression, page_index)
            .what());
  }

  // Validate bits per sample
  const uint32_t bytes_per_sample = ComputeBytesPerSample(page.bits_per_sample);
  if (bytes_per_sample == 0) {
    return Error(
        UnsupportedFormatError::BitsPerSample(page.bits_per_sample, page_index)
            .what());
  }

  const uint32_t bytes_per_pixel = bytes_per_sample * page.samples_per_pixel;
  if (bytes_per_pixel == 0) {
    return Error(InvalidPageError::Parameters(
                     page_index, page.samples_per_pixel, bytes_per_sample)
                     .what());
  }

  // ========================================
  // 2. Compute ROI parameters
  // ========================================
  StripRoiParams params;
  if (!ComputeStripRoiParams(page, roi, bytes_per_pixel, params)) {
    return Error("ROI is empty or completely outside image bounds");
  }

  // ========================================
  // 3. Strip iteration and ROI extraction
  // ========================================
  const auto &strips = index.Strips(page.payload_id);
  auto offsets = index.Offsets(strips.offsets);

  uint32_t y_offset = 0;
  uint32_t total_rows_written = 0;
  std::vector<uint8_t> strip_data;

  for (size_t strip_idx = 0; strip_idx < offsets.size(); ++strip_idx) {
    // Read and decompress strip (includes predictor application)
    AIFOCORE_RETURN_IF_ERROR(ReadStripe(
        index, page_index, static_cast<uint32_t>(strip_idx), ctx, strip_data));

    // Calculate strip height from decompressed data
    const uint32_t strip_height =
        static_cast<uint32_t>(strip_data.size() / params.full_row_bytes);

    // Copy relevant rows to destination buffer
    const uint32_t rows_written = CopyStripRowsToRoi(
        strip_data, strip_height, y_offset, params, dst, dst_stride);

    total_rows_written += rows_written;

    // Early exit if we've filled the ROI
    if (total_rows_written >= params.roi_h) {
      return Result<void>();
    }

    // Advance to next strip
    y_offset += strip_height;
    if (y_offset >= page.height) {
      break;
    }
  }

  if (total_rows_written != params.roi_h) {
    return Error("Incomplete ROI read: expected " +
                 std::to_string(params.roi_h) + " rows, got " +
                 std::to_string(total_rows_written));
  }
  return Result<void>();
}

Result<void> ReadSingleJpegPage(const TiffIndex &index, uint32_t page_index,
                                DecodeContext &ctx, std::vector<uint8_t> &dst,
                                int &dst_stride, int &out_width,
                                int &out_height) {
  if (page_index >= index.NumPages()) {
    return Error("Page index " + std::to_string(page_index) +
                 " out of range (file has " + std::to_string(index.NumPages()) +
                 " pages)");
  }

  const auto &page = index.Page(page_index);
  if (page.storage != Storage::kSingleJpeg) {
    return Error("Page " + std::to_string(page_index) +
                 " is not single-JPEG storage");
  }

  const auto &single = index.SingleJpeg(page.payload_id);

  // Read JPEG data using pread
  auto jpeg_data_span =
      ReadBytesSpan(index.Fd(), index.FileSize(), single.offset, single.length,
                    ctx.temp_buffer);
  if (jpeg_data_span.empty()) {
    return Error("Failed to read JPEG data from file");
  }

  // Decode
  if (!DecodeJpeg(ctx, jpeg_data_span, out_width, out_height, dst)) {
    return Error("JPEG decoding failed for page " + std::to_string(page_index));
  }

  dst_stride = out_width * 3;
  return Result<void>();
}

Result<void> ReadPage(const TiffIndex &index, uint32_t page_index,
                      const Roi &roi, DecodeContext &ctx, uint8_t *dst,
                      int dst_stride) {
  if (page_index >= index.NumPages()) {
    return Error("Page index " + std::to_string(page_index) +
                 " out of range (file has " + std::to_string(index.NumPages()) +
                 " pages)");
  }

  const auto &page = index.Page(page_index);

  switch (page.storage) {
  case Storage::kTiles:
    return ReadTiledPage(index, page_index, roi, ctx, dst, dst_stride);

  case Storage::kStrips:
    return ReadStripedPage(index, page_index, roi, ctx, dst, dst_stride);

  case Storage::kSingleJpeg: {
    // Single JPEG storage: decode entire image then crop to ROI
    // Note: This is less efficient than tiled/striped formats since we must
    // decode the entire JPEG before cropping. Consider converting large
    // single-JPEG TIFFs to tiled format for better ROI performance.
    std::vector<uint8_t> temp_dst;
    int temp_stride = 0;
    int w = 0, h = 0;
    AIFOCORE_RETURN_IF_ERROR(ReadSingleJpegPage(index, page_index, ctx,
                                                temp_dst, temp_stride, w, h));

    // Clamp ROI to image bounds
    const uint32_t x0 = std::min(roi.x, static_cast<uint32_t>(w));
    const uint32_t y0 = std::min(roi.y, static_cast<uint32_t>(h));
    const uint32_t x1 = std::min(roi.x + roi.width, static_cast<uint32_t>(w));
    const uint32_t y1 = std::min(roi.y + roi.height, static_cast<uint32_t>(h));

    // Validate clamped ROI
    if (x0 >= x1 || y0 >= y1) {
      return Error("ROI is completely outside image bounds");
    }

    const uint32_t roi_w = x1 - x0;
    const uint32_t roi_h = y1 - y0;
    const int bytes_per_pixel = 3; // JPEG output is always RGB

    // Copy ROI rows from decoded image to destination
    for (uint32_t y = 0; y < roi_h; ++y) {
      const uint8_t *src_row =
          temp_dst.data() + (y0 + y) * temp_stride + x0 * bytes_per_pixel;
      uint8_t *dst_row = dst + y * dst_stride;
      std::memcpy(dst_row, src_row, roi_w * bytes_per_pixel);
    }
    return Result<void>();
  }

  default:
    return Error("Unknown storage type for page " + std::to_string(page_index));
  }
}

} // namespace simpletiff
