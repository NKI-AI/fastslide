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

#include "fastslide/readers/tiff_quickhash.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"

namespace fastslide::readers::tiff_quickhash {
namespace {

aifocore::Status HashNulTerminatedString(QuickHashBuilder& hasher,
                                         std::string_view value) {
  std::vector<uint8_t> buf(value.size() + 1, 0);
  if (!value.empty()) {
    std::memcpy(buf.data(), value.data(), value.size());
  }
  return hasher.HashData(buf);
}

}  // namespace

aifocore::Status HashPageRawCompressedBytes(
    const simpletiff::TiffIndex& tiff_index, uint16_t page,
    const std::filesystem::path& filename, QuickHashBuilder& hasher,
    int64_t max_total_compressed_bytes) {
  (void)filename;
  if (page >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kOutOfRange,
                                "Page out of range");
  }

  const auto& page_header = tiff_index.Page(page);
  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index.Tiles(page_header.payload_id);
    const uint32_t total_tiles = tiles.tiles_x * tiles.tiles_y;

    std::vector<uint8_t> raw_tile;
    int64_t total_bytes = 0;
    for (uint32_t i = 0; i < total_tiles; ++i) {
      auto read_or = simpletiff::ReadRawTile(tiff_index, page, i, raw_tile);
      if (!read_or.ok()) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                    read_or.error().message());
      }
      total_bytes += static_cast<int64_t>(raw_tile.size());
      if (total_bytes > max_total_compressed_bytes) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kOutOfRange,
            "Lowest resolution level too large to hash");
      }
      AIFOCORE_RETURN_IF_ERROR(hasher.HashData(raw_tile));
    }
    return aifocore::Status::OkStatus();
  }
  if (page_header.storage == simpletiff::Storage::kStrips) {
    const auto& strips = tiff_index.Strips(page_header.payload_id);
    uint32_t rows_per_strip = strips.rows_per_strip;
    if (rows_per_strip == 0) {
      rows_per_strip = page_header.height;
    }
    const uint32_t total_strips =
        (page_header.height + rows_per_strip - 1) / rows_per_strip;

    std::vector<uint8_t> raw_strip;
    int64_t total_bytes = 0;
    for (uint32_t i = 0; i < total_strips; ++i) {
      auto read_or = simpletiff::ReadRawTile(tiff_index, page, i, raw_strip);
      if (!read_or.ok()) {
        return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                    read_or.error().message());
      }
      total_bytes += static_cast<int64_t>(raw_strip.size());
      if (total_bytes > max_total_compressed_bytes) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kOutOfRange,
            "Lowest resolution level too large to hash");
      }
      AIFOCORE_RETURN_IF_ERROR(hasher.HashData(raw_strip));
    }
    return aifocore::Status::OkStatus();
  }
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                              "Unsupported TIFF storage for quickhash");
}

void HashTiffProperties(const simpletiff::TiffIndex& tiff_index,
                        QuickHashBuilder& hasher) {
  if (tiff_index.NumPages() == 0) {
    return;
  }
  const auto& page0 = tiff_index.Page(0);

  auto hash_prop = [&](std::string_view name, std::string_view value) {
    (void)HashNulTerminatedString(hasher, name);
    (void)HashNulTerminatedString(hasher, value);
  };

  // OpenSlide property hashing order for TIFF:
  // https://github.com/openslide/openslide/blob/main/src/openslide-decode-tifflike.c
  hash_prop("tiff.ImageDescription", page0.description);
  hash_prop("tiff.Make", "");
  hash_prop("tiff.Model", "");
  hash_prop("tiff.Software", page0.software);
  hash_prop("tiff.DateTime", "");
  hash_prop("tiff.Artist", "");
  hash_prop("tiff.HostComputer", "");
  hash_prop("tiff.Copyright", "");
  hash_prop("tiff.DocumentName", "");
}

}  // namespace fastslide::readers::tiff_quickhash
