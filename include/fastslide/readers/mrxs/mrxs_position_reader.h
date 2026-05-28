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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_POSITION_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_POSITION_READER_H_

#include <filesystem>

#include "aifocore/status/result.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"

/// @file mrxs_position_reader.h
/// @brief Reader for MRXS camera position and gain metadata.
///
/// MRXS scanners capture overlapping photos at discrete camera positions and
/// store the per-position pixel coordinates (and optionally an intensity gain
/// per position) in a non-hierarchical record of the index file. This helper
/// isolates the binary-format parsing for those records so `MrxsReader` does
/// not need to embed the full layout.
///
/// Two storage variants are handled transparently:
///   1. `VIMSLIDE_POSITION_BUFFER` (older slides) - uncompressed.
///   2. `StitchingIntensityLayer` (newer slides) - DEFLATE/zlib compressed
///      (magic `0x78 0x9C`), optionally followed by a second item carrying
///      per-position float gain values.
///
/// The reader populates `SlideDataInfo::camera_positions` and
/// `SlideDataInfo::camera_position_gains` and clears
/// `using_synthetic_positions` on success. If the slide already declared
/// synthetic positions, the call is a no-op.

namespace fs = std::filesystem;

namespace fastslide {
namespace mrxs {

/// @brief Reader for MRXS camera position buffers.
///
/// All methods are static; the class is purely a namespace-like grouping that
/// matches the pattern used by sibling helpers (`MrxsDataReader`,
/// `MrxsIndexReader`, `MrxsPlanBuilder`, ...).
class MrxsPositionReader {
 public:
  /// @brief Read camera positions (and gains, when present) into @p slide_info.
  ///
  /// Inspects `slide_info.position_layer_record_offset` and friends - which
  /// must have been populated by the INI parser - and decodes the matching
  /// non-hierarchical record from the slide's `Index.dat` and data files.
  ///
  /// @param dirname Path to the MRXS directory (the `.mrxs` stem).
  /// @param slide_info Slide metadata to update; mutated on success.
  /// @return OkStatus on success, error status on failure.
  /// @retval NotFound if the index file cannot be opened.
  /// @retval Internal if a file read fails.
  /// @retval InvalidArgument if the on-disk record is malformed.
  /// @note Bad gain metadata is logged once and ignored; positions still
  ///       succeed in that case.
  /// @note `using_synthetic_positions` is left untouched (true) when no
  ///       position layer was advertised in `Slidedat.ini`.
  static aifocore::Status Read(const fs::path& dirname,
                               SlideDataInfo& slide_info);
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_POSITION_READER_H_
