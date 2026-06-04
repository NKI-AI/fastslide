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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_DECODE_UTILS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_DECODE_UTILS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/image.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide {
namespace readers {
namespace simpletiff_decode {

/// @brief Simple rectangle with unsigned integer coordinates.
struct RectU32 {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

/// @brief View of decoded interleaved pixels backed by a caller-owned buffer.
struct DecodedInterleavedView {
  std::span<const uint8_t> data;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t channels = 0;
};

/// @brief Read exactly one TIFF tile (tiled) or strip (striped) into `buffer`.
///
/// This helper centralizes the "ReadTile vs ReadStripe + compute decoded dims"
/// logic used by multiple TIFF-based readers.
///
/// @param tiff_index TIFF index
/// @param page_index Page index to read from
/// @param tile_or_strip_index Linear tile index (tiles) or strip index (strips)
/// @param decode_ctx Per-thread decode context (scratch buffers, JPEG state)
/// @param buffer Destination buffer (will be resized by simpletiff)
/// @return View of decoded pixel data in `buffer`
inline aifocore::Result<DecodedInterleavedView> ReadTileOrStrip(
    const simpletiff::TiffIndex& tiff_index, uint32_t page_index,
    uint32_t tile_or_strip_index, simpletiff::DecodeContext& decode_ctx,
    std::vector<uint8_t>& buffer) {
  if (page_index >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Page {} out of range ({} pages)", page_index,
                              tiff_index.NumPages()));
  }

  const auto& page_header = tiff_index.Page(page_index);

  if (page_header.samples_per_pixel == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid SamplesPerPixel=0");
  }

  const size_t bytes_per_sample =
      static_cast<size_t>((page_header.bits_per_sample + 7U) / 8U);
  const size_t bytes_per_pixel =
      bytes_per_sample * static_cast<size_t>(page_header.samples_per_pixel);
  if (bytes_per_pixel == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid BytesPerPixel=0");
  }

  simpletiff::Result<void> read_result;
  int decoded_w = 0;
  int decoded_h = 0;

  if (page_header.storage == simpletiff::Storage::kTiles) {
    read_result =
        simpletiff::ReadTile(tiff_index, page_index, tile_or_strip_index,
                             decode_ctx, buffer, decoded_w, decoded_h);
  } else if (page_header.storage == simpletiff::Storage::kStrips) {
    read_result = simpletiff::ReadStripe(
        tiff_index, page_index, tile_or_strip_index, decode_ctx, buffer);

    const size_t row_bytes =
        static_cast<size_t>(page_header.width) * bytes_per_pixel;
    if (row_bytes == 0) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format("Invalid row_bytes={} for page {}", row_bytes,
                                page_index));
    }

    // Compute expected strip height from TIFF metadata. This is more robust
    // than inferring height from the decoded buffer size (which may include
    // padding).
    const auto& strips = tiff_index.Strips(page_header.payload_id);
    const uint32_t rows_per_strip = (strips.rows_per_strip == 0)
                                        ? page_header.height
                                        : strips.rows_per_strip;

    const uint64_t start_row =
        static_cast<uint64_t>(tile_or_strip_index) * rows_per_strip;
    if (start_row >= page_header.height) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "Strip index {} starts at row {} >= page height {}",
              tile_or_strip_index, start_row, page_header.height));
    }

    const uint32_t expected_rows = std::min<uint32_t>(
        rows_per_strip, static_cast<uint32_t>(page_header.height - start_row));
    const size_t expected_size = static_cast<size_t>(expected_rows) * row_bytes;
    if (expected_rows == 0 || expected_size == 0) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format(
              "Invalid expected strip size: rows={} row_bytes={}",
              expected_rows, row_bytes));
    }

    if (buffer.size() < expected_size) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format(
              "ReadStripe returned too-small buffer size={} expected_size={}",
              buffer.size(), expected_size));
    }

    // Some striped TIFFs (seen in the wild with QPTIFF) can return a few
    // trailing bytes after decompression. Treat them as padding and truncate to
    // the expected strip payload size.
    if (buffer.size() != expected_size) {
      buffer.resize(expected_size);
    }

    decoded_w = static_cast<int>(page_header.width);
    decoded_h = static_cast<int>(expected_rows);
  } else {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Unsupported storage type {} for page {}",
                              static_cast<int>(page_header.storage),
                              page_index));
  }

  if (!read_result.ok()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read tile/strip {} from page {}: {}",
                              tile_or_strip_index, page_index,
                              read_result.error().message()));
  }

  if (decoded_w <= 0 || decoded_h <= 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Decoded dimensions are invalid: {}x{}",
                              decoded_w, decoded_h));
  }

  return DecodedInterleavedView{
      .data = std::span<const uint8_t>(buffer.data(), buffer.size()),
      .width = static_cast<uint32_t>(decoded_w),
      .height = static_cast<uint32_t>(decoded_h),
      .channels = static_cast<uint32_t>(page_header.samples_per_pixel),
  };
}

/// @brief Crop an interleaved ROI from a decoded tile/strip into `dst`.
///
/// @param src Decoded pixel buffer (interleaved)
/// @param src_width Decoded width in pixels
/// @param src_height Decoded height in pixels
/// @param crop Crop rectangle in pixels
/// @param bytes_per_pixel Interleaved bytes per pixel
/// @param dst Destination buffer (must be sized to crop_width*crop_height*bpp)
/// @return View of cropped bytes in `dst`
inline aifocore::Result<std::span<const uint8_t>> CropInterleavedRoi(
    std::span<const uint8_t> src, uint32_t src_width, uint32_t src_height,
    RectU32 crop, size_t bytes_per_pixel, std::span<uint8_t> dst) {
  if (crop.width == 0 || crop.height == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Crop ROI has zero area");
  }
  if (bytes_per_pixel == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "bytes_per_pixel=0");
  }
  if (crop.x + crop.width > src_width || crop.y + crop.height > src_height) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Crop ROI ({},{},{}x{}) out of bounds ({}x{})",
                              crop.x, crop.y, crop.width, crop.height,
                              src_width, src_height));
  }

  const size_t src_row_bytes = static_cast<size_t>(src_width) * bytes_per_pixel;
  const size_t crop_row_bytes =
      static_cast<size_t>(crop.width) * bytes_per_pixel;
  const size_t crop_total_bytes =
      static_cast<size_t>(crop.height) * crop_row_bytes;

  if (dst.size() < crop_total_bytes) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Destination too small: {} < {}", dst.size(),
                              crop_total_bytes));
  }

  const size_t min_src_bytes = static_cast<size_t>(src_height) * src_row_bytes;
  if (src.size() < min_src_bytes) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Source too small: {} < {}", src.size(),
                              min_src_bytes));
  }

  for (uint32_t row = 0; row < crop.height; ++row) {
    const size_t src_offset =
        (static_cast<size_t>(crop.y + row) * src_row_bytes) +
        (static_cast<size_t>(crop.x) * bytes_per_pixel);
    const size_t dst_offset = static_cast<size_t>(row) * crop_row_bytes;

    const auto src_row = src.subspan(src_offset, crop_row_bytes);
    auto dst_row = dst.subspan(dst_offset, crop_row_bytes);
    std::copy(src_row.begin(), src_row.end(), dst_row.begin());
  }

  return std::span<const uint8_t>(dst.data(), crop_total_bytes);
}

/// @brief Decode a full TIFF page into a freshly allocated RGB(A) image.
///
/// Used for "associated images" (label/macro/thumbnail) of pyramidal TIFF
/// formats (Aperio, QPTIFF, GenericTIFF, ...). Validates the page index,
/// allocates an RGBImage sized to `size`, and dispatches to
/// `simpletiff::ReadPage`, which handles both tiled and striped pages.
///
/// @param tiff_index   TIFF index for the open file.
/// @param page         TIFF page index to decode.
/// @param size         Expected image size (width, height) in pixels; used to
///                     allocate the destination buffer and crop ROI.
/// @param image_name   Display name used in error messages (e.g. "label").
/// @return A populated `RGBImage` or an error status.
inline aifocore::Result<RGBImage> ReadAssociatedTiffPage(
    const simpletiff::TiffIndex& tiff_index, uint32_t page,
    ImageDimensions size, std::string_view image_name) {
  if (page >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Invalid page {} for associated image '{}'", page,
                              image_name));
  }

  const auto& page_header = tiff_index.Page(page);
  const uint32_t width = size[0];
  const uint32_t height = size[1];
  const uint16_t samples_per_pixel = page_header.samples_per_pixel;

  RGBImage rgb_image({width, height}, ImageFormat::kRGB, DataType::kUInt8);

  simpletiff::DecodeContext ctx;
  simpletiff::Roi roi{0, 0, width, height};
  const int stride = static_cast<int>(width) * samples_per_pixel;
  auto result = simpletiff::ReadPage(tiff_index, page, roi, ctx,
                                     rgb_image.GetData(), stride);
  if (!result) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read associated image '{}': {}",
                              image_name, result.error().message()));
  }
  return rgb_image;
}

}  // namespace simpletiff_decode
}  // namespace readers
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_SIMPLETIFF_DECODE_UTILS_H_
