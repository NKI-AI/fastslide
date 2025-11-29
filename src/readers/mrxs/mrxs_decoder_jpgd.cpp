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

#include "fastslide/readers/mrxs/mrxs_decoder.h"

#include <jpeg-compressor/jpgd.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "lodepng/lodepng.h"

namespace fastslide::mrxs::internal {

/// @brief Decode compressed image data based on the specified format
///
/// Routes the compressed data to the appropriate decoder based on format type.
/// Supports JPEG, PNG, and BMP formats commonly used in MRXS slides.
///
/// @param data Compressed image data
/// @param format Image format (JPEG/PNG/BMP)
/// @return StatusOr containing decoded RGB image or error
/// @retval aifocore::Status::InvalidArgumentError if format is unknown or
/// unsupported
aifocore::Result<RGBImage> DecodeImage(const std::vector<uint8_t>& data,
                                       MrxsImageFormat format) {
  switch (format) {
    case MrxsImageFormat::kJpeg:
      return DecodeJpeg(data);
    case MrxsImageFormat::kPng:
      return DecodePng(data);
    case MrxsImageFormat::kBmp:
      return DecodeBmp(data);
    default:
      return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                              "Unknown or unsupported image format");
  }
}

/// @brief Decode JPEG-compressed image data using jpgd
///
/// Decompresses JPEG data and converts to RGB format using the jpgd library.
/// Handles standard JPEG tiles as used in MRXS slides. The decoder uses
/// jpgd's optimized Highway SIMD paths for IDCT and color conversion.
///
/// @param data JPEG-compressed data
/// @return StatusOr containing decoded RGB image or error
/// @retval aifocore::Status::InvalidArgumentError if data is empty
/// @retval aifocore::Status::InternalError if JPEG decompression fails
aifocore::Result<RGBImage> DecodeJpeg(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Empty JPEG data");
  }

  // jpgd will allocate and decompress the image
  int actual_comps = 0;
  int width = 0;
  int height = 0;

  // For MRXS files, the JPEG data is RGB but marked as YCbCr
  // Use cFlagNoYCbCrConversion to skip YCbCr->RGB conversion
  constexpr uint32_t flags = jpgd::jpeg_decoder::cFlagNoYCbCrConversion;

  // Request 3 components (RGB)
  unsigned char* decoded = jpgd::decompress_jpeg_image_from_memory(
      data.data(), static_cast<int>(data.size()), &width, &height,
      &actual_comps, 3, flags);

  if (!decoded) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "jpgd JPEG decompression failed");
  }

  const uint32_t u_width = static_cast<uint32_t>(width);
  const uint32_t u_height = static_cast<uint32_t>(height);

  // Create RGB image
  RGBImage result(ImageDimensions{u_width, u_height}, ImageFormat::kRGB,
                  DataType::kUInt8);

  // Copy data from jpgd's buffer to our image
  const size_t num_pixels =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  std::memcpy(result.GetData(), decoded, num_pixels * 3);

  // Free jpgd's allocated memory (using standard free since jpgd uses malloc)
  free(decoded);

  return result;
}

/// @brief Decode PNG-compressed image data using lodepng
///
/// Decompresses PNG data and converts to RGB format. Uses the lodepng library
/// for decoding. Forces 8-bit RGB output suitable for OpenSlide compatibility.
///
/// @param data PNG-compressed data
/// @return StatusOr containing decoded RGB image or error
/// @retval aifocore::Status::InvalidArgumentError if data is empty
/// @retval aifocore::Status::InternalError if PNG decompression fails
aifocore::Result<RGBImage> DecodePng(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Empty PNG data");
  }

  std::vector<unsigned char> image;
  unsigned int width, height;

  // Decode PNG to RGB
  unsigned int error = lodepng::decode(image, width, height, data, LCT_RGB, 8);

  if (error) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("PNG decode error {}: {}", error,
                              lodepng_error_text(error)));
  }

  // Create RGB image
  RGBImage result(ImageDimensions{static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height)},
                  ImageFormat::kRGB, DataType::kUInt8);

  // Copy data
  std::memcpy(result.GetData(), image.data(), image.size());

  return result;
}

/// @brief Decode BMP image data
///
/// Decodes uncompressed 24-bit BMP images. Handles both top-down and bottom-up
/// BMP formats and converts BGR pixel order to RGB. Only supports uncompressed
/// 24-bit BMPs as commonly used in MRXS slides.
///
/// @param data BMP image data
/// @return StatusOr containing decoded RGB image or error
/// @retval aifocore::Status::InvalidArgumentError if data is too small or
/// invalid
/// @retval aifocore::Status::UnimplementedError if BMP is not 24-bit
/// uncompressed
aifocore::Result<RGBImage> DecodeBmp(const std::vector<uint8_t>& data) {
  // Simplified BMP decoder for 24-bit uncompressed BMP
  if (data.size() < 54) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "BMP data too small");
  }

  // Check BMP signature
  if (data[0] != 'B' || data[1] != 'M') {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Invalid BMP signature");
  }

  // Read header
  const int32_t data_offset = *reinterpret_cast<const int32_t*>(&data[10]);
  const int32_t width = *reinterpret_cast<const int32_t*>(&data[18]);
  const int32_t height_raw = *reinterpret_cast<const int32_t*>(&data[22]);
  const int16_t bits_per_pixel = *reinterpret_cast<const int16_t*>(&data[28]);

  // Only support 24-bit BMP
  if (bits_per_pixel != 24) {
    return aifocore::Status(
        aifocore::StatusCode::kUnimplemented,
        aifocore::fmt::format("Only 24-bit BMP supported, got %d-bit",
                              bits_per_pixel));
  }

  const int32_t height = std::abs(height_raw);
  const bool top_down = height_raw < 0;

  // Calculate row stride (rows are padded to 4-byte boundaries)
  const uint32_t row_stride_src = ((width * 3) + 3) & ~3;

  if (data_offset + row_stride_src * height >
      static_cast<int32_t>(data.size())) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "BMP data truncated");
  }

  // Create RGB image
  RGBImage result(ImageDimensions{static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height)},
                  ImageFormat::kRGB, DataType::kUInt8);

  uint8_t* result_data = result.GetData();

  // BMP stores pixels as BGR, we need RGB
  for (int32_t y = 0; y < height; ++y) {
    const int32_t src_y = top_down ? y : (height - 1 - y);
    const uint8_t* src_row = &data[data_offset + src_y * row_stride_src];
    uint8_t* dst_row = &result_data[y * width * 3];

    for (int32_t x = 0; x < width; ++x) {
      // Convert BGR to RGB
      dst_row[x * 3 + 0] = src_row[x * 3 + 2];  // R
      dst_row[x * 3 + 1] = src_row[x * 3 + 1];  // G
      dst_row[x * 3 + 2] = src_row[x * 3 + 0];  // B
    }
  }

  return result;
}

}  // namespace fastslide::mrxs::internal
