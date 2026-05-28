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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_XML_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_XML_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"

/**
 * @file bif_xml.h
 * @brief Parsers for the XMP (TIFF tag 700) metadata embedded in Roche
 *        VENTANA BIF files.
 *
 * Two distinct packets are parsed:
 *  - IFD 0 carries an `<iScan>` element with scanner-level calibration
 *    (`bif::ScannerInfo`).
 *  - The high-resolution (level-0) IFD, in practice IFD 2, carries an
 *    `<EncodeInfo>` element describing the per-AOI stitch geometry
 *    (`bif::EncodeInfo`).
 *
 * The field semantics follow the normative description in
 * `docs/source/formats/bif.rst`, derived from the documented XMP field layout
 * and validated against sample files.
 */

namespace fastslide {
namespace bif {

/// @brief Direction of a tile joint between two neighbouring snapshot tiles.
enum class JointDirection : std::uint8_t {
  kRight,
  kLeft,
  kUp,
  kDown,
  kUnknown,
};

/// @brief A single `<TileJointInfo>` overlap record.
struct TileJoint {
  JointDirection direction = JointDirection::kUnknown;
  int tile1 = 0;  ///< 1-based serpentine index of the first tile.
  int tile2 = 0;  ///< 1-based serpentine index of the second tile.
  double overlap_x = 0.0;
  double overlap_y = 0.0;
  int confidence = 0;
  bool flag_joined =
      false;  ///< `FlagJoined`: this joint was actually measured.
};

/// @brief Per-area-of-interest stitch metadata (`<ImageInfo>` + `<AoiOrigin>`).
struct AoiInfo {
  int aoi_index = 0;
  int num_rows = 0;          ///< R (tile rows in this AOI).
  int num_cols = 0;          ///< C (tile columns in this AOI).
  double tile_width = 0.0;   ///< Per-tile width in pixels (`Width`).
  double tile_height = 0.0;  ///< Per-tile height in pixels (`Height`).
  double pos_x = 0.0;        ///< Stage X origin (pixels, X right).
  double pos_y = 0.0;        ///< Stage Y origin (pixels, Y up).
  double origin_x = 0.0;     ///< Image-space X origin (`AoiOrigin`, px).
  double origin_y = 0.0;     ///< Image-space Y origin (`AoiOrigin`, px).
  std::vector<TileJoint> joints;
};

/// @brief Parsed `<EncodeInfo>` packet (IFD 2).
struct EncodeInfo {
  std::vector<AoiInfo> aois;
};

/// @brief Parsed `<iScan>` scanner calibration (IFD 0).
struct ScannerInfo {
  std::string scanner_model;
  double magnification = 0.0;  ///< Objective power (e.g. 40).
  double scan_res = 0.0;       ///< Microns per pixel.
  int scan_white_point =
      255;  ///< Fill value for unscanned tiles (per channel).
  std::string barcode_1d;
  std::string barcode_2d;
  int z_layers = 1;
};

/// @brief Cheap check for the IFD 0 `iScan` marker (used by content matching).
[[nodiscard]] bool LooksLikeBif(std::string_view xmp_packet);

/// @brief Parse the IFD 0 `<iScan>` packet into a ScannerInfo.
[[nodiscard]] aifocore::Result<ScannerInfo> ParseScannerInfo(
    std::string_view xmp_packet);

/// @brief Parse the level-0 `<EncodeInfo>` packet into per-AOI stitch metadata.
///
/// AOIs flagged `AOIScanned="0"` (failed scans, no on-disk tiles) are skipped.
[[nodiscard]] aifocore::Result<EncodeInfo> ParseEncodeInfo(
    std::string_view xmp_packet);

}  // namespace bif
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_XML_H_
