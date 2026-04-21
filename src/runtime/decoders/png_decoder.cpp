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

#include "fastslide/runtime/decoders/png_decoder.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "lodepng/lodepng.h"

namespace fastslide::runtime::decoders {
namespace {

/// @brief Build a lodepng-flavoured Internal status.
aifocore::Status MakePngError(unsigned int error, std::string_view stage) {
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kInternal,
      aifocore::fmt::format("PNG {} error {}: {}", stage, error,
                            lodepng_error_text(error)));
}

}  // namespace

aifocore::Result<DecodedRgb> DecodePngToRgb(
    std::span<const uint8_t> png_bytes) {
  if (png_bytes.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Empty PNG data");
  }

  std::vector<unsigned char> pixels;
  unsigned int width = 0;
  unsigned int height = 0;
  const unsigned int err = lodepng::decode(
      pixels, width, height, png_bytes.data(), png_bytes.size(), LCT_RGB, 8);
  if (err != 0U) {
    return MakePngError(err, "decode");
  }

  DecodedRgb out;
  out.width = width;
  out.height = height;
  out.rgb = std::move(pixels);
  return out;
}

aifocore::Result<DecodedRgb16> DecodePng16ToRgb(
    std::span<const uint8_t> png_bytes) {
  if (png_bytes.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Empty PNG data");
  }

  std::vector<unsigned char> pixels;
  unsigned int width = 0;
  unsigned int height = 0;
  const unsigned int err = lodepng::decode(
      pixels, width, height, png_bytes.data(), png_bytes.size(), LCT_RGB, 16);
  if (err != 0U) {
    return MakePngError(err, "decode");
  }

  const std::size_t sample_count = static_cast<std::size_t>(width) * height * 3;
  if (pixels.size() != sample_count * 2U) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format(
            "PNG16 decode: byte size mismatch (expected {}, got {})",
            sample_count * 2U, pixels.size()));
  }

  DecodedRgb16 out;
  out.width = width;
  out.height = height;
  out.rgb.resize(sample_count);

  // PNG stores 16-bit samples big-endian; convert to host endianness.
  const unsigned char* src = pixels.data();
  for (std::size_t i = 0; i < sample_count; ++i) {
    const uint16_t hi = src[2 * i];
    const uint16_t lo = src[2 * i + 1];
    out.rgb[i] = static_cast<uint16_t>((hi << 8) | lo);
  }
  return out;
}

aifocore::Result<DecodedRgba> DecodePngToRgba(
    std::span<const uint8_t> png_bytes) {
  if (png_bytes.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Empty PNG data");
  }

  std::vector<unsigned char> pixels;
  unsigned int width = 0;
  unsigned int height = 0;
  const unsigned int err = lodepng::decode(
      pixels, width, height, png_bytes.data(), png_bytes.size(), LCT_RGBA, 8);
  if (err != 0U) {
    return MakePngError(err, "decode");
  }

  DecodedRgba out;
  out.width = width;
  out.height = height;
  out.rgba = std::move(pixels);
  return out;
}

aifocore::Result<DecodedRgb> DecodePngFileToRgb(std::string_view path) {
  std::vector<unsigned char> file_bytes;
  const std::string path_str(path);
  const unsigned int err = lodepng::load_file(file_bytes, path_str);
  if (err != 0U) {
    return MakePngError(err, "load");
  }
  return DecodePngToRgb(std::span<const uint8_t>(file_bytes));
}

aifocore::Result<DecodedRgba> DecodePngFileToRgba(std::string_view path) {
  std::vector<unsigned char> file_bytes;
  const std::string path_str(path);
  const unsigned int err = lodepng::load_file(file_bytes, path_str);
  if (err != 0U) {
    return MakePngError(err, "load");
  }
  return DecodePngToRgba(std::span<const uint8_t>(file_bytes));
}

aifocore::Status EncodePngToFile(std::string_view path,
                                 std::span<const uint8_t> pixels,
                                 uint32_t width, uint32_t height,
                                 uint32_t channels) {
  if (channels != 3U && channels != 4U) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("PNG encode: unsupported channel count {} "
                              "(must be 3 or 4)",
                              channels));
  }
  const std::size_t expected =
      static_cast<std::size_t>(width) * height * channels;
  if (pixels.size() != expected) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "PNG encode: pixel buffer size mismatch (expected {}, got {})",
            expected, pixels.size()));
  }

  const std::string path_str(path);
  const unsigned int err =
      (channels == 3U) ? lodepng_encode24_file(path_str.c_str(), pixels.data(),
                                               width, height)
                       : lodepng_encode32_file(path_str.c_str(), pixels.data(),
                                               width, height);
  if (err != 0U) {
    return MakePngError(err, "encode");
  }
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide::runtime::decoders
