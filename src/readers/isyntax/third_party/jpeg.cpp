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

#include "fastslide/readers/isyntax/third_party/jpeg.h"

#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#ifdef defer
#undef defer
#endif
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/readers/isyntax/third_party/base64.h"
#include "fastslide/readers/isyntax/third_party/platform/intrinsics.h"
#include "fastslide/readers/isyntax/third_party/platform/platform.h"

// Toolchains differ on whether they define `__ARM_NEON` or `__ARM_NEON__`, and
// `platform/intrinsics.h` only includes `<arm_neon.h>` for some compiler/macro
// combinations. Include it explicitly when NEON is enabled.
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace {

bool HasJpegSoi(const uint8_t *data, size_t len) {
  return len >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

bool HasJpegEoi(const uint8_t *data, size_t len) {
  return len >= 2 && data[len - 2] == 0xFF && data[len - 1] == 0xD9;
}

aifocore::Result<std::vector<uint8_t>>
ReadFileBytesAtOffset(file_handle_t file_handle, int64_t read_offset,
                      size_t read_size) {
  if (read_offset <= 0 || read_size == 0) {
    return std::vector<uint8_t>{};
  }
  std::vector<uint8_t> encoded(read_size);

  const ssize_t bytes_read =
      aifocore::portable_pread(static_cast<int>(file_handle), encoded.data(),
                               read_size, static_cast<uint64_t>(read_offset));
  if (bytes_read < 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "portable_pread failed");
  }
  if (static_cast<size_t>(bytes_read) != read_size) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("portable_pread short read: {} != {}", bytes_read,
                              read_size));
  }

  return encoded;
}

aifocore::Result<std::vector<uint8_t>>
Base64DecodeToVector(const uint8_t *encoded, size_t encoded_len) {
  if (encoded == nullptr || encoded_len == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "Base64DecodeToVector: empty input");
  }
  return isyntax::Base64Decode(std::span<const uint8_t>(encoded, encoded_len));
}

void SwapRedBlueInPlaceRaw(uint32_t *pixels, int width, int height) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }
  int num_pixels = width * height;
  int num_pixels_aligned = (num_pixels / 4) * 4;

#if defined(__ARM_NEON)
  for (int i = 0; i < num_pixels_aligned; i += 4) {
    uint32x4_t bgra = vld1q_u32(pixels + i);
    uint32x4_t b_mask = vdupq_n_u32(0x000000FF);
    uint32x4_t r_mask = vdupq_n_u32(0x00FF0000);
    uint32x4_t b = vandq_u32(bgra, b_mask);
    uint32x4_t r = vandq_u32(bgra, r_mask);
    uint32x4_t br_swapped = vorrq_u32(vshlq_n_u32(b, 16), vshrq_n_u32(r, 16));
    uint32x4_t ga_alpha_mask = vdupq_n_u32(0xFF00FF00);
    uint32x4_t ga_alpha = vandq_u32(bgra, ga_alpha_mask);
    uint32x4_t rgba = vorrq_u32(ga_alpha, br_swapped);
    vst1q_u32(pixels + i, rgba);
  }
#elif defined(__SSE2__)
  for (int i = 0; i < num_pixels_aligned; i += 4) {
    __m128i bgra =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(pixels + i));
    __m128i b_mask = _mm_set1_epi32(0x000000FF);
    __m128i r_mask = _mm_set1_epi32(0x00FF0000);
    __m128i b = _mm_and_si128(bgra, b_mask);
    __m128i r = _mm_and_si128(bgra, r_mask);
    __m128i br_swapped =
        _mm_or_si128(_mm_slli_epi32(b, 16), _mm_srli_epi32(r, 16));
    __m128i ga_alpha_mask = _mm_set1_epi32(0xFF00FF00);
    __m128i ga_alpha = _mm_and_si128(bgra, ga_alpha_mask);
    __m128i rgba = _mm_or_si128(ga_alpha, br_swapped);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(pixels + i), rgba);
  }
#endif

  for (int i = num_pixels_aligned; i < num_pixels; ++i) {
    uint32_t val = pixels[i];
    pixels[i] = ((val & 0xff) << 16) | (val & 0x00ff00) |
                ((val & 0xff0000) >> 16) | (val & 0xff000000);
  }
}

} // namespace

namespace isyntax {
namespace jpeg {

void SwapRedBlueInPlace(std::span<uint32_t> pixels, int width, int height) {
  if (pixels.empty() || width <= 0 || height <= 0) {
    return;
  }
  SwapRedBlueInPlaceRaw(pixels.data(), width, height);
}

aifocore::Result<std::vector<uint8_t>>
ReadAssociatedImageJpegBytes(isyntax_t *isyntax, isyntax_image_t *image) {
  if (isyntax == nullptr || image == nullptr || isyntax->file_handle == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "ReadAssociatedImageJpegBytes: invalid arguments");
  }
  auto encoded_or = ReadFileBytesAtOffset(isyntax->file_handle,
                                          image->base64_encoded_jpg_file_offset,
                                          image->base64_encoded_jpg_len);
  AIFOCORE_RETURN_IF_ERROR(encoded_or.status());
  return Base64DecodeToVector(encoded_or->data(), encoded_or->size());
}

aifocore::Result<std::vector<uint8_t>>
ReadIccProfileBytes(isyntax_t *isyntax, isyntax_image_t *image) {
  if (isyntax == nullptr || image == nullptr || isyntax->file_handle == 0) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "ReadIccProfileBytes: invalid arguments");
  }
  auto encoded_or = ReadFileBytesAtOffset(
      isyntax->file_handle, image->base64_encoded_icc_profile_file_offset,
      image->base64_encoded_icc_profile_len);
  AIFOCORE_RETURN_IF_ERROR(encoded_or.status());
  return Base64DecodeToVector(encoded_or->data(), encoded_or->size());
}

aifocore::Result<DecodedImage> DecodeJpeg(std::span<const uint8_t> jpeg_bytes,
                                          isyntax_pixel_format_t fmt) {
  if (jpeg_bytes.empty()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "DecodeJpeg: empty input");
  }

  const uint8_t *input = jpeg_bytes.data();
  const size_t input_len = jpeg_bytes.size();

  std::vector<uint8_t> with_markers;
  if (!HasJpegSoi(input, input_len) || !HasJpegEoi(input, input_len)) {
    with_markers.reserve(input_len + 4);
    if (!HasJpegSoi(input, input_len)) {
      with_markers.push_back(0xFF);
      with_markers.push_back(0xD8);
    }
    with_markers.insert(with_markers.end(), input, input + input_len);
    if (!HasJpegEoi(input, input_len)) {
      with_markers.push_back(0xFF);
      with_markers.push_back(0xD9);
    }
    input = with_markers.data();
  }

  if (fmt != ISYNTAX_PIXEL_FORMAT_RGBA && fmt != ISYNTAX_PIXEL_FORMAT_BGRA) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            "DecodeJpeg: invalid pixel format");
  }

  AIFOCORE_ASSIGN_OR_RETURN(
      auto decoded_rgb,
      fastslide::runtime::decoders::DecodeJpegToRgb(std::span<const uint8_t>(
          input, with_markers.empty() ? input_len : with_markers.size())));

  const int w = static_cast<int>(decoded_rgb.width);
  const int h = static_cast<int>(decoded_rgb.height);
  const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
  if (w <= 0 || h <= 0 || decoded_rgb.rgb.size() != count * 3) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "DecodeJpeg: invalid decoded size");
  }

  std::vector<uint32_t> pixels(count);
  for (size_t i = 0; i < count; ++i) {
    const uint8_t r = decoded_rgb.rgb[i * 3 + 0];
    const uint8_t g = decoded_rgb.rgb[i * 3 + 1];
    const uint8_t b = decoded_rgb.rgb[i * 3 + 2];
    const uint32_t rgba =
        (static_cast<uint32_t>(r) << 0) | (static_cast<uint32_t>(g) << 8) |
        (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(255) << 24);
    pixels[i] = rgba;
  }
  if (fmt == ISYNTAX_PIXEL_FORMAT_BGRA) {
    SwapRedBlueInPlaceRaw(pixels.data(), w, h);
  }

  DecodedImage out;
  out.width = w;
  out.height = h;
  out.pixels = std::move(pixels);
  return out;
}

aifocore::Result<DecodedImage>
ReadAssociatedImagePixels(isyntax_t *isyntax, isyntax_image_t *image,
                          isyntax_pixel_format_t fmt) {
  auto jpeg_or = ReadAssociatedImageJpegBytes(isyntax, image);
  AIFOCORE_RETURN_IF_ERROR(jpeg_or.status());
  return DecodeJpeg(std::span<const uint8_t>(jpeg_or->data(), jpeg_or->size()),
                    fmt);
}

} // namespace jpeg
} // namespace isyntax
