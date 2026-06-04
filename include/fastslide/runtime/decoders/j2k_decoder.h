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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_J2K_DECODER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_J2K_DECODER_H_

#include <cstdint>
#include <span>

#include "aifocore/status/result.h"
#include "fastslide/runtime/decoders/decoded_image.h"

namespace fastslide::runtime::decoders {

/// @brief Source colour space of a JPEG 2000 codestream.
///
/// JPEG 2000 itself only encodes signal samples; the interpretation comes from
/// the container (e.g. DICOM `PhotometricInterpretation`). When the source is
/// `YBR_ICT`/`YBR_RCT`, the OpenJPEG decoder returns raw Y/Cb/Cr samples with a
/// neutral chroma value of 128, which the caller must convert to RGB.
enum class J2kColorspace {
  kRgb,    ///< Components are R, G, B in display order.
  kYCbCr,  ///< Components are Y, Cb, Cr (e.g. DICOM `YBR_ICT`/`YBR_RCT`).
};

struct J2kDecodeOptions {
  J2kColorspace colorspace = J2kColorspace::kRgb;
};

/// @brief Decode a JPEG 2000 codestream (J2K or JP2) to packed RGB8.
///
/// Handles arbitrary horizontal/vertical chroma subsampling; for YCbCr input
/// the conversion uses the JPEG/ITU-R BT.601 coefficients
[[nodiscard]] aifocore::Result<DecodedRgb> DecodeJ2kToRgb(
    std::span<const uint8_t> j2k_bytes, const J2kDecodeOptions& options = {});

/// @brief Decode a JPEG 2000 codestream (J2K or JP2) at native 16-bit
/// precision into interleaved samples.
///
/// Accepts both single-component (Olympus VSI fluorescence sub-stacks,
/// one filter per stack) and three-component (brightfield-style RGB)
/// codestreams; the result's ``channels`` field tells the caller which
/// is which. Preserves the codestream's native sample precision:
/// 12/14/16-bit components are linearly rescaled into the full
/// ``[0, 65535]`` range (left-shifted by ``16 - prec``) rather than
/// quantised to 8 bits.
///
/// YCbCr -> RGB conversion is not supported for the 16-bit path because
/// the fluorescence slides we target are always stored as RGB or mono;
/// only ``J2kColorspace::kRgb`` is accepted here.
[[nodiscard]] aifocore::Result<DecodedRgb16> DecodeJ2k16(
    std::span<const uint8_t> j2k_bytes, const J2kDecodeOptions& options = {});

}  // namespace fastslide::runtime::decoders

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_J2K_DECODER_H_
