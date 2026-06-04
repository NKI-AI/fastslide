// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_ASSOCIATED_DATA_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_ASSOCIATED_DATA_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/associated_data.h"
#include "fastslide/image.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"

/// @file mrxs_associated_data.h
/// @brief Helpers for MRXS non-hierarchical associated data records.
///
/// MRXS slides store auxiliary content (label, macro, thumbnails, XML, raw
/// binary blobs, position buffers, ...) inside non-hierarchical records of
/// `Index.dat`. This module isolates the discovery, lookup, and decoding of
/// those records so `MrxsReader` itself can stay focused on the pyramid /
/// tile pipeline.
///
/// All functions are pure with respect to the underlying slide state: they
/// take a `(dirname, SlideDataInfo)` pair (or just `SlideDataInfo` for
/// metadata-only operations) and never mutate it.

namespace fs = std::filesystem;

namespace fastslide {
namespace mrxs {

/// @brief Static helpers for MRXS associated-data records.
///
/// Mirrors the namespace-class pattern used by sibling helpers
/// (`MrxsDataReader`, `MrxsIndexReader`, `MrxsPositionReader`).
class MrxsAssociatedData {
 public:
  /// @brief Return *all* associated-data record names in @p slide_info.
  ///
  /// Includes both the image-prefixed records (`ScanDataLayer_Slide*`) and
  /// the non-image XML / binary records. Names are derived from
  /// `record.value_name`, falling back to `<layer>_<index>` when the value
  /// name is empty.
  [[nodiscard]] static std::vector<std::string> GetNames(
      const SlideDataInfo& slide_info);

  /// @brief Image-only subset of @ref GetNames.
  ///
  /// Filters @ref GetNames to the records whose name starts with
  /// `internal::kAssociatedImagePrefix` and strips that prefix from the
  /// returned values, matching the public
  /// `SlideReader::GetAssociatedImageNames` contract.
  [[nodiscard]] static std::vector<std::string> GetImageNames(
      const SlideDataInfo& slide_info);

  /// @brief Non-image subset of @ref GetNames.
  ///
  /// Returns the records whose name does *not* start with the associated-image
  /// prefix (XML metadata, raw binary blobs, etc.).
  [[nodiscard]] static std::vector<std::string> GetNonImageNames(
      const SlideDataInfo& slide_info);

  /// @brief Look up record metadata by name without loading the payload.
  ///
  /// Size / type / compression fields are filled in only when the data is
  /// actually loaded via @ref Load; this call is metadata-only.
  ///
  /// @param slide_info Slide metadata (provides the non-hierarchical layers).
  /// @param name Record name as returned by @ref GetNames.
  /// @return `AssociatedDataInfo` for the record.
  /// @retval NotFound if no matching record exists.
  [[nodiscard]] static aifocore::Result<AssociatedDataInfo> GetInfo(
      const SlideDataInfo& slide_info, std::string_view name);

  /// @brief Read and decode an associated-data record.
  ///
  /// Resolves the record via the index reader, reads the raw bytes,
  /// transparently inflates zlib-wrapped payloads, sniffs the resulting magic
  /// bytes, and produces an `AssociatedData` of the appropriate variant (image,
  /// XML string, or raw binary). On decompression failure the raw bytes are
  /// returned and a warning is logged.
  ///
  /// @param dirname Path to the MRXS directory.
  /// @param slide_info Slide metadata.
  /// @param name Record name as returned by @ref GetNames.
  [[nodiscard]] static aifocore::Result<AssociatedData> Load(
      const fs::path& dirname, const SlideDataInfo& slide_info,
      std::string_view name);

  /// @brief Decode an `ScanDataLayer_Slide<name>` record as an RGB image.
  ///
  /// Convenience wrapper around @ref Load that re-applies the associated-image
  /// prefix and unwraps the variant.
  [[nodiscard]] static aifocore::Result<RGBImage> ReadImage(
      const fs::path& dirname, const SlideDataInfo& slide_info,
      std::string_view name);

  /// @brief Read and return only the dimensions of an associated image.
  ///
  /// Today this fully decodes the image; kept as a separate entry point so we
  /// can later optimize to a header-only fast path if needed.
  [[nodiscard]] static aifocore::Result<ImageDimensions> ReadImageDimensions(
      const fs::path& dirname, const SlideDataInfo& slide_info,
      std::string_view name);

  /// @brief Sniff the type of a raw associated-data payload.
  ///
  /// Inspects the leading magic bytes (JPEG / PNG / BMP / XML / zlib) and
  /// falls back to a printable-character heuristic for ambiguous text. The
  /// result is `kUnknown` only for empty input.
  [[nodiscard]] static AssociatedDataType DetectDataType(
      const std::vector<uint8_t>& data);
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_ASSOCIATED_DATA_H_
