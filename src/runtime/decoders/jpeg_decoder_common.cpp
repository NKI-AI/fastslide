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

#include "fastslide/runtime/decoders/jpeg_decoder.h"

#include <cstddef>
#include <cstdint>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide::runtime::decoders {
namespace {

constexpr uint8_t kMarkerPrefix = 0xFF;

[[nodiscard]] bool IsSofMarker(uint8_t marker) {
  // Baseline / extended sequential / progressive / lossless and variants:
  // SOF0..SOF3, SOF5..SOF7, SOF9..SOF11, SOF13..SOF15
  if (marker >= 0xC0 && marker <= 0xCF) {
    // Exclude DHT (C4), JPG (C8), DAC (CC)
    return marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
  }
  return false;
}

[[nodiscard]] uint16_t ReadBeU16(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                               static_cast<uint16_t>(p[1]));
}

}  // namespace

aifocore::Result<ImageDimensions> GetJpegDimensions(
    std::span<const uint8_t> jpeg_bytes) {
  // Minimal JPEG parser: scan markers until SOF, then read height/width.
  // References: ISO/IEC 10918-1 marker structure.
  if (jpeg_bytes.size() < 4) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "JPEG too small");
  }
  if (jpeg_bytes[0] != 0xFF || jpeg_bytes[1] != 0xD8) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Missing JPEG SOI marker");
  }

  size_t i = 2;
  while (i + 1 < jpeg_bytes.size()) {
    // Find marker prefix 0xFF (may have fill bytes).
    if (jpeg_bytes[i] != kMarkerPrefix) {
      ++i;
      continue;
    }
    while (i < jpeg_bytes.size() && jpeg_bytes[i] == kMarkerPrefix) {
      ++i;
    }
    if (i >= jpeg_bytes.size()) {
      break;
    }
    const uint8_t marker = jpeg_bytes[i++];

    // Standalone markers without length.
    if (marker == 0xD9 /*EOI*/ || marker == 0xDA /*SOS*/) {
      break;
    }
    if (marker >= 0xD0 && marker <= 0xD7) {
      continue;  // restart markers
    }

    if (i + 1 >= jpeg_bytes.size()) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Truncated JPEG segment length");
    }
    const uint16_t seg_len = ReadBeU16(jpeg_bytes.data() + i);
    i += 2;
    if (seg_len < 2) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Invalid JPEG segment length");
    }
    if (i + (seg_len - 2) > jpeg_bytes.size()) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Truncated JPEG segment payload");
    }

    if (IsSofMarker(marker)) {
      // SOF segment payload:
      // precision(1), height(2), width(2), components(1), ...
      if (seg_len < 2 + 1 + 2 + 2 + 1) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "Invalid JPEG SOF segment length");
      }
      const uint8_t* p = jpeg_bytes.data() + i;
      const uint16_t height = ReadBeU16(p + 1);
      const uint16_t width = ReadBeU16(p + 3);
      if (width == 0 || height == 0) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                    "Invalid JPEG dimensions");
      }
      return ImageDimensions{static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height)};
    }

    i += (seg_len - 2);
  }

  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                              "JPEG SOF marker not found");
}

}  // namespace fastslide::runtime::decoders
