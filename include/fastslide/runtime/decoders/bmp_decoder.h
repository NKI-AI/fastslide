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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_BMP_DECODER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_BMP_DECODER_H_

#include <cstdint>
#include <span>

#include "aifocore/status/result.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"

namespace fastslide::runtime::decoders {

/// @brief Decode an uncompressed 24-bit BMP bitstream to 8-bit RGB pixels.
///
/// Targeted at the `BMP24` (Windows V3+ `BITMAPINFOHEADER`, `biCompression =
/// BI_RGB`, `biPlanes = 1`, `biBitCount = 24`) variant produced by 3DHISTECH
/// MRXS slides. Both top-down and bottom-up row orderings are supported, and
/// the on-disk BGR channel order is converted to RGB. Anything else
/// (compressed BMPs such as RLE/BITFIELDS, 1/4/8/16/32-bit depths, multiple
/// planes, embedded JPEG/PNG, ...) is reported as `kUnimplemented`.
///
/// @param bmp_bytes In-memory BMP bitstream.
/// @return DecodedRgb on success, otherwise a status describing the failure.
[[nodiscard]] aifocore::Result<DecodedRgb> DecodeBmpToRgb(
    std::span<const uint8_t> bmp_bytes);

}  // namespace fastslide::runtime::decoders

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_BMP_DECODER_H_
