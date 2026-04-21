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

#include "fastslide/runtime/decoders/bmp_decoder.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide::runtime::decoders {
namespace {

/// @brief Smallest BMP header we support (BITMAPFILEHEADER + BITMAPINFOHEADER).
constexpr std::size_t kBmpMinHeaderBytes = 54;
constexpr std::size_t kPixelDataOffsetField = 10;
constexpr std::size_t kWidthField = 18;
constexpr std::size_t kHeightField = 22;
constexpr std::size_t kBitsPerPixelField = 28;

/// @brief Read a little-endian integer of type T at `offset` from `data`.
template <typename T>
T ReadLe(std::span<const uint8_t> data, std::size_t offset) {
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
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

  const auto data_offset = ReadLe<int32_t>(bmp_bytes, kPixelDataOffsetField);
  const auto width = ReadLe<int32_t>(bmp_bytes, kWidthField);
  const auto height_raw = ReadLe<int32_t>(bmp_bytes, kHeightField);
  const auto bits_per_pixel = ReadLe<int16_t>(bmp_bytes, kBitsPerPixelField);

  if (bits_per_pixel != 24) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("Only 24-bit BMP supported, got {}-bit",
                              bits_per_pixel));
  }

  if (width <= 0 || height_raw == 0 || data_offset < 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid BMP dimensions: width={}, height={}, "
                              "data_offset={}",
                              width, height_raw, data_offset));
  }

  const int32_t height = std::abs(height_raw);
  const bool top_down = height_raw < 0;

  // Rows are padded to 4-byte boundaries.
  const uint32_t row_stride_src =
      ((static_cast<uint32_t>(width) * 3U) + 3U) & ~3U;

  const std::size_t needed = static_cast<std::size_t>(data_offset) +
                             static_cast<std::size_t>(row_stride_src) *
                                 static_cast<std::size_t>(height);
  if (needed > bmp_bytes.size()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BMP data truncated");
  }

  DecodedRgb out;
  out.width = static_cast<uint32_t>(width);
  out.height = static_cast<uint32_t>(height);
  out.rgb.resize(static_cast<std::size_t>(width) *
                 static_cast<std::size_t>(height) * 3U);

  uint8_t* dst = out.rgb.data();
  for (int32_t y = 0; y < height; ++y) {
    const int32_t src_y = top_down ? y : (height - 1 - y);
    const uint8_t* src_row =
        &bmp_bytes[static_cast<std::size_t>(data_offset) +
                   static_cast<std::size_t>(src_y) * row_stride_src];
    uint8_t* dst_row = dst + static_cast<std::size_t>(y) *
                                 static_cast<std::size_t>(width) * 3U;
    for (int32_t x = 0; x < width; ++x) {
      // BMP stores BGR, we want RGB.
      dst_row[x * 3 + 0] = src_row[x * 3 + 2];
      dst_row[x * 3 + 1] = src_row[x * 3 + 1];
      dst_row[x * 3 + 2] = src_row[x * 3 + 0];
    }
  }

  return out;
}

}  // namespace fastslide::runtime::decoders
