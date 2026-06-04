// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

/// @file bmp_decoder.cpp
/// @brief Minimal BMP24 (BI_RGB) decoder used for 3DHISTECH MRXS tiles.
///
/// MRXS slides flagged with `IMAGE_FORMAT=BMP24` always store uncompressed
/// 24 bpp BGR rows wrapped in a Windows V3 (`BITMAPINFOHEADER`) BMP container.
/// This decoder accepts that subset only: any other bit depth, plane count or
/// compression scheme is reported as `kUnimplemented` so the caller can decide
/// whether to fail loudly.
///
/// Layout summary (all little-endian):
///
///   `BITMAPFILEHEADER` (14 B):
///     - `bfType`     ('B','M')
///     - `bfSize`         u32  (file size, ignored)
///     - reserved         u32
///     - `bfOffBits`      u32  (offset to pixel data) -> read from byte 10
///   `BITMAPINFOHEADER` (>= 40 B for V3+):
///     - `biSize`         u32  -> byte 14
///     - `biWidth`        i32  -> byte 18
///     - `biHeight`       i32  -> byte 22
///     - `biPlanes`       u16  -> byte 26
///     - `biBitCount`     u16  -> byte 28
///     - `biCompression`  u32  -> byte 30  (0 = BI_RGB)

#include "fastslide/runtime/decoders/bmp_decoder.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide::runtime::decoders {
namespace {

/// @brief Size of the `BITMAPFILEHEADER` preamble.
constexpr std::size_t kBmpFileHeaderBytes = 14;

/// @brief Smallest `BITMAPINFOHEADER` (Windows V3) we accept.
constexpr std::size_t kMinInfoHeaderBytes = 40;

/// @brief Smallest legal BMP24 prelude (`BITMAPFILEHEADER` + V3 info header).
constexpr std::size_t kBmpMinHeaderBytes =
    kBmpFileHeaderBytes + kMinInfoHeaderBytes;

constexpr std::size_t kPixelDataOffsetField = 10;
constexpr std::size_t kInfoHeaderSizeField = 14;
constexpr std::size_t kWidthField = 18;
constexpr std::size_t kHeightField = 22;
constexpr std::size_t kPlanesField = 26;
constexpr std::size_t kBitsPerPixelField = 28;
constexpr std::size_t kCompressionField = 30;

/// @brief BI_RGB: uncompressed bitmap pixel data.
constexpr uint32_t kCompressionRgb = 0;

/// @brief Read a little-endian integer of type @p T at @p offset from @p data.
///
/// Uses `memcpy` to avoid alignment UB; modern compilers fold this to a single
/// unaligned load.
template <typename T>
[[nodiscard]] T ReadLe(std::span<const uint8_t> data, std::size_t offset) {
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

/// @brief Convert one row of BGR triplets to RGB triplets in lockstep.
///
/// Channel-swap is the entire body of the inner loop, so we keep it in a
/// dedicated helper to give the compiler the clearest possible view for
/// auto-vectorization. The function is intentionally `static` and small so
/// it can be inlined without LTO. Source and destination must not overlap.
///
/// @param src   Pointer to the first BGR triplet of the source row.
/// @param dst   Pointer to the first RGB triplet of the destination row.
/// @param width Number of pixels (must match the row width).
inline void BgrRowToRgb(const uint8_t* __restrict src, uint8_t* __restrict dst,
                        uint32_t width) noexcept {
  for (uint32_t x = 0; x < width; ++x) {
    const uint8_t b = src[0];
    const uint8_t g = src[1];
    const uint8_t r = src[2];
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    src += 3;
    dst += 3;
  }
}

}  // namespace

aifocore::Result<DecodedRgb> DecodeBmpToRgb(
    std::span<const uint8_t> bmp_bytes) {
  if (bmp_bytes.size() < kBmpMinHeaderBytes) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BMP data too small");
  }

  if (bmp_bytes[0] != static_cast<uint8_t>('B') ||
      bmp_bytes[1] != static_cast<uint8_t>('M')) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid BMP signature");
  }

  const auto info_header_size =
      ReadLe<uint32_t>(bmp_bytes, kInfoHeaderSizeField);
  if (info_header_size < kMinInfoHeaderBytes ||
      kBmpFileHeaderBytes + info_header_size > bmp_bytes.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format(
            "Unsupported BMP info header size {} (need >= {})",
            info_header_size, kMinInfoHeaderBytes));
  }

  const auto data_offset = ReadLe<uint32_t>(bmp_bytes, kPixelDataOffsetField);
  const auto width = ReadLe<int32_t>(bmp_bytes, kWidthField);
  const auto height_raw = ReadLe<int32_t>(bmp_bytes, kHeightField);
  const auto planes = ReadLe<uint16_t>(bmp_bytes, kPlanesField);
  const auto bits_per_pixel = ReadLe<int16_t>(bmp_bytes, kBitsPerPixelField);
  const auto compression = ReadLe<uint32_t>(bmp_bytes, kCompressionField);

  if (planes != 1) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("BMP biPlanes must be 1, got {}", planes));
  }

  if (bits_per_pixel != 24) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("Only 24-bit BMP supported, got {}-bit",
                              bits_per_pixel));
  }

  if (compression != kCompressionRgb) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format(
            "Only uncompressed (BI_RGB) BMP supported, got compression={}",
            compression));
  }

  if (width <= 0 || height_raw == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid BMP dimensions: width={}, height={}",
                              width, height_raw));
  }

  if (data_offset < kBmpMinHeaderBytes || data_offset >= bmp_bytes.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid BMP pixel-data offset: {}",
                              data_offset));
  }

  const int32_t height = std::abs(height_raw);
  const bool top_down = height_raw < 0;

  const uint32_t row_stride_src =
      ((static_cast<uint32_t>(width) * 3U) + 3U) & ~3U;

  const std::size_t pixel_array_bytes =
      static_cast<std::size_t>(row_stride_src) *
      static_cast<std::size_t>(height);
  if (pixel_array_bytes / static_cast<std::size_t>(row_stride_src) !=
      static_cast<std::size_t>(height)) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BMP pixel array size overflowed");
  }

  const std::size_t needed =
      static_cast<std::size_t>(data_offset) + pixel_array_bytes;
  if (needed < pixel_array_bytes || needed > bmp_bytes.size()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BMP data truncated");
  }

  DecodedRgb out;
  out.width = static_cast<uint32_t>(width);
  out.height = static_cast<uint32_t>(height);
  out.rgb.resize(static_cast<std::size_t>(width) *
                 static_cast<std::size_t>(height) * 3U);

  // Walk source rows top-to-bottom in destination order. For bottom-up BMPs
  // we step the source pointer backwards by one row each iteration; for
  // top-down BMPs we step forwards. Hoisting the direction out of the inner
  // loop avoids per-pixel branches and keeps the row body tight enough for
  // the compiler to auto-vectorize the BGR<->RGB swap.
  const std::ptrdiff_t row_step =
      top_down ? static_cast<std::ptrdiff_t>(row_stride_src)
               : -static_cast<std::ptrdiff_t>(row_stride_src);
  const uint8_t* src_row =
      bmp_bytes.data() + data_offset +
      (top_down ? 0U : static_cast<std::size_t>(height - 1) * row_stride_src);

  const std::size_t dst_row_bytes = static_cast<std::size_t>(width) * 3U;
  uint8_t* dst_row = out.rgb.data();

  for (int32_t y = 0; y < height; ++y) {
    BgrRowToRgb(src_row, dst_row, static_cast<uint32_t>(width));
    src_row += row_step;
    dst_row += dst_row_bytes;
  }

  return out;
}

}  // namespace fastslide::runtime::decoders
