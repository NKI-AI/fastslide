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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_JPEG_HEADER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_JPEG_HEADER_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "aifocore/status/result.h"

namespace fastslide {

/// @brief Build a patched NDPI JPEG header for a headerless tile payload.
inline aifocore::Status BuildPatchedNdpiJpegHeader(
    std::span<const uint8_t> header_template,
    std::span<const size_t> sof_height_offsets,
    std::span<const size_t> sof_width_offsets, uint16_t tile_width,
    uint16_t tile_height, std::vector<uint8_t>& out) {
  if (header_template.empty() || sof_height_offsets.empty() ||
      sof_height_offsets.size() != sof_width_offsets.size()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "NDPI JPEG header template is not initialized");
  }
  out.assign(header_template.begin(), header_template.end());
  for (size_t i = 0; i < sof_height_offsets.size(); ++i) {
    const size_t h_off = sof_height_offsets[i];
    const size_t w_off = sof_width_offsets[i];
    if (h_off + 1 >= out.size() || w_off + 1 >= out.size()) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "SOF offsets out of bounds");
    }
    out[h_off + 0] = static_cast<uint8_t>((tile_height >> 8U) & 0xFF);
    out[h_off + 1] = static_cast<uint8_t>(tile_height & 0xFF);
    out[w_off + 0] = static_cast<uint8_t>((tile_width >> 8U) & 0xFF);
    out[w_off + 1] = static_cast<uint8_t>(tile_width & 0xFF);
  }
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_JPEG_HEADER_H_
