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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_PNG_DECODER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_PNG_DECODER_H_

#include <cstdint>
#include <span>
#include <string_view>

#include "aifocore/status/result.h"
#include "fastslide/runtime/decoders/decoded_image.h"

namespace fastslide::runtime::decoders {

/// @brief Decode a PNG bitstream to 8-bit RGB pixels (alpha dropped).
///
/// The decoder forces lodepng to deliver `LCT_RGB`/`8`, so palettized,
/// grayscale and 16-bit PNGs are converted automatically.
///
/// @param png_bytes In-memory PNG bitstream.
/// @return DecodedRgb on success, otherwise a status describing the
///         lodepng error.
[[nodiscard]] aifocore::Result<DecodedRgb> DecodePngToRgb(
    std::span<const uint8_t> png_bytes);

/// @brief Decode a PNG bitstream to 8-bit RGBA pixels.
///
/// @param png_bytes In-memory PNG bitstream.
/// @return DecodedRgba on success, otherwise a status describing the
///         lodepng error.
[[nodiscard]] aifocore::Result<DecodedRgba> DecodePngToRgba(
    std::span<const uint8_t> png_bytes);

/// @brief Decode a PNG bitstream to 16-bit RGB pixels (alpha dropped).
///
/// Forces lodepng to deliver `LCT_RGB`/`16`. PNG stores 16-bit samples in
/// big-endian byte order; this routine returns values in host endianness so
/// callers can treat the output as a flat `uint16_t` buffer regardless of the
/// platform.
///
/// @param png_bytes In-memory PNG bitstream.
/// @return DecodedRgb16 on success, otherwise a status describing the
///         lodepng error.
[[nodiscard]] aifocore::Result<DecodedRgb16> DecodePng16ToRgb(
    std::span<const uint8_t> png_bytes);

/// @brief Load a PNG file from disk and return RGB8 pixels.
///
/// Convenience wrapper that reads the file into memory and forwards to
/// `DecodePngToRgb`.
[[nodiscard]] aifocore::Result<DecodedRgb> DecodePngFileToRgb(
    std::string_view path);

/// @brief Load a PNG file from disk and return RGBA8 pixels.
[[nodiscard]] aifocore::Result<DecodedRgba> DecodePngFileToRgba(
    std::string_view path);

/// @brief Encode RGB or RGBA pixels to a PNG file on disk.
///
/// @param path     Destination file path.
/// @param pixels   Interleaved 8-bit pixel buffer.
/// @param width    Image width in pixels.
/// @param height   Image height in pixels.
/// @param channels Number of channels per pixel (must be 3 or 4).
/// @return OkStatus on success, otherwise a status describing the lodepng
///         encode error or invalid arguments.
[[nodiscard]] aifocore::Status EncodePngToFile(std::string_view path,
                                               std::span<const uint8_t> pixels,
                                               uint32_t width, uint32_t height,
                                               uint32_t channels);

/// @brief Encode an interleaved pixel buffer as a PNG file.
///
/// Supports 1/3/4 channels at 8 or 16 bits per channel. For 16-bit input
/// the pixel data must be little-endian uint16 (host order on common
/// platforms); the encoder byte-swaps internally so the on-disk PNG is
/// always written in the standard big-endian form.
///
/// @param path        Destination path.
/// @param pixels      Interleaved raw byte buffer. Length must equal
///                    ``width * height * channels * (bit_depth / 8)``.
/// @param width       Image width in pixels.
/// @param height      Image height in pixels.
/// @param channels    Components per pixel: 1 (gray), 3 (RGB), 4 (RGBA).
/// @param bit_depth   Bits per channel; must be 8 or 16.
/// @return Status, either ok or describing the failure.
[[nodiscard]] aifocore::Status EncodePngToFile(std::string_view path,
                                               std::span<const uint8_t> pixels,
                                               uint32_t width, uint32_t height,
                                               uint32_t channels,
                                               uint32_t bit_depth);

}  // namespace fastslide::runtime::decoders

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_PNG_DECODER_H_
