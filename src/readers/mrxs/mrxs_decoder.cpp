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

#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstring>
#include <string>
#include <vector>

#include <jpeglib.h>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "lodepng/lodepng.h"

namespace fastslide::mrxs::internal {

namespace {  // ---- JPEG: thread-local reusable decompressor ----

struct ThreadLocalJpeg {
  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr jerr{};
  std::jmp_buf jump_buffer{};
  char error_message[JMSG_LENGTH_MAX]{};
  bool inited{false};

  ~ThreadLocalJpeg() {
    if (inited)
      jpeg_destroy_decompress(&cinfo);
  }

  /// @brief Custom error exit handler that longjmps instead of calling exit()
  ///
  /// This replaces libjpeg's default error handler which terminates the
  /// process. Instead, we capture the error message and jump back to the
  /// setjmp point in the calling code.
  static void ErrorExit(j_common_ptr cinfo) {
    ThreadLocalJpeg* self =
        reinterpret_cast<ThreadLocalJpeg*>(cinfo->client_data);
    // Format the error message for later retrieval
    (*cinfo->err->format_message)(cinfo, self->error_message);
    // Jump back to setjmp in Get() or decode function
    std::longjmp(self->jump_buffer, 1);
  }

  jpeg_decompress_struct* Get() {
    if (!inited) {
      cinfo.err = jpeg_std_error(&jerr);
      jerr.error_exit = ErrorExit;
      cinfo.client_data = this;
      jpeg_create_decompress(&cinfo);
      inited = true;
    } else {
      // Reset state in case a previous call aborted early.
      jpeg_abort_decompress(&cinfo);
    }
    return &cinfo;
  }
};

static thread_local ThreadLocalJpeg g_tls_jpeg;
}  // namespace

/// @brief Decode compressed image data based on the specified format
///
/// Routes the compressed data to the appropriate decoder based on format type.
/// Supports JPEG, PNG, and BMP formats commonly used in MRXS slides.
///
/// @param data Compressed image data
/// @param format Image format (JPEG/PNG/BMP)
/// @return StatusOr containing decoded RGB image or error
/// @retval absl::InvalidArgumentError if format is unknown or unsupported
absl::StatusOr<RGBImage> DecodeImage(const std::vector<uint8_t>& data,
                                     MrxsImageFormat format) {
  switch (format) {
    case MrxsImageFormat::kJpeg:
      return DecodeJpeg(data);
    case MrxsImageFormat::kPng:
      return DecodePng(data);
    case MrxsImageFormat::kBmp:
      return DecodeBmp(data);
    default:
      return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                         "Unknown or unsupported image format");
  }
}

/// @brief Decode JPEG-compressed image data using libjpeg(-turbo fast path)
///
/// Decompresses JPEG data and converts to RGB format. Handles standard JPEG
/// tiles as used in MRXS slides. The decoder forces RGB output and uses
/// libjpeg-turbo's SIMD paths when available.
///
/// Uses setjmp/longjmp error handling to catch JPEG library errors and convert
/// them to Status returns instead of crashing the process.
///
/// @param data JPEG-compressed data
/// @return StatusOr containing decoded RGB image or error
/// @retval absl::InvalidArgumentError if data is empty
/// @retval absl::InternalError if JPEG decompression fails
absl::StatusOr<RGBImage> DecodeJpeg(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument, "Empty JPEG data");
  }

  jpeg_decompress_struct* c = g_tls_jpeg.Get();

  // Set up error handling - if any JPEG operation fails, we'll longjmp here
  if (setjmp(g_tls_jpeg.jump_buffer)) {
    // Error occurred during JPEG operations
    jpeg_abort_decompress(c);
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        absl::StrFormat("JPEG decode error: %s", g_tls_jpeg.error_message));
  }

  // Feed memory buffer
  jpeg_mem_src(c, data.data(), data.size());

  // Read header
  if (jpeg_read_header(c, TRUE) != JPEG_HEADER_OK) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to read JPEG header");
  }

  // ---- Fast knobs (good quality, much faster) ----
  c->dct_method = JDCT_IFAST;      // fast integer DCT
  c->do_fancy_upsampling = FALSE;  // faster chroma upsampling
  c->do_block_smoothing = FALSE;   // faster for progressive images
  c->quantize_colors = FALSE;      // ensure no palette quantization
  c->dither_mode = JDITHER_NONE;

  // Prefer libjpeg-turbo's packed RGB SIMD path if available.
#ifdef JCS_EXT_RGB
  c->out_color_space = JCS_EXT_RGB;  // fastest packed RGB on turbo
#else
  c->out_color_space = JCS_RGB;
#endif

  // Optional native downscale (1/2, 1/4, 1/8). Uncomment if desired:
  // c->scale_num = 1; c->scale_denom = 2;

  // Start decompression
  if (!jpeg_start_decompress(c)) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to start JPEG decompression");
  }

  const uint32_t width = static_cast<uint32_t>(c->output_width);
  const uint32_t height = static_cast<uint32_t>(c->output_height);
  const int channels = c->output_components;  // expect 3 for RGB
  if (channels != 3) {
    jpeg_finish_decompress(c);
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        absl::StrFormat("Expected 3 channels (RGB), got %d", channels));
  }

  // Create RGB image
  RGBImage result(ImageDimensions{width, height}, ImageFormat::kRGB,
                  DataType::kUInt8);

  // Read scanlines directly into destination buffer (no extra copy)
  uint8_t* dst = result.GetData();
  const JDIMENSION row_stride = static_cast<JDIMENSION>(width * channels);

  // Batch a few scanlines to reduce call overhead
  const JDIMENSION kBatch = 32;
  std::array<JSAMPROW, kBatch> rows;

  while (c->output_scanline < c->output_height) {
    JDIMENSION n =
        std::min<JDIMENSION>(kBatch, c->output_height - c->output_scanline);
    for (JDIMENSION i = 0; i < n; ++i) {
      rows[i] =
          dst + (static_cast<size_t>(c->output_scanline) + i) * row_stride;
    }
    JDIMENSION got = jpeg_read_scanlines(c, rows.data(), n);
    if (got == 0) {
      jpeg_finish_decompress(c);
      return MAKE_STATUS(absl::StatusCode::kInternal,
                         "jpeg_read_scanlines returned 0");
    }
  }

  // Finish (TLS decompressor remains for reuse)
  jpeg_finish_decompress(c);
  return result;
}

/// @brief Decode PNG-compressed image data using lodepng
///
/// Decompresses PNG data and converts to RGB format. Uses the lodepng library
/// for decoding. Forces 8-bit RGB output suitable for OpenSlide compatibility.
///
/// @param data PNG-compressed data
/// @return StatusOr containing decoded RGB image or error
/// @retval absl::InvalidArgumentError if data is empty
/// @retval absl::InternalError if PNG decompression fails
absl::StatusOr<RGBImage> DecodePng(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument, "Empty PNG data");
  }

  std::vector<unsigned char> image;
  unsigned int width, height;

  // Decode PNG to RGB
  unsigned int error = lodepng::decode(image, width, height, data, LCT_RGB, 8);

  if (error) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       absl::StrFormat("PNG decode error %d: %s", error,
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
/// @retval absl::InvalidArgumentError if data is too small or invalid
/// @retval absl::UnimplementedError if BMP is not 24-bit uncompressed
absl::StatusOr<RGBImage> DecodeBmp(const std::vector<uint8_t>& data) {
  // Simplified BMP decoder for 24-bit uncompressed BMP
  if (data.size() < 54) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "BMP data too small");
  }

  // Check BMP signature
  if (data[0] != 'B' || data[1] != 'M') {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Invalid BMP signature");
  }

  // Read header
  const int32_t data_offset = *reinterpret_cast<const int32_t*>(&data[10]);
  const int32_t width = *reinterpret_cast<const int32_t*>(&data[18]);
  const int32_t height_raw = *reinterpret_cast<const int32_t*>(&data[22]);
  const int16_t bits_per_pixel = *reinterpret_cast<const int16_t*>(&data[28]);

  // Only support 24-bit BMP
  if (bits_per_pixel != 24) {
    return MAKE_STATUS(absl::StatusCode::kUnimplemented,
                       absl::StrFormat("Only 24-bit BMP supported, got %d-bit",
                                       bits_per_pixel));
  }

  const int32_t height = std::abs(height_raw);
  const bool top_down = height_raw < 0;

  // Calculate row stride (rows are padded to 4-byte boundaries)
  const uint32_t row_stride_src = ((width * 3) + 3) & ~3;

  if (data_offset + row_stride_src * height >
      static_cast<int32_t>(data.size())) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
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
