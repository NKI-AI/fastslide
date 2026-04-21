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

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "simpletiff/ccitt.h"
#include "simpletiff/decompression.h"
#include "simpletiff/deflate.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/tiff_constants.h"
#include "simpletiff/tiff_parser.h"

namespace simpletiff {

using aifocore::Result;

// =============================================================================
// Internal JPEG helpers
// =============================================================================

namespace {

// Message builders that mirror the previous simpletiff::*Error factory
// methods. Kept as plain string builders so error returns stay routed through
// AIFOCORE_MAKE_STATUS (which captures file/line/function trace).
inline std::string UnsupportedCompressionMsg(uint16_t compression_code,
                                             uint32_t page_index) {
  return "Unsupported compression scheme " + std::to_string(compression_code) +
         " on page " + std::to_string(page_index);
}

inline std::string UnsupportedBitsPerSampleMsg(uint16_t bits_per_sample,
                                               uint32_t page_index) {
  return "Unsupported bits_per_sample=" + std::to_string(bits_per_sample) +
         " on page " + std::to_string(page_index) +
         ". SimpleTIFF requires byte-aligned formats (8, 16, or 32 bits)";
}

inline std::string DecompressionFailedMsg(std::string_view codec_name,
                                          uint32_t page_index,
                                          uint32_t tile_or_strip_index,
                                          std::string_view inner_cause = {}) {
  std::string msg(codec_name);
  msg += " decompression failed for page ";
  msg += std::to_string(page_index);
  msg += " tile/strip ";
  msg += std::to_string(tile_or_strip_index);
  if (!inner_cause.empty()) {
    msg += ": ";
    msg.append(inner_cause);
  }
  return msg;
}

inline std::string InvalidPageParametersMsg(uint32_t page_index,
                                            uint32_t samples_per_pixel,
                                            uint32_t bytes_per_sample) {
  return "Invalid image parameters for page " + std::to_string(page_index) +
         " (samples_per_pixel=" + std::to_string(samples_per_pixel) +
         ", bytes_per_sample=" + std::to_string(bytes_per_sample) + ")";
}

Result<void> NormalizeDecodedPixels(const TiffIndex& index,
                                    const PageHeader& page, int width,
                                    int height, std::vector<uint8_t>& data) {
  const bool file_big_endian = !index.IsLittleEndian();

  // Convert multi-byte samples to host endianness for consumers (Python creates
  // native-endian uint16/uint32 arrays from these bytes).
  if (file_big_endian &&
      (page.bits_per_sample == 16 || page.bits_per_sample == 32)) {
    if (page.bits_per_sample == 16) {
      if ((data.size() % 2) != 0) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            "Decoded buffer size is not a multiple of 2 bytes");
      }
      for (size_t i = 0; i < data.size(); i += 2) {
        std::swap(data[i], data[i + 1]);
      }
    } else {  // 32-bit
      if ((data.size() % 4) != 0) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInternal,
            "Decoded buffer size is not a multiple of 4 bytes");
      }
      for (size_t i = 0; i < data.size(); i += 4) {
        std::swap(data[i + 0], data[i + 3]);
        std::swap(data[i + 1], data[i + 2]);
      }
    }
  }

  // Apply TIFF horizontal predictor (Predictor=2) after decompression.
  // We operate on host-endian values, so file_big_endian=false here.
  if (page.predictor == 2) {
    constexpr int planar_configuration = 1;  // CONTIG
    AIFOCORE_RETURN_IF_ERROR(ApplyHorizontalPredictor(
        data, width, height, page.samples_per_pixel, page.bits_per_sample,
        /*file_big_endian=*/false, planar_configuration));
  }

  return Result<void>();
}

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
                                      std::vector<uint8_t>& jpeg_stream_buffer,
                                      DecodeContext& ctx, int& out_width,
                                      int& out_height,
                                      std::vector<uint8_t>& dst,
                                      const JpegDecodeOptions& jpeg_options) {
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
  if (!DecodeJpeg(ctx, jpeg_data_span, out_width, out_height, dst,
                  jpeg_options)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "JPEG decompression failed");
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
Result<std::span<const uint8_t>> LoadJpegTablesFromCache(
    const TiffIndex& index, const CacheRec& cache_rec) {
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
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "Failed to load JPEG tables from file");
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
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to load JPEG tables from file");
  }

  return std::span<const uint8_t>();
}

/// Decompress a CCITT bilevel tile/strip into 8-bit grayscale bytes.
///
/// Selects the right libtiff-derived codec (G3 or G4) based on the page's
/// compression code, then expands the packed 1-bit output to 8-bit grayscale
/// honoring the page's PhotometricInterpretation. The resulting buffer has
/// `width * height * 1` bytes and matches the post-decode layout the rest of
/// the simpletiff pipeline expects (because BuildPageFromContext already
/// promoted bits_per_sample from 1 to 8 for CCITT pages).
///
/// @param page Page header (provides photometric, fill_order, compression)
/// @param compressed Compressed CCITT bytes for one tile or strip
/// @param width Decoded tile/strip width in pixels
/// @param height Decoded tile/strip height in pixels
/// @param dst Output buffer (will be sized to width * height bytes)
/// @return Ok status on success; an error status on decoder failure or when
///         the page's compression code is not a CCITT bilevel codec.
Result<void> DecompressCcittToGray(const PageHeader& page,
                                   std::span<const uint8_t> compressed,
                                   uint32_t width, uint32_t height,
                                   std::vector<uint8_t>& dst) {
  const FillOrder fill_order =
      (page.fill_order == 2) ? FillOrder::kLsb2Msb : FillOrder::kMsb2Lsb;
  std::vector<uint8_t> packed;
  if (page.compression == static_cast<uint16_t>(Compression::kCcittFax4)) {
    AIFOCORE_RETURN_IF_ERROR(
        DecompressCcittG4(compressed, width, height, fill_order, packed));
  } else if (page.compression ==
                 static_cast<uint16_t>(Compression::kCcittFax3) ||
             page.compression ==
                 static_cast<uint16_t>(Compression::kCcittRle)) {
    AIFOCORE_RETURN_IF_ERROR(
        DecompressCcittG3(compressed, width, height, fill_order, packed));
  } else {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "DecompressCcittToGray: page compression " +
                                    std::to_string(page.compression) +
                                    " is not a CCITT bilevel codec");
  }
  AIFOCORE_RETURN_IF_ERROR(
      UnpackOneBitToGray(packed, width, height, page.photometric, dst));
  return Result<void>();
}

}  // namespace

// =============================================================================
// Public API
// =============================================================================

Result<void> ReadTile(const TiffIndex& index, uint32_t page_index,
                      uint32_t tile_index, DecodeContext& ctx,
                      std::vector<uint8_t>& dst, int& out_width,
                      int& out_height) {
  if (page_index >= index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page index " + std::to_string(page_index) +
                                    " out of range (file has " +
                                    std::to_string(index.NumPages()) +
                                    " pages)");
  }

  const auto& page = index.Page(page_index);
  if (page.storage != Storage::kTiles) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Page " + std::to_string(page_index) +
                                    " is not tiled (cannot use ReadTile)");
  }

  const auto& tiles = index.Tiles(page.payload_id);

  // Use optimized single-tile loader (avoids loading entire offset/bytecount
  // arrays)
  uint64_t offset = 0;
  uint64_t bytecount = 0;
  if (!EnsureTileLoaded(index, page_index, tile_index, offset, bytecount)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to load tile " +
                                    std::to_string(tile_index) + " on page " +
                                    std::to_string(page_index));
  }

  // Read tile data using pread
  auto tile_data_span = ReadBytesSpan(index.Fd(), index.FileSize(), offset,
                                      bytecount, ctx.temp_buffer,
                                      /*strict=*/false);
  if (tile_data_span.empty()) {
    // If bytecount was 0 (sparse tile) or we couldn't read any data (truncated
    // file / offset >= EOF) in lenient mode, we return a specific error
    // so the caller can decide whether to treat it as a blank tile.
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kDataLoss,
                                "Tile data empty or missing");
  }

  if (IsCcittCompression(page.compression)) {
    out_width = static_cast<int>(tiles.tile_w);
    out_height = static_cast<int>(tiles.tile_h);
    if (auto s = DecompressCcittToGray(page, tile_data_span,
                                       static_cast<uint32_t>(out_width),
                                       static_cast<uint32_t>(out_height), dst);
        !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("CCITT", page_index, tile_index,
                                 s.status().message()));
    }
    AIFOCORE_RETURN_IF_ERROR(
        NormalizeDecodedPixels(index, page, out_width, out_height, dst));
    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kZstd)) {
    // ZSTD compression
    if (auto s = DecompressZstd(tile_data_span, dst); !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("ZSTD", page_index, tile_index,
                                 s.status().message()));
    }

    out_width = static_cast<int>(tiles.tile_w);
    out_height = static_cast<int>(tiles.tile_h);

    AIFOCORE_RETURN_IF_ERROR(
        NormalizeDecodedPixels(index, page, out_width, out_height, dst));
    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kLzw)) {
    // LZW compression
    if (auto s = DecompressLzw(tile_data_span, dst); !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("LZW", page_index, tile_index,
                                 s.status().message()));
    }

    out_width = static_cast<int>(tiles.tile_w);
    out_height = static_cast<int>(tiles.tile_h);

    AIFOCORE_RETURN_IF_ERROR(
        NormalizeDecodedPixels(index, page, out_width, out_height, dst));
    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kAdobeDeflate) ||
             IsCompression(page.compression, Compression::kDeflate)) {
    if (auto s = DecompressDeflate(tile_data_span, dst); !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("Deflate", page_index, tile_index,
                                 s.status().message()));
    }

    out_width = static_cast<int>(tiles.tile_w);
    out_height = static_cast<int>(tiles.tile_h);
    AIFOCORE_RETURN_IF_ERROR(
        NormalizeDecodedPixels(index, page, out_width, out_height, dst));
    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kJpeg)) {
    // JPEG compression - load tables from cache and decompress
    AIFOCORE_ASSIGN_OR_RETURN(auto jpeg_tables_span,
                              LoadJpegTablesFromCache(index, tiles));

    const JpegDecodeOptions jpeg_options = {
        .treat_ycbcr_as_rgb =
            !IsPhotometric(page.photometric, Photometric::kYCbCr),
    };

    // Decompress JPEG using common helper with explicit context
    auto decompress_result = DecompressJpegWithTables(
        tile_data_span, jpeg_tables_span, ctx.jpeg_stream_buffer, ctx,
        out_width, out_height, dst, jpeg_options);
    if (!decompress_result) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "JPEG decompression failed for tile " +
                                      std::to_string(tile_index) + " on page " +
                                      std::to_string(page_index) + ": " +
                                      decompress_result.error().message());
    }

    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kJpeg2000)) {
    const bool file_big_endian = !index.IsLittleEndian();
    const bool convert_ycbcr_to_rgb = IsJpeg2000YCbCr(page.compression);
    AIFOCORE_RETURN_IF_ERROR(
        DecodeJpeg2000(tile_data_span, file_big_endian, page.bits_per_sample,
                       page.samples_per_pixel, convert_ycbcr_to_rgb, out_width,
                       out_height, dst));
    return Result<void>();
  } else {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        UnsupportedCompressionMsg(page.compression, page_index));
  }
}

Result<void> ReadRawTile(const TiffIndex& index, uint32_t page_index,
                         uint32_t tile_index, std::vector<uint8_t>& dst) {
  if (page_index >= index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page index " + std::to_string(page_index) +
                                    " out of range (file has " +
                                    std::to_string(index.NumPages()) +
                                    " pages)");
  }

  const auto& page = index.Page(page_index);

  // Support both tiled and strip storage for raw reading
  uint64_t offset = 0;
  uint64_t bytecount = 0;

  if (page.storage == Storage::kTiles) {
    // Use optimized single-tile loader
    if (!EnsureTileLoaded(index, page_index, tile_index, offset, bytecount)) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "Failed to load tile " +
                                      std::to_string(tile_index) + " on page " +
                                      std::to_string(page_index));
    }
  } else if (page.storage == Storage::kStrips) {
    // Use optimized single-strip loader
    if (!EnsureTileLoaded(index, page_index, tile_index, offset, bytecount)) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "Failed to load strip " +
                                      std::to_string(tile_index) + " on page " +
                                      std::to_string(page_index));
    }
  } else {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Page " + std::to_string(page_index) +
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
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "Failed to read raw tile/strip data from file at offset " +
            std::to_string(offset));
  }

  return Result<void>();
}

Result<void> ReadTiledPage(const TiffIndex& index, uint32_t page_index,
                           const Roi& roi, DecodeContext& ctx, uint8_t* dst,
                           int dst_stride) {
  if (page_index >= index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page index " + std::to_string(page_index) +
                                    " out of range (file has " +
                                    std::to_string(index.NumPages()) +
                                    " pages)");
  }

  // Ensure page data is loaded (lazy load if needed)
  if (!EnsurePageLoaded(index, page_index)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "Failed to load page " + std::to_string(page_index) + " metadata");
  }

  const auto& page = index.Page(page_index);
  if (page.storage != Storage::kTiles) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Page " + std::to_string(page_index) +
                                    " is not tiled (cannot use ReadTiledPage)");
  }

  // Check if compression is supported
  if (!IsCompression(page.compression, Compression::kJpeg) &&
      !IsCompression(page.compression, Compression::kJpeg2000) &&
      !IsCompression(page.compression, Compression::kZstd) &&
      !IsCcittCompression(page.compression)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        UnsupportedCompressionMsg(page.compression, page_index));
  }

  const auto& tiles = index.Tiles(page.payload_id);

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

Result<void> ReadStripe(const TiffIndex& index, uint32_t page_index,
                        uint32_t strip_index, DecodeContext& ctx,
                        std::vector<uint8_t>& decompressed) {
  if (page_index >= index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page index " + std::to_string(page_index) +
                                    " out of range (file has " +
                                    std::to_string(index.NumPages()) +
                                    " pages)");
  }

  // Ensure page data is loaded (lazy load if needed)
  if (!EnsurePageLoaded(index, page_index)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "Failed to load page " + std::to_string(page_index) + " metadata");
  }

  const auto& page = index.Page(page_index);
  if (page.storage != Storage::kStrips) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Page " + std::to_string(page_index) +
                                    " is not striped (cannot use ReadStripe)");
  }

  const auto& strips = index.Strips(page.payload_id);
  auto offsets = index.Offsets(strips.offsets);
  auto bytecounts = index.Bytecounts(strips.bytecounts);

  if (strip_index >= offsets.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        "Strip index " + std::to_string(strip_index) +
            " out of range on page " + std::to_string(page_index) + " (has " +
            std::to_string(offsets.size()) + " strips)");
  }

  // Read strip data using pread
  auto strip_data_span =
      ReadBytesSpan(index.Fd(), index.FileSize(), offsets[strip_index],
                    bytecounts[strip_index], ctx.temp_buffer, /*strict=*/false);
  if (strip_data_span.empty()) {
    // If strip is empty or missing, return specific error for caller handling
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kDataLoss,
                                "Strip data empty or missing");
  }

  if (IsCcittCompression(page.compression)) {
    // CCITT bilevel (T.4 / T.6) — decode 1-bit and unpack to 8-bit gray.
    const uint32_t rows_per_strip =
        strips.rows_per_strip > 0 ? strips.rows_per_strip : page.height;
    const uint64_t y_offset =
        static_cast<uint64_t>(strip_index) * rows_per_strip;
    if (y_offset >= page.height) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                  "Strip " + std::to_string(strip_index) +
                                      " starts past page height on page " +
                                      std::to_string(page_index));
    }
    const uint32_t strip_h_pixels = static_cast<uint32_t>(
        std::min<uint64_t>(rows_per_strip, page.height - y_offset));
    if (auto s = DecompressCcittToGray(page, strip_data_span, page.width,
                                       strip_h_pixels, decompressed);
        !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("CCITT", page_index, strip_index,
                                 s.status().message()));
    }
    AIFOCORE_RETURN_IF_ERROR(
        NormalizeDecodedPixels(index, page, static_cast<int>(page.width),
                               static_cast<int>(strip_h_pixels), decompressed));
    return Result<void>();
  } else if (IsCompression(page.compression, Compression::kJpeg)) {
    // JPEG compression - load tables from cache and decompress
    AIFOCORE_ASSIGN_OR_RETURN(auto jpeg_tables_span,
                              LoadJpegTablesFromCache(index, strips));

    const JpegDecodeOptions jpeg_options = {
        .treat_ycbcr_as_rgb =
            !IsPhotometric(page.photometric, Photometric::kYCbCr),
    };

    // Decompress JPEG using common helper with explicit context
    int strip_w = 0, strip_h = 0;
    auto decompress_result = DecompressJpegWithTables(
        strip_data_span, jpeg_tables_span, ctx.jpeg_stream_buffer, ctx, strip_w,
        strip_h, decompressed, jpeg_options);
    if (!decompress_result) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("JPEG", page_index, strip_index));
    }
  } else if (IsCompression(page.compression, Compression::kJpeg2000)) {
    const bool file_big_endian = !index.IsLittleEndian();
    const bool convert_ycbcr_to_rgb = IsJpeg2000YCbCr(page.compression);
    int strip_w = 0;
    int strip_h = 0;
    auto result =
        DecodeJpeg2000(strip_data_span, file_big_endian, page.bits_per_sample,
                       page.samples_per_pixel, convert_ycbcr_to_rgb, strip_w,
                       strip_h, decompressed);
    if (!result) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("JPEG2000", page_index, strip_index));
    }
  } else if (IsCompression(page.compression, Compression::kLzw)) {
    // LZW compression
    if (auto s = DecompressLzw(strip_data_span, decompressed); !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("LZW", page_index, strip_index,
                                 s.status().message()));
    }
  } else if (IsCompression(page.compression, Compression::kZstd)) {
    // ZSTD compression
    if (auto s = DecompressZstd(strip_data_span, decompressed); !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("ZSTD", page_index, strip_index,
                                 s.status().message()));
    }
  } else if (IsCompression(page.compression, Compression::kAdobeDeflate) ||
             IsCompression(page.compression, Compression::kDeflate)) {
    if (auto s = DecompressDeflate(strip_data_span, decompressed); !s.ok()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          DecompressionFailedMsg("Deflate", page_index, strip_index,
                                 s.status().message()));
    }
  } else if (IsCompression(page.compression, Compression::kNone)) {
    // Uncompressed - copy data
    decompressed.assign(strip_data_span.begin(), strip_data_span.end());
  } else {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        UnsupportedCompressionMsg(page.compression, page_index));
  }

  // Normalize decoded bytes (endianness + predictor) per strip.
  const uint32_t bytes_per_sample = ComputeBytesPerSample(page.bits_per_sample);
  if (bytes_per_sample == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        UnsupportedBitsPerSampleMsg(page.bits_per_sample, page_index));
  }

  const uint32_t bytes_per_pixel = bytes_per_sample * page.samples_per_pixel;
  if (bytes_per_pixel == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        InvalidPageParametersMsg(page_index, page.samples_per_pixel,
                                 bytes_per_sample));
  }

  const uint32_t full_row_bytes = page.width * bytes_per_pixel;
  const int strip_h = static_cast<int>(decompressed.size() / full_row_bytes);
  AIFOCORE_RETURN_IF_ERROR(NormalizeDecodedPixels(
      index, page, static_cast<int>(page.width), strip_h, decompressed));

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
bool ComputeStripRoiParams(const PageHeader& page, const Roi& roi,
                           uint32_t bytes_per_pixel, StripRoiParams& params) {
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
uint32_t CopyStripRowsToRoi(const std::vector<uint8_t>& strip_data,
                            uint32_t strip_height, uint32_t y_offset,
                            const StripRoiParams& params, uint8_t* dst,
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
    const uint8_t* src_row =
        strip_data.data() + r * row_bytes + params.roi_row_offset;
    uint8_t* dst_row = dst + (global_y - params.roi_y) * dst_stride;

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

}  // namespace

// =============================================================================
// Public API
// =============================================================================

Result<void> ReadStripedPage(const TiffIndex& index, uint32_t page_index,
                             const Roi& roi, DecodeContext& ctx, uint8_t* dst,
                             int dst_stride) {
  // ========================================
  // 1. Validation
  // ========================================
  if (page_index >= index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page index " + std::to_string(page_index) +
                                    " out of range (file has " +
                                    std::to_string(index.NumPages()) +
                                    " pages)");
  }

  if (!EnsurePageLoaded(index, page_index)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "Failed to load page " + std::to_string(page_index) + " metadata");
  }

  const auto& page = index.Page(page_index);
  if (page.storage != Storage::kStrips) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Page " + std::to_string(page_index) +
            " is not striped (cannot use ReadStripedPage)");
  }

  // Check compression support
  if (!IsCompression(page.compression, Compression::kJpeg) &&
      !IsCompression(page.compression, Compression::kJpeg2000) &&
      !IsCompression(page.compression, Compression::kLzw) &&
      !IsCompression(page.compression, Compression::kNone) &&
      !IsCompression(page.compression, Compression::kZstd) &&
      !IsCcittCompression(page.compression)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        UnsupportedCompressionMsg(page.compression, page_index));
  }

  // Validate bits per sample
  const uint32_t bytes_per_sample = ComputeBytesPerSample(page.bits_per_sample);
  if (bytes_per_sample == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        UnsupportedBitsPerSampleMsg(page.bits_per_sample, page_index));
  }

  const uint32_t bytes_per_pixel = bytes_per_sample * page.samples_per_pixel;
  if (bytes_per_pixel == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        InvalidPageParametersMsg(page_index, page.samples_per_pixel,
                                 bytes_per_sample));
  }

  // ========================================
  // 2. Compute ROI parameters
  // ========================================
  StripRoiParams params;
  if (!ComputeStripRoiParams(page, roi, bytes_per_pixel, params)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "ROI is empty or completely outside image bounds");
  }

  // ========================================
  // 3. Strip iteration and ROI extraction
  // ========================================
  const auto& strips = index.Strips(page.payload_id);
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
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "Incomplete ROI read: expected " + std::to_string(params.roi_h) +
            " rows, got " + std::to_string(total_rows_written));
  }
  return Result<void>();
}

Result<void> ReadSingleJpegPage(const TiffIndex& index, uint32_t page_index,
                                DecodeContext& ctx, std::vector<uint8_t>& dst,
                                int& dst_stride, int& out_width,
                                int& out_height) {
  if (page_index >= index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page index " + std::to_string(page_index) +
                                    " out of range (file has " +
                                    std::to_string(index.NumPages()) +
                                    " pages)");
  }

  const auto& page = index.Page(page_index);
  if (page.storage != Storage::kSingleJpeg) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Page " + std::to_string(page_index) + " is not single-JPEG storage");
  }

  const auto& single = index.SingleJpeg(page.payload_id);

  // Read JPEG data using pread
  auto jpeg_data_span =
      ReadBytesSpan(index.Fd(), index.FileSize(), single.offset, single.length,
                    ctx.temp_buffer);
  if (jpeg_data_span.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to read JPEG data from file");
  }

  // Decode
  if (!DecodeJpeg(ctx, jpeg_data_span, out_width, out_height, dst)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        "JPEG decoding failed for page " + std::to_string(page_index));
  }

  dst_stride = out_width * 3;
  return Result<void>();
}

Result<void> ReadPage(const TiffIndex& index, uint32_t page_index,
                      const Roi& roi, DecodeContext& ctx, uint8_t* dst,
                      int dst_stride) {
  if (page_index >= index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page index " + std::to_string(page_index) +
                                    " out of range (file has " +
                                    std::to_string(index.NumPages()) +
                                    " pages)");
  }

  const auto& page = index.Page(page_index);

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
      const uint32_t y1 =
          std::min(roi.y + roi.height, static_cast<uint32_t>(h));

      // Validate clamped ROI
      if (x0 >= x1 || y0 >= y1) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "ROI is completely outside image bounds");
      }

      const uint32_t roi_w = x1 - x0;
      const uint32_t roi_h = y1 - y0;
      const int bytes_per_pixel = 3;  // JPEG output is always RGB

      // Copy ROI rows from decoded image to destination
      for (uint32_t y = 0; y < roi_h; ++y) {
        const uint8_t* src_row =
            temp_dst.data() + (y0 + y) * temp_stride + x0 * bytes_per_pixel;
        uint8_t* dst_row = dst + y * dst_stride;
        std::memcpy(dst_row, src_row, roi_w * bytes_per_pixel);
      }
      return Result<void>();
    }

    default:
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          "Unknown storage type for page " + std::to_string(page_index));
  }
}

}  // namespace simpletiff
