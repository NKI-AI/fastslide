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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DECODER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DECODER_H_

#include <cstdint>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"

namespace fastslide {
namespace mrxs {
namespace internal {

/// @brief Decode a compressed MRXS image into an RGB8 `RGBImage`.
///
/// Routes to the appropriate centralized `runtime::decoders` backend based
/// on `format` (JPEG / PNG / BMP) and repacks the resulting RGB pixels into
/// an `RGBImage`. The JPEG path forces `no_ycbcr_conversion = true` to
/// match what 3DHISTECH MRXS slides expect.
///
/// @param data Compressed image bitstream.
/// @param format Image format (JPEG / PNG / BMP).
/// @return RGB8 `RGBImage` on success, otherwise a status describing the
///         decode failure.
[[nodiscard]] aifocore::Result<RGBImage> DecodeImage(
    const std::vector<uint8_t>& data, MrxsImageFormat format);

/// @brief Decode a compressed MRXS image into an RGB16 `Image`.
///
/// Used for 16-bit fluorescence slides where each PNG tile carries one
/// 16-bit sample per RGB plane. Currently only `MrxsImageFormat::kPng` is
/// supported as 16-bit; other formats fall back to an error.
///
/// @param data   Compressed image bitstream.
/// @param format Image format (only PNG is meaningful for 16-bit).
/// @return RGB16 `Image` (interleaved, host endianness) on success.
[[nodiscard]] aifocore::Result<Image> DecodeImage16(
    const std::vector<uint8_t>& data, MrxsImageFormat format);

}  // namespace internal
}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_DECODER_H_
