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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_JPEG_DECODER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_JPEG_DECODER_H_

#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"

namespace fastslide::runtime::decoders {

struct DecodedRgb {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgb;
};

struct JpegDecodeOptions {
  // When using the jpgd backend, MRXS needs to skip YCbCr->RGB conversion for
  // some slides. For libjpeg-turbo this flag is currently ignored.
  bool no_ycbcr_conversion = false;
};

/// @brief Decode a JPEG bitstream to RGB8.
[[nodiscard]] aifocore::Result<DecodedRgb> DecodeJpegToRgb(
    std::span<const uint8_t> jpeg_bytes, const JpegDecodeOptions& options = {});

/// @brief Parse JPEG dimensions without fully decoding pixels.
///
/// This is a lightweight SOF-marker scan and does not depend on the chosen
/// decode backend.
[[nodiscard]] aifocore::Result<ImageDimensions> GetJpegDimensions(
    std::span<const uint8_t> jpeg_bytes);

}  // namespace fastslide::runtime::decoders

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_DECODERS_JPEG_DECODER_H_
