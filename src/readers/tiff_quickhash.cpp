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
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
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

/// @brief Number of tiles or strips holding @p page_header's pixel data.
uint32_t ChunkCount(const simpletiff::TiffIndex& tiff_index,
                    const simpletiff::PageHeader& page_header) {
  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index.Tiles(page_header.payload_id);
    return tiles.tiles_x * tiles.tiles_y;
  }
  const auto& strips = tiff_index.Strips(page_header.payload_id);
  const uint32_t rows_per_strip =
      strips.rows_per_strip > 0 ? strips.rows_per_strip : page_header.height;
  if (rows_per_strip == 0) {
    return 0;
  }
  return (page_header.height + rows_per_strip - 1) / rows_per_strip;
}

}  // namespace

aifocore::Status HashPageRawCompressedBytes(
    const simpletiff::TiffIndex& tiff_index, uint32_t page,
    QuickHashBuilder& hasher) {
  if (page >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format("Page {} out of range ({} pages)", page,
                              tiff_index.NumPages()));
  }

  const auto& page_header = tiff_index.Page(page);
  if (page_header.storage != simpletiff::Storage::kTiles &&
      page_header.storage != simpletiff::Storage::kStrips) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format(
            "Page {} is neither tiled nor stripped, so it has no raw bytes to "
            "hash",
            page));
  }

  const uint32_t chunk_count = ChunkCount(tiff_index, page_header);
  if (chunk_count == 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Page {} holds no tile or strip data", page));
  }

  std::vector<uint8_t> raw_chunk;
  for (uint32_t i = 0; i < chunk_count; ++i) {
    auto read_or = simpletiff::ReadRawTile(tiff_index, page, i, raw_chunk);
    if (!read_or.ok()) {
      // Skipping a chunk would silently change the digest, so a read failure
      // has to abort the whole hash.
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInternal,
          aifocore::fmt::format("Cannot read chunk {} of page {}: {}", i, page,
                                read_or.error().message()));
    }
    AIFOCORE_RETURN_IF_ERROR(hasher.HashData(raw_chunk));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status HashTiffProperties(const simpletiff::TiffIndex& tiff_index,
                                    uint32_t property_page,
                                    QuickHashBuilder& hasher) {
  if (property_page >= tiff_index.NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format("Property page {} out of range ({} pages)",
                              property_page, tiff_index.NumPages()));
  }
  const auto& page = tiff_index.Page(property_page);

  auto hash_prop = [&](std::string_view name,
                       std::string_view value) -> aifocore::Status {
    AIFOCORE_RETURN_IF_ERROR(HashNulTerminatedString(hasher, name));
    return HashNulTerminatedString(hasher, value);
  };

  // Order matches OpenSlide's store_and_hash_properties() in
  // openslide-decode-tifflike.c. An absent tag hashes as an empty value, which
  // is what OpenSlide does when the tag lookup returns NULL.
  AIFOCORE_RETURN_IF_ERROR(
      hash_prop("tiff.ImageDescription", page.description));
  AIFOCORE_RETURN_IF_ERROR(hash_prop("tiff.Make", page.make));
  AIFOCORE_RETURN_IF_ERROR(hash_prop("tiff.Model", page.model));
  AIFOCORE_RETURN_IF_ERROR(hash_prop("tiff.Software", page.software));
  AIFOCORE_RETURN_IF_ERROR(hash_prop("tiff.DateTime", page.date_time));
  AIFOCORE_RETURN_IF_ERROR(hash_prop("tiff.Artist", page.artist));
  AIFOCORE_RETURN_IF_ERROR(hash_prop("tiff.HostComputer", page.host_computer));
  AIFOCORE_RETURN_IF_ERROR(hash_prop("tiff.Copyright", page.copyright));
  return hash_prop("tiff.DocumentName", page.document_name);
}

aifocore::Result<std::string> Compute(const Spec& spec) {
  if (spec.index == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Quickhash spec names no TIFF index");
  }
  if (spec.level_pages.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Quickhash spec names no pages to hash");
  }

  QuickHashBuilder hasher;
  for (const uint32_t page : spec.level_pages) {
    AIFOCORE_RETURN_IF_ERROR(
        HashPageRawCompressedBytes(*spec.index, page, hasher));
  }
  AIFOCORE_RETURN_IF_ERROR(
      HashTiffProperties(*spec.index, spec.property_page, hasher));

  return hasher.Finalize();
}

}  // namespace fastslide::readers::tiff_quickhash
