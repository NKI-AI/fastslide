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

#include "fastslide/readers/czi/czi_parse.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

#include <zstd.h>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {
namespace czi {

namespace {

/// Read a little-endian scalar of type T from `bytes` at `offset`. The caller
/// must guarantee the span is large enough.
template <typename T>
T ReadLe(std::span<const uint8_t> bytes, size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

}  // namespace

aifocore::Result<DirEntryHeader> ParseDirEntryHeader(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < kDirEntryFixedSize) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "DirectoryEntryDV needs {} bytes, only {} available",
            kDirEntryFixedSize, bytes.size()));
  }

  // Field offsets within the fixed prefix (see czi_parse.h for the layout).
  if (bytes[0] != 'D' || bytes[1] != 'V') {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "DirectoryEntry has unexpected schema tag");
  }

  DirEntryHeader header;
  header.pixel_type = ReadLe<int32_t>(bytes, 2);
  header.file_position = ReadLe<int64_t>(bytes, 6);
  header.compression = ReadLe<int32_t>(bytes, 18);
  header.dimension_count = ReadLe<int32_t>(bytes, 28);
  if (header.dimension_count < 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Negative dimension count {}",
                              header.dimension_count));
  }
  return header;
}

aifocore::Result<DimensionRecord> ParseDimensionRecord(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < kDimensionEntrySize) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("DimensionEntryDV needs {} bytes, only {} "
                              "available",
                              kDimensionEntrySize, bytes.size()));
  }

  DimensionRecord record;
  record.axis = static_cast<char>(bytes[0]);
  record.start = ReadLe<int32_t>(bytes, 4);
  record.size = ReadLe<int32_t>(bytes, 8);
  // bytes[12..16): float start coordinate, not needed for tiling.
  record.stored_size = ReadLe<int32_t>(bytes, 16);
  return record;
}

int32_t DownsampleFromSizes(int32_t full_size, int32_t stored_size) {
  if (stored_size <= 0) {
    return 1;
  }
  const double ratio =
      static_cast<double>(full_size) / static_cast<double>(stored_size);
  const auto rounded = static_cast<int64_t>(std::llround(ratio));
  if (rounded <= 0) {
    return 1;
  }
  return static_cast<int32_t>(
      std::min<int64_t>(rounded, std::numeric_limits<int32_t>::max()));
}

size_t SubblockFixedHeaderLength(int32_t dimension_count) {
  const size_t dims =
      dimension_count > 0 ? static_cast<size_t>(dimension_count) : 0;
  const size_t entry_size = kDirEntryFixedSize + dims * kDimensionEntrySize;
  // 16 bytes = meta_size[4] + attach_size[4] + data_size[8] preceding the
  // inline directory entry within the segment data section.
  const size_t data_section = std::max(kSubblockDataMinLen, 16 + entry_size);
  return kSegmentHeaderLen + data_section;
}

aifocore::Result<std::vector<uint8_t>> DecompressZstd(
    std::span<const uint8_t> in, size_t expected_size) {
  std::vector<uint8_t> out(expected_size);
  const size_t res =
      ZSTD_decompress(out.data(), out.size(), in.data(), in.size());
  if (ZSTD_isError(res)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("ZSTD_decompress failed: {}",
                              ZSTD_getErrorName(res)));
  }
  if (res != expected_size) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("ZSTD size mismatch: got {}, expected {}", res,
                              expected_size));
  }
  return out;
}

aifocore::Result<Zstd1Payload> ParseZstd1Payload(std::span<const uint8_t> in) {
  if (in.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "zstd1 payload truncated");
  }
  const uint8_t header_len = in[0];
  if (header_len == 1) {
    return Zstd1Payload{.payload = in.subspan(1), .do_hilo = false};
  }
  if (header_len == 3) {
    if (in.size() < 3) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "zstd1 payload truncated (header)");
    }
    const uint8_t chunk_type = in[1];
    const uint8_t flags = in[2];
    if (chunk_type != 1) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Unexpected zstd1 chunk type: {}", chunk_type));
    }
    return Zstd1Payload{.payload = in.subspan(3),
                        .do_hilo = (flags & 1u) != 0u};
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kInvalidArgument,
      aifocore::fmt::format("Unexpected zstd1 header length: {}", header_len));
}

aifocore::Result<std::vector<uint8_t>> UnpackHiLo16(
    std::span<const uint8_t> in) {
  if ((in.size() % 2) != 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "HiLo unpacking requires even byte count");
  }
  const size_t half = in.size() / 2;
  std::vector<uint8_t> out(in.size());
  for (size_t i = 0; i < half; ++i) {
    out[i * 2] = in[i];
    out[i * 2 + 1] = in[half + i];
  }
  return out;
}

std::vector<SceneGroup> GroupSubblocksByScene(
    std::span<const CziSubblockInfo> subblocks) {
  // std::map keeps scene ids ordered ascending; values preserve insertion
  // order, which mirrors the subblock array order.
  std::map<int32_t, std::vector<uint32_t>> by_scene;
  for (const auto& sb : subblocks) {
    by_scene[sb.scene].push_back(sb.index);
  }

  std::vector<SceneGroup> groups;
  groups.reserve(by_scene.size());
  for (auto& [scene_id, indices] : by_scene) {
    groups.push_back(SceneGroup{scene_id, std::move(indices)});
  }
  return groups;
}

std::vector<int32_t> SortedUniqueAxis(std::span<const int32_t> values) {
  std::vector<int32_t> result(values.begin(), values.end());
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

}  // namespace czi
}  // namespace fastslide
