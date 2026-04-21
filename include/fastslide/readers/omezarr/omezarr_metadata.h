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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_METADATA_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_METADATA_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/utilities/colors.h"

namespace fastslide::formats::omezarr {

/// @brief Numeric dtype kind parsed from a Zarr V3 `data_type` string.
enum class ZarrDtypeKind : uint8_t {
  kUInt,
  kInt,
  kFloat,
  kBool,
};

/// @brief Parsed Zarr V3 `data_type` (e.g. "uint16", "float32").
struct ZarrDtype {
  ZarrDtypeKind kind = ZarrDtypeKind::kUInt;
  uint32_t bits = 8;

  /// @brief Bytes per scalar element.
  [[nodiscard]] uint32_t BytesPerElement() const noexcept {
    return (bits + 7u) / 8u;
  }
};

/// @brief One codec in a Zarr V3 codec chain.
///
/// Only the codec name is mandatory; configuration is parsed lazily by the
/// codec implementation. The raw JSON-encoded configuration is preserved so
/// that the decoder can interpret format-specific options without re-loading
/// `zarr.json`.
struct ZarrCodec {
  std::string name;           ///< e.g. "bytes", "zstd", "gzip", "blosc"
  std::string configuration;  ///< Raw JSON-encoded configuration object
};

/// @brief Multiscale axis definition (subset of OME-NGFF v0.5).
struct OmeAxis {
  std::string name;  ///< e.g. "c", "y", "x", "z", "t"
  std::string type;  ///< e.g. "channel", "space", "time"
};

/// @brief Per-level scale transform.
struct OmeMultiscaleDataset {
  std::string path;           ///< Sub-array path (e.g. "0", "s0")
  std::vector<double> scale;  ///< Coordinate scale (one entry per axis)
};

/// @brief OME-NGFF "omero" channel descriptor (subset).
struct OmeroChannel {
  std::string label;              ///< Channel label/name
  std::optional<ColorRGB> color;  ///< Display color (omero `color` hex string)
  std::optional<double> window_start;
  std::optional<double> window_end;
  bool active = true;
};

/// @brief Top-level OME-NGFF metadata combined with the array headers.
struct OmeNgffMetadata {
  std::string version;        ///< OME version ("0.4", "0.5", ...)
  std::vector<OmeAxis> axes;  ///< Multiscales axes
  std::vector<OmeMultiscaleDataset> datasets;  ///< Pyramid level entries
  std::vector<OmeroChannel> channels;          ///< Optional omero channels

  /// @brief Index of the channel ("c") axis if present.
  [[nodiscard]] std::optional<size_t> ChannelAxis() const noexcept;

  /// @brief Index of the "y" axis (required).
  [[nodiscard]] std::optional<size_t> YAxis() const noexcept;

  /// @brief Index of the "x" axis (required).
  [[nodiscard]] std::optional<size_t> XAxis() const noexcept;
};

/// @brief Parsed Zarr V3 array metadata (subset needed for tile reads).
struct ZarrArrayMetadata {
  std::vector<uint64_t> shape;
  std::vector<uint64_t> chunk_shape;
  std::vector<std::string> dimension_names;  ///< May be empty
  std::vector<ZarrCodec> codecs;
  ZarrDtype dtype;
  /// @brief Chunk path separator from `chunk_key_encoding.configuration`.
  /// Defaults to "/" for Zarr V3 default key encoding.
  char chunk_key_separator = '/';
  /// @brief Fill value (raw JSON literal). Most arrays use 0; we just record it.
  double fill_value = 0.0;
};

/// @brief Parser for OME-NGFF v0.4/v0.5 root group metadata and Zarr V3
/// array metadata.
class OmeZarrMetadataParser {
 public:
  /// @brief Parse the JSON contents of the root `zarr.json` group document.
  ///
  /// Accepts both:
  ///   - V3: `{"node_type": "group", "attributes": {"ome": {...}}}` (v0.5)
  ///   - V2-style fallback: top-level `multiscales`/`omero` (v0.4)
  static aifocore::Result<OmeNgffMetadata> ParseRootJson(std::string_view json);

  /// @brief Parse the JSON contents of an array `zarr.json` document.
  static aifocore::Result<ZarrArrayMetadata> ParseArrayJson(
      std::string_view json);

  /// @brief Parse a Zarr V3 `data_type` string (e.g. "uint16", "float32").
  static aifocore::Result<ZarrDtype> ParseDtype(std::string_view dtype);

  /// @brief Parse an OMERO color hex string ("RRGGBB" or "RRGGBBAA").
  static std::optional<ColorRGB> ParseOmeroColor(std::string_view hex);
};

}  // namespace fastslide::formats::omezarr

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OMEZARR_OMEZARR_METADATA_H_
