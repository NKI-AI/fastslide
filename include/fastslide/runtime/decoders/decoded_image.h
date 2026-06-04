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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_DECODED_IMAGE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_DECODED_IMAGE_H_

#include <cstdint>
#include <vector>

/// @file
/// @brief Codec-agnostic, decoded-pixel-buffer types shared between the
/// JPEG, PNG, JPEG-XR, BMP, and JPEG 2000 decoders.
///
/// Keeping these structs in a standalone header avoids one decoder
/// header pulling in another just to reuse a return type (e.g. the JPEG
/// 2000 decoder returning a `DecodedRgb16` previously forced the J2K
/// library to depend on the PNG library).

namespace fastslide::runtime::decoders {

/// @brief Decoded 8-bit RGB pixels.
///
/// Interleaved RGB8 buffer (``rgb.size() == width * height * 3``).
struct DecodedRgb {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgb;
};

/// @brief Decoded 8-bit RGBA pixels.
///
/// Interleaved RGBA8 buffer (``rgba.size() == width * height * 4``).
struct DecodedRgba {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

/// @brief Decoded 16-bit-per-channel interleaved samples.
///
/// Values are stored in host endianness; decoders that produce
/// big-endian samples (e.g. PNG 16-bit) byte-swap on little-endian
/// platforms before returning. The buffer is always interleaved;
/// ``channels`` selects between mono (Olympus VSI fluorescence
/// sub-stacks), RGB (PNG/JPEG-XR/JP2 colour) and other counts.
/// ``rgb.size() == width * height * channels``.
struct DecodedRgb16 {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t channels = 3;
  std::vector<uint16_t> rgb;
};

}  // namespace fastslide::runtime::decoders

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_DECODED_IMAGE_H_
