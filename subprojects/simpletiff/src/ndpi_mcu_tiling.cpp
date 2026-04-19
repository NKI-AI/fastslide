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

#include "simpletiff/internal/ndpi_mcu_tiling.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "simpletiff/io_utils.h"

namespace simpletiff::internal {
namespace {

[[nodiscard]] uint32_t CeilDivU32(uint32_t value, uint32_t divisor) {
  if (divisor == 0) {
    return 0;
  }
  return (value + divisor - 1) / divisor;
}

[[nodiscard]] bool IsSofMarker(uint8_t marker) {
  return (marker >= 0xC0 && marker <= 0xC3) ||
         (marker >= 0xC5 && marker <= 0xC7) ||
         (marker >= 0xC9 && marker <= 0xCB) ||
         (marker >= 0xCD && marker <= 0xCF);
}

struct JpegHeaderInfo {
  uint16_t restart_interval = 0;  // DRI (FF DD)
  uint16_t mcu_w = 0;
  uint16_t mcu_h = 0;
};

[[nodiscard]] bool ParseJpegHeaderInfoBounded(int file_descriptor,
                                              size_t file_size,
                                              uint64_t jpeg_start_offset,
                                              JpegHeaderInfo& out) {
  constexpr size_t kProbeBytes = 64 * 1024;
  const size_t to_read = static_cast<size_t>(
      std::min<uint64_t>(kProbeBytes, file_size - jpeg_start_offset));
  if (to_read < 4) {
    return false;
  }

  std::vector<uint8_t> bytes;
  if (!ReadBytes(file_descriptor, file_size, jpeg_start_offset, to_read,
                 bytes) ||
      bytes.size() < 2 || bytes[0] != 0xFF || bytes[1] != 0xD8) {
    return false;
  }

  bool have_dri = false;
  bool have_sof = false;
  uint8_t max_h = 1;
  uint8_t max_v = 1;

  size_t pos = 2;  // after SOI
  while (pos + 1 < bytes.size()) {
    if (bytes[pos] != 0xFF) {
      ++pos;
      continue;
    }
    while (pos < bytes.size() && bytes[pos] == 0xFF) {
      ++pos;
    }
    if (pos >= bytes.size()) {
      break;
    }

    const uint8_t marker = bytes[pos++];
    if (marker == 0xDA) {  // SOS
      break;
    }

    // Markers without payload.
    if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) ||
        marker == 0x01) {
      continue;
    }

    if (pos + 1 >= bytes.size()) {
      break;
    }
    const uint16_t seg_len =
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[pos]) << 8U |
                              static_cast<uint16_t>(bytes[pos + 1]));
    pos += 2;
    if (seg_len < 2) {
      return false;
    }
    const size_t payload = static_cast<size_t>(seg_len - 2);
    if (pos + payload > bytes.size()) {
      break;
    }

    if (marker == 0xDD) {  // DRI
      if (payload < 2) {
        return false;
      }
      out.restart_interval =
          static_cast<uint16_t>(static_cast<uint16_t>(bytes[pos]) << 8U |
                                static_cast<uint16_t>(bytes[pos + 1]));
      have_dri = true;
    } else if (IsSofMarker(marker)) {
      // SOF payload: P(1), Y(2), X(2), Nf(1), [C(1),HV(1),Tq(1)] * Nf
      if (payload < 8) {
        return false;
      }
      const uint8_t components = bytes[pos + 5];
      size_t comp_pos = pos + 6;
      max_h = 1;
      max_v = 1;
      for (uint8_t i = 0; i < components; ++i) {
        if (comp_pos + 2 >= pos + payload) {
          return false;
        }
        const uint8_t hv = bytes[comp_pos + 1];
        const uint8_t h_samp = static_cast<uint8_t>(hv >> 4U);
        const uint8_t v_samp = static_cast<uint8_t>(hv & 0x0FU);
        max_h = std::max<uint8_t>(max_h, h_samp == 0 ? 1 : h_samp);
        max_v = std::max<uint8_t>(max_v, v_samp == 0 ? 1 : v_samp);
        comp_pos += 3;
      }
      have_sof = true;
    }

    pos += payload;
  }

  if (!have_dri || !have_sof || out.restart_interval == 0) {
    return false;
  }
  out.mcu_w = static_cast<uint16_t>(max_h * 8U);
  out.mcu_h = static_cast<uint16_t>(max_v * 8U);
  return out.mcu_w != 0 && out.mcu_h != 0;
}

}  // namespace

bool ComputeNdpiMcuTileGeometryFromJpegHeader(
    int file_descriptor, size_t file_size, uint64_t jpeg_start_offset,
    uint32_t image_width, uint32_t image_height, NdpiMcuTileGeometry& out) {
  out = NdpiMcuTileGeometry{};
  if (image_width == 0 || image_height == 0) {
    return false;
  }

  JpegHeaderInfo info{};
  if (!ParseJpegHeaderInfoBounded(file_descriptor, file_size, jpeg_start_offset,
                                  info)) {
    return false;
  }

  const uint32_t mcus_per_row = CeilDivU32(image_width, info.mcu_w);
  if (mcus_per_row == 0 || info.restart_interval > mcus_per_row ||
      (mcus_per_row % info.restart_interval) != 0) {
    return false;
  }

  const uint32_t tile_w_u32 = static_cast<uint32_t>(info.restart_interval) *
                              static_cast<uint32_t>(info.mcu_w);
  const uint32_t tile_h_u32 = static_cast<uint32_t>(info.mcu_h);
  if (tile_w_u32 == 0 || tile_h_u32 == 0 ||
      tile_w_u32 > std::numeric_limits<uint16_t>::max() ||
      tile_h_u32 > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  if ((image_width % tile_w_u32) != 0) {
    return false;
  }

  const uint32_t tiles_x = image_width / tile_w_u32;
  const uint32_t tiles_y = CeilDivU32(image_height, tile_h_u32);
  if (tiles_x == 0 || tiles_y == 0) {
    return false;
  }

  out.tile_w = static_cast<uint16_t>(tile_w_u32);
  out.tile_h = static_cast<uint16_t>(tile_h_u32);
  out.tiles_x = tiles_x;
  out.tiles_y = tiles_y;
  return true;
}

bool BuildOffsetsAndBytecountsFromNdpiMcuStarts(
    uint64_t strip_start_offset, uint64_t strip_byte_count,
    std::span<const uint64_t> mcu_starts, std::span<uint64_t> offsets_out,
    std::span<uint64_t> bytecounts_out) {
  if (mcu_starts.empty() || offsets_out.size() != mcu_starts.size() ||
      bytecounts_out.size() != mcu_starts.size()) {
    return false;
  }
  if (strip_byte_count == 0) {
    return false;
  }

  // NDPI MCU_STARTS values are 32-bit offsets *within* the strip JPEG stream
  // and can wrap at 2^32 for very large strips. We reconstruct a monotonic
  // 64-bit stream position by tracking wrap-arounds.
  uint64_t base = 0;
  uint64_t prev = (mcu_starts[0] & 0xFFFFFFFFULL);
  std::vector<uint64_t> starts64;
  starts64.resize(mcu_starts.size());
  starts64[0] = prev;
  for (size_t i = 1; i < mcu_starts.size(); ++i) {
    const uint64_t cur32 = (mcu_starts[i] & 0xFFFFFFFFULL);
    if (cur32 < prev) {
      base += (1ULL << 32U);
    }
    const uint64_t cur64 = base + cur32;
    starts64[i] = cur64;
    prev = cur32;
  }

  const uint64_t max_start = starts64.back();
  while (strip_byte_count <= max_start) {
    strip_byte_count += (1ULL << 32U);
  }

  for (size_t i = 0; i < starts64.size(); ++i) {
    offsets_out[i] = strip_start_offset + starts64[i];
    const uint64_t next =
        (i + 1 < starts64.size()) ? starts64[i + 1] : strip_byte_count;
    if (next < starts64[i]) {
      return false;
    }
    bytecounts_out[i] = next - starts64[i];
  }
  return true;
}

bool BuildNdpiMcuTilesFromMcuStarts(
    int file_descriptor, size_t file_size, uint64_t strip_start_offset,
    uint64_t strip_byte_count, uint32_t image_width, uint32_t image_height,
    std::span<const uint64_t> mcu_starts, NdpiMcuTileGeometry& geometry_out,
    std::vector<uint64_t>& offsets_out, std::vector<uint64_t>& bytecounts_out) {
  geometry_out = NdpiMcuTileGeometry{};
  offsets_out.clear();
  bytecounts_out.clear();

  if (!ComputeNdpiMcuTileGeometryFromJpegHeader(file_descriptor, file_size,
                                                strip_start_offset, image_width,
                                                image_height, geometry_out)) {
    return false;
  }
  if (mcu_starts.empty() ||
      (geometry_out.tiles_x * geometry_out.tiles_y) != mcu_starts.size()) {
    return false;
  }

  offsets_out.resize(mcu_starts.size());
  bytecounts_out.resize(mcu_starts.size());
  return BuildOffsetsAndBytecountsFromNdpiMcuStarts(
      strip_start_offset, strip_byte_count, mcu_starts, offsets_out,
      bytecounts_out);
}

}  // namespace simpletiff::internal
