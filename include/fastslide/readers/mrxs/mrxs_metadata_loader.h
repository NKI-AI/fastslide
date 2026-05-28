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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_METADATA_LOADER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_METADATA_LOADER_H_

#include <filesystem>

#include "aifocore/status/result.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"

/// @file mrxs_metadata_loader.h
/// @brief Facade for assembling a populated `mrxs::SlideDataInfo`.
///
/// `MrxsReader` only needs a *fully assembled* `SlideDataInfo` to construct
/// itself. Historically the reader pulled in three separate helpers
/// (`mrxs_ini_parser`, `mrxs_layer_parser`, `mrxs_position_reader`) to do
/// that, inflating its include fanout. This module hides those steps behind
/// a single entry point so the reader's translation unit only has to know
/// about one helper.
///
/// The loader wraps:
///   1. `mrxs::internal::IniFile::Load` to parse `Slidedat.ini`.
///   2. `mrxs::internal::ParseNonTiledLayers` / `ParseTiledLayers` /
///      `ParseFilterChannels` to populate hierarchical layout.
///   3. `mrxs::MrxsPositionReader::Read` to materialise camera positions and
///      per-position gain values when the slide advertises them.

namespace fs = std::filesystem;

namespace fastslide {
namespace mrxs {

/// @brief Static facade for loading MRXS slide metadata.
///
/// Mirrors the namespace-class pattern used by sibling helpers in this
/// directory.
class MrxsMetadataLoader {
 public:
  /// @brief Parse `Slidedat.ini` and read the camera-position non-hier record.
  ///
  /// Builds a `SlideDataInfo` with everything `MrxsReader` needs to operate:
  /// general slide metadata, the data-file table, hierarchical / non-hier
  /// layer descriptors, fluorescence filter channels, and (when present)
  /// camera positions + per-position gain values.
  ///
  /// @param slidedat_path Absolute path to `<slide>/Slidedat.ini`.
  /// @param dirname Absolute path to the slide directory (`<slide>/`).
  /// @return Populated `SlideDataInfo` or an error status.
  /// @retval NotFound if a referenced data file or position record is missing.
  /// @retval InvalidArgument if a required section / key is missing or
  ///         malformed.
  /// @retval Internal if file I/O fails.
  /// @note Callers must ensure both paths exist; pre-flight existence checks
  ///       belong upstream (see `MrxsReader::ValidateInput`).
  static aifocore::Result<SlideDataInfo> Load(const fs::path& slidedat_path,
                                              const fs::path& dirname);
};

}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_METADATA_LOADER_H_
