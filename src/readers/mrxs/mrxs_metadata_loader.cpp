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

/// @file mrxs_metadata_loader.cpp
/// @brief Implementation of the `MrxsMetadataLoader` facade.

#include "fastslide/readers/mrxs/mrxs_metadata_loader.h"

#include <string>
#include <string_view>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/mrxs/mrxs_ini_parser.h"
#include "fastslide/readers/mrxs/mrxs_layer_parser.h"
#include "fastslide/readers/mrxs/mrxs_position_reader.h"
#include "fastslide/runtime/io/path_utils.h"

namespace fastslide {
namespace mrxs {
namespace {

// INI sections / keys handled directly by the loader. Hierarchical and
// non-hierarchical layer keys live in `mrxs_layer_parser.cpp`.
constexpr std::string_view kGroupGeneral = "GENERAL";
constexpr std::string_view kKeySlideId = "SLIDE_ID";
constexpr std::string_view kKeyImageNumberX = "IMAGENUMBER_X";
constexpr std::string_view kKeyImageNumberY = "IMAGENUMBER_Y";
constexpr std::string_view kKeyObjectiveMagnification =
    "OBJECTIVE_MAGNIFICATION";
constexpr std::string_view kKeyCameraImageDivisionsPerSide =
    "CameraImageDivisionsPerSide";
constexpr std::string_view kKeySlideType = "SLIDE_TYPE";
constexpr std::string_view kKeySlideBitDepth = "VIMSLIDE_SLIDE_BITDEPTH";
constexpr std::string_view kSlideTypeFluorescence = "SLIDE_TYPE_FLUORESCENCE";

constexpr std::string_view kGroupDatafile = "DATAFILE";
constexpr std::string_view kKeyFileCount = "FILE_COUNT";
constexpr std::string_view kKeyDataFile = "FILE_{}";

/// @brief Parse the mandatory `[GENERAL]` section into @p info.
aifocore::Status ParseGeneralSection(const internal::IniFile& ini,
                                     SlideDataInfo& info) {
  if (!ini.HasSection(kGroupGeneral)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Missing section: {}", kGroupGeneral));
  }

  AIFOCORE_ASSIGN_OR_RETURN(info.slide_id,
                            ini.GetString(kGroupGeneral, kKeySlideId));
  AIFOCORE_ASSIGN_OR_RETURN(info.images_x,
                            ini.GetInt(kGroupGeneral, kKeyImageNumberX));
  AIFOCORE_ASSIGN_OR_RETURN(info.images_y,
                            ini.GetInt(kGroupGeneral, kKeyImageNumberY));
  AIFOCORE_ASSIGN_OR_RETURN(
      info.objective_magnification,
      ini.GetInt(kGroupGeneral, kKeyObjectiveMagnification));

  // Optional: camera image divisions defaults to 1 when absent.
  auto image_divisions =
      ini.GetInt(kGroupGeneral, kKeyCameraImageDivisionsPerSide);
  info.image_divisions = image_divisions.ok() ? *image_divisions : 1;

  // Optional: slide modality + camera bit depth.
  auto slide_type_or = ini.GetString(kGroupGeneral, kKeySlideType);
  if (slide_type_or.ok()) {
    info.slide_type_raw = *slide_type_or;
    if (*slide_type_or == kSlideTypeFluorescence) {
      info.slide_type = MrxsSlideType::kFluorescence;
    }
  }
  auto bitdepth_or = ini.GetInt(kGroupGeneral, kKeySlideBitDepth);
  if (bitdepth_or.ok()) {
    info.camera_bitdepth = *bitdepth_or;
  }
  return aifocore::Status::OkStatus();
}

/// @brief Parse the mandatory `[DATAFILE]` section into @p info.
aifocore::Status ParseDatafileSection(const internal::IniFile& ini,
                                      SlideDataInfo& info) {
  if (!ini.HasSection(kGroupDatafile)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Missing section: {}", kGroupDatafile));
  }

  int file_count;
  AIFOCORE_ASSIGN_OR_RETURN(file_count,
                            ini.GetInt(kGroupDatafile, kKeyFileCount));

  for (int i = 0; i < file_count; ++i) {
    std::string key = aifocore::fmt::format(kKeyDataFile, i);
    std::string file_path;
    AIFOCORE_ASSIGN_OR_RETURN(file_path, ini.GetString(kGroupDatafile, key));
    info.datafile_paths.push_back(file_path);
  }
  return aifocore::Status::OkStatus();
}

/// @brief Parse all sections of `Slidedat.ini` into a fresh `SlideDataInfo`.
aifocore::Result<SlideDataInfo> ReadSlidedatIni(const fs::path& slidedat_path,
                                                const fs::path& dirname) {
  internal::IniFile ini;
  AIFOCORE_ASSIGN_OR_RETURN(ini, internal::IniFile::Load(slidedat_path));

  SlideDataInfo info;
  info.dirname = dirname.string();  // Used as cache key.

  AIFOCORE_RETURN_IF_ERROR(ParseGeneralSection(ini, info));

  // Parse non-tiled (non-hierarchical) layers before tiled ones; the latter
  // depends on data already populated here.
  AIFOCORE_RETURN_IF_ERROR(internal::ParseNonTiledLayers(ini, info));
  AIFOCORE_RETURN_IF_ERROR(internal::ParseTiledLayers(ini, info));
  // No-op for brightfield slides.
  AIFOCORE_RETURN_IF_ERROR(internal::ParseFilterChannels(ini, info));

  AIFOCORE_RETURN_IF_ERROR(ParseDatafileSection(ini, info));
  return info;
}

/// @brief Reject INI-supplied filenames that point outside the slide directory.
///
/// `FILE_n` and `INDEXFILE` are joined onto the slide directory at several
/// points during reading, and their contents are returned to the caller as
/// tile or associated-image bytes. Validating them here, once, keeps every
/// downstream join safe.
aifocore::Status ValidateReferencedPaths(const fs::path& dirname,
                                         const SlideDataInfo& info) {
  for (const std::string& datafile : info.datafile_paths) {
    AIFOCORE_RETURN_IF_ERROR(
        runtime::io::ResolveContainedPath(dirname, datafile).status());
  }
  if (!info.index_filename.empty()) {
    AIFOCORE_RETURN_IF_ERROR(
        runtime::io::ResolveContainedPath(dirname, info.index_filename)
            .status());
  }
  return aifocore::Status::OkStatus();
}

}  // namespace

aifocore::Result<SlideDataInfo> MrxsMetadataLoader::Load(
    const fs::path& slidedat_path, const fs::path& dirname) {
  SlideDataInfo info;
  AIFOCORE_ASSIGN_OR_RETURN(info, ReadSlidedatIni(slidedat_path, dirname));
  AIFOCORE_RETURN_IF_ERROR(ValidateReferencedPaths(dirname, info));

  // IMPORTANT: Camera positions MUST be loaded during initialization;
  // they are required for accurate tile positioning at read time.
  AIFOCORE_RETURN_IF_ERROR(MrxsPositionReader::Read(dirname, info));
  return info;
}

}  // namespace mrxs
}  // namespace fastslide
