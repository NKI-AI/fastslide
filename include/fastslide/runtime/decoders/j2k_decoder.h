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
#include "fastslide/runtime/decoders/jpeg_decoder.h"

namespace fastslide::runtime::decoders {

/// @brief Decode a JPEG 2000 codestream (J2K or JP2) to RGB8 using OpenJPEG.
[[nodiscard]] aifocore::Result<DecodedRgb> DecodeJ2kToRgb(
    std::span<const uint8_t> j2k_bytes);

}  // namespace fastslide::runtime::decoders

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_J2K_DECODER_H_
