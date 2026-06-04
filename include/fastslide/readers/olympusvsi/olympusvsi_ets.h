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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_ETS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_ETS_H_

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"

namespace fastslide::formats::olympusvsi {

/// @brief Declared tile compression id in the ETS header.
///
/// On-disk values follow the Olympus codec enumeration: ``0`` is RAW
/// (uncompressed pixel bytes), ``2`` is baseline JPEG, ``3`` is JPEG 2000.
enum class TileCodec : uint32_t {
  kUnknown = 0xFFFFFFFFu,  ///< Internal sentinel when magic does not match.
  kRaw = 0,
  kJpeg = 2,
  kJp2 = 3,
};

/// @brief Per-component sample type in the ETS header.
enum class TilePixelType : uint32_t {
  kUInt8 = 2,
  kUInt16 = 4,
};

/// @brief Top-level SIS container header (64 bytes).
struct SisHeader {
  uint32_t header_size = 0;  ///< Always 64.
  uint32_t version = 0;
  uint32_t ndim = 0;
  uint64_t ets_offset = 0;  ///< Byte offset of the embedded ETS header.
  uint32_t ets_nbytes = 0;
  uint64_t tiles_offset = 0;  ///< Byte offset of the tile table.
  uint32_t n_tiles = 0;
};

/// @brief Embedded ETS image header (40 bytes prefix + background pad).
struct EtsHeader {
  uint32_t version = 0;
  TilePixelType pixel_type = TilePixelType::kUInt8;
  uint32_t n_channels = 0;
  uint32_t color_space = 0;
  TileCodec compression = TileCodec::kUnknown;
  uint32_t quality = 0;
  uint32_t tile_w = 0;
  uint32_t tile_h = 0;
  uint32_t tile_d = 0;  ///< Only 1 is supported.
  /// @brief Per-component background fill value (up to 4 components).
  std::array<int32_t, 4> background = {0, 0, 0, 0};
};

/// @brief One tile record. Packed size depends on the SIS-declared
/// dimensionality ``ndim``: 36 B for ``ndim=4`` (brightfield x, y,
/// channel, level), 40 B for ``ndim=5`` (fluorescence x, y, channel,
/// Z, level), etc.
///
/// Coordinates `x`, `y`, `channel`, `level` are tile-grid indices (not
/// pixel coordinates). Any "extra" intermediate dims (typically Z or
/// time-point indices when ``ndim > 4``) are summed into
/// ``extra_dim_sum`` so the pyramid builder can drop non-principal
/// slices in one comparison.
struct TileRecord {
  uint32_t const_marker = 0;  ///< Must equal SIS.ndim for every record.
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t channel = 0;
  uint32_t level = 0;
  /// @brief Sum of the intermediate dim values between ``channel`` and
  ///        ``level`` (Z / time-point / position slots). Zero for the
  ///        principal slice; non-zero records are skipped by the
  ///        pyramid builder.
  uint32_t extra_dim_sum = 0;
  uint64_t offset = 0;        ///< Byte offset of the compressed tile bytes.
  uint32_t n_bytes = 0;       ///< Size of the compressed tile bytes.
  uint32_t trailing_pad = 0;  ///< Reserved; zero in validated files.
};

/// @brief Parsed contents of a single frame_t.ets file.
struct EtsFileData {
  std::filesystem::path path;
  SisHeader sis;
  EtsHeader ets;
  std::vector<TileRecord> tiles;
};

/// @brief Match compressed-tile file magic at the start of a tile payload.
///
/// Returns `kJpeg` for a JPEG SOI marker, `kJp2` for a JPEG 2000 codestream
/// or JP2 box prefix, else `kUnknown`.
TileCodec SniffCodec(std::span<const uint8_t> buf);

/// @brief Parse a frame_t.ets file from disk.
///
/// Performs magic-byte validation and layout self-checks (const-marker
/// invariant, tile bounds, first-tile magic vs declared compression).
[[nodiscard]] aifocore::Result<EtsFileData> ParseEtsFile(
    const std::filesystem::path& path);

/// @brief Parse a frame_t.ets buffer (e.g. for unit tests).
///
/// Same validation as `ParseEtsFile`, but reads from an in-memory buffer.
/// `path` is stored on the returned struct purely for diagnostics.
[[nodiscard]] aifocore::Result<EtsFileData> ParseEtsBuffer(
    std::span<const uint8_t> buffer, std::filesystem::path path = {});

/// @brief Convert an on-disk codec id to a human-readable name.
[[nodiscard]] std::string_view CodecName(TileCodec codec);

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_ETS_H_
