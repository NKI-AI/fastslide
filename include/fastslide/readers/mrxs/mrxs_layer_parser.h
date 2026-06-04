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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_LAYER_PARSER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_LAYER_PARSER_H_

/// @file mrxs_layer_parser.h
/// @brief INI-driven parsers for MRXS hierarchical and non-hierarchical layers.
///
/// These helpers populate `mrxs::SlideDataInfo` from a parsed
/// `mrxs::internal::IniFile`. They perform no I/O beyond what the IniFile
/// already loaded; tile data is fetched lazily by the rest of the reader.

#include "aifocore/status/result.h"
#include "fastslide/readers/mrxs/mrxs_ini_parser.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"

namespace fastslide {
namespace mrxs {
namespace internal {

/// @brief Parse tiled (hierarchical/zoom) layers from Slidedat.ini
///
/// Lazy-loads metadata about the multi-resolution pyramid levels without
/// reading any actual tile data from disk. Each zoom level contains
/// information about tile dimensions, overlaps, compression format, and MPP
/// values.
///
/// This routine only reads INI metadata - actual tile indices are read later
/// via `MrxsReader::ReadLevelTiles()` when needed for spatial indexing or
/// region reading.
///
/// @param ini Parsed INI file
/// @param slide_info Slide information to update with tiled layers
/// @return OkStatus on success or error status
/// @retval InvalidArgument if required keys are missing or invalid
aifocore::Status ParseTiledLayers(const IniFile& ini,
                                  SlideDataInfo& slide_info);

/// @brief Parse non-tiled (non-hierarchical) layer metadata from Slidedat.ini
///
/// Lazy-loads metadata about associated data (label, macro, thumbnails,
/// position data, etc.) without reading the actual data from disk. The data
/// is loaded on demand via `MrxsReader::LoadAssociatedData()` or
/// `ReadCameraPositions()`.
///
/// Additionally detects and stores position layer information
/// (VIMSLIDE_POSITION_BUFFER or StitchingIntensityLayer) and sets the
/// `using_synthetic_positions` flag accordingly.
///
/// @param ini Parsed INI file
/// @param slide_info Slide information to update with non-tiled metadata
/// @return OkStatus on success or error status
aifocore::Status ParseNonTiledLayers(const IniFile& ini,
                                     SlideDataInfo& slide_info);

/// @brief Parse fluorescence filter channel metadata from Slidedat.ini
///
/// Locates the hierarchical level whose `HIER_<i>_NAME` is "Slide filter
/// level" (the convention used by 3DHISTECH for fluorescence slides) and
/// populates `slide_info.filters` from the corresponding
/// `LAYER_<i>_LEVEL_<k>_SECTION` entries.
///
/// Safe to call on brightfield slides; in that case `slide_info.filters`
/// will simply be left empty.
///
/// @param ini Parsed INI file
/// @param slide_info Slide information to update with filter metadata
/// @return OkStatus on success or a soft warning on missing optional fields
aifocore::Status ParseFilterChannels(const IniFile& ini,
                                     SlideDataInfo& slide_info);

}  // namespace internal
}  // namespace mrxs
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_LAYER_PARSER_H_
