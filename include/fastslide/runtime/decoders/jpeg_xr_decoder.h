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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_JPEG_XR_DECODER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_JPEG_XR_DECODER_H_

#include <cstdint>
#include <optional>
#include <span>

#include "aifocore/status/result.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/runtime/decoders/png_decoder.h"

namespace fastslide::runtime::decoders {

struct ExpectedDimensions {
  uint32_t width = 0;
  uint32_t height = 0;
};

/// @brief Decode a JPEG-XR (JXR) bitstream to RGB8 using jxrlib.
[[nodiscard]] aifocore::Result<DecodedRgb> DecodeJpegXrToRgb(
    std::span<const uint8_t> jxr_bytes,
    std::optional<ExpectedDimensions> expected = std::nullopt);

/// @brief Decode a JPEG-XR (JXR) bitstream to interleaved RGB16 using jxrlib.
///
/// Output samples are returned in host endianness so callers can treat the
/// buffer as a flat `uint16_t` array. This is the path used by 16-bit
/// 3DHISTECH MRXS fluorescence slides where each filter level is stored as a
/// JPEG-XR with up to three 16-bit channels packed into the R/G/B planes.
[[nodiscard]] aifocore::Result<DecodedRgb16> DecodeJpegXrToRgb16(
    std::span<const uint8_t> jxr_bytes,
    std::optional<ExpectedDimensions> expected = std::nullopt);

}  // namespace fastslide::runtime::decoders

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_JPEG_XR_DECODER_H_
