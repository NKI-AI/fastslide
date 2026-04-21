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

/// @file mrxs_layer_parser.cpp
/// @brief Implementation of MRXS hierarchical/non-hierarchical layer parsing.

#include "fastslide/readers/mrxs/mrxs_layer_parser.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {
namespace mrxs {
namespace internal {

namespace {

// Hierarchical / zoom level keys
constexpr std::string_view kGroupHierarchical = "HIERARCHICAL";
constexpr std::string_view kKeyHierCount = "HIER_COUNT";
constexpr std::string_view kKeyIndexFile = "INDEXFILE";
constexpr std::string_view kValueSlideZoomLevel = "Slide zoom level";
// Some older slides use "Scan data layer_0" or similar
constexpr std::string_view kValueScanDataLayer0 = "Scan data layer_0";
// Fluorescence filter hierarchy is named "Slide filter level" by 3DHISTECH.
constexpr std::string_view kValueSlideFilterLevel = "Slide filter level";

constexpr std::string_view kKeyHierLevelName = "HIER_{}_NAME";
constexpr std::string_view kKeyHierLevelCount = "HIER_{}_COUNT";
constexpr std::string_view kKeyHierLevelValSection = "HIER_{}_VAL_{}_SECTION";

// Filter channel keys (per LAYER_<filter_hier>_LEVEL_<k>_SECTION).
constexpr std::string_view kKeyFilterName = "FILTER_NAME";
constexpr std::string_view kKeyExcitationWavelength = "EXCITATION_WAVELENGTH";
constexpr std::string_view kKeyEmissionWavelength = "EMISSION_WAVELENGTH";
constexpr std::string_view kKeyColorR = "COLOR_R";
constexpr std::string_view kKeyColorG = "COLOR_G";
constexpr std::string_view kKeyColorB = "COLOR_B";
constexpr std::string_view kKeyStoringChannel = "STORING_CHANNEL_NUMBER";
constexpr std::string_view kKeyDataInFilter = "DATA_IN_THIS_FILTER_LEVEL";
constexpr std::string_view kKeyExposureTime = "EXPOSURE_TIME";
constexpr std::string_view kKeyDigitalGain = "DIGITALGAIN";
constexpr std::string_view kKeyIsMasterFilter = "IS_MASTER_FILTER";
constexpr std::string_view kKeyIsStitchingFilter = "IS_STITCHING_FILTER";

// Filter level section template: LAYER_<hier_idx>_LEVEL_<k>_SECTION lookup
// names live under [HIERARCHICAL] using HIER_{}_VAL_{}_SECTION; the section
// itself is a top-level INI section referenced from there.

// Non-hierarchical keys
constexpr std::string_view kKeyNonHierCount = "NONHIER_COUNT";
constexpr std::string_view kKeyNonHierName = "NONHIER_{}_NAME";
constexpr std::string_view kKeyNonHierLevelCount = "NONHIER_{}_COUNT";
constexpr std::string_view kKeyNonHierVal = "NONHIER_{}_VAL_{}";
constexpr std::string_view kKeyNonHierValSection = "NONHIER_{}_VAL_{}_SECTION";

// Per-level metadata keys
constexpr std::string_view kKeyOverlapX = "OVERLAP_X";
constexpr std::string_view kKeyOverlapY = "OVERLAP_Y";
constexpr std::string_view kKeyMppX = "MICROMETER_PER_PIXEL_X";
constexpr std::string_view kKeyMppY = "MICROMETER_PER_PIXEL_Y";
constexpr std::string_view kKeyImageFormat = "IMAGE_FORMAT";
constexpr std::string_view kKeyImageFillColorBgr = "IMAGE_FILL_COLOR_BGR";
constexpr std::string_view kKeyDigitizerWidth = "DIGITIZER_WIDTH";
constexpr std::string_view kKeyDigitizerHeight = "DIGITIZER_HEIGHT";
constexpr std::string_view kKeyImageConcatFactor = "IMAGE_CONCAT_FACTOR";

/// @brief Parse image format string from INI file
///
/// Converts a string representation of image format (from Slidedat.ini) to
/// the corresponding `MrxsImageFormat` enum value.
///
/// @param format_str Format string from INI file ("JPEG", "PNG", "BMP", or
///                   "JPEGXR")
/// @return Result containing parsed format or error
/// @retval InvalidArgument if format string is unknown
aifocore::Result<MrxsImageFormat> ParseImageFormat(
    std::string_view format_str) {
  if (format_str == "JPEG") {
    return MrxsImageFormat::kJpeg;
  }
  if (format_str == "PNG") {
    return MrxsImageFormat::kPng;
  }
  if (format_str == "BMP") {
    return MrxsImageFormat::kBmp;
  }
  if (format_str == "JPEGXR" || format_str == "JPEG-XR" ||
      format_str == "JXR") {
    return MrxsImageFormat::kJpegXr;
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kInvalidArgument,
      aifocore::fmt::format("Unknown image format: {}", format_str));
}

}  // namespace

aifocore::Status ParseTiledLayers(const IniFile& ini,
                                  SlideDataInfo& slide_info) {
  if (!ini.HasSection(kGroupHierarchical)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Missing section: {}", kGroupHierarchical));
  }

  int hier_count;
  AIFOCORE_ASSIGN_OR_RETURN(hier_count,
                            ini.GetInt(kGroupHierarchical, kKeyHierCount));

  // Capture HIER_<i>_COUNT for every hierarchy along with the total
  // number of hierarchies. The hier_counts are informational; the
  // index reader actually allocates `pyramidDepth` slots per hierarchy
  // in the on-disk pointer table, so it  uses `nhierarchies` x
  // pyramidDepth instead.
  slide_info.hier_counts.assign(hier_count, 0);
  slide_info.nhierarchies = hier_count;
  slide_info.filter_hier_index = -1;

  // Find slide zoom level index
  int slide_zoom_level_index = -1;
  for (int i = 0; i < hier_count; ++i) {
    std::string name_key = aifocore::fmt::format(kKeyHierLevelName, i);
    std::string count_key = aifocore::fmt::format(kKeyHierLevelCount, i);
    auto count_or = ini.GetInt(kGroupHierarchical, count_key);
    if (count_or.ok()) {
      slide_info.hier_counts[i] = *count_or;
    }
    auto value = ini.GetString(kGroupHierarchical, name_key);
    if (value.ok()) {
      if (*value == kValueSlideFilterLevel) {
        slide_info.filter_hier_index = i;
      }
      if (slide_zoom_level_index == -1 &&
          (*value == kValueSlideZoomLevel || *value == kValueScanDataLayer0)) {
        slide_zoom_level_index = i;
        // Don't `break` here -- keep scanning so we also pick up the filter
        // hierarchy index and HIER_<i>_COUNT for hierarchies past the zoom
        // level, used by the level-0 multichannel merge in the index reader.
      } else if (*value != kValueSlideZoomLevel &&
                 *value != kValueScanDataLayer0 &&
                 *value != kValueSlideFilterLevel) {
        // TODO(jonasteuwen): Check how different this is and if it can't be
        // merged into the CRTP pattern
        // std::cerr << "Found hierarchical level: " << i << " with name '"
        //           << *value << "'\n";
      }
    }
  }

  if (slide_zoom_level_index == -1) {
    // If not found by name, fall back to the first hierarchical level. Some
    // very old slides do not have the name set correctly.
    if (hier_count > 0) {
      std::cerr
          << "Warning: Could not find 'Slide zoom level' section name. Using "
             "index 0 as fallback.\n";
      slide_zoom_level_index = 0;
    } else {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Cannot find slide zoom level");
    }
  }

  AIFOCORE_ASSIGN_OR_RETURN(slide_info.index_filename,
                            ini.GetString(kGroupHierarchical, kKeyIndexFile));

  std::string zoom_count_key =
      aifocore::fmt::format(kKeyHierLevelCount, slide_zoom_level_index);
  int zoom_levels;
  AIFOCORE_ASSIGN_OR_RETURN(zoom_levels,
                            ini.GetInt(kGroupHierarchical, zoom_count_key));

  std::vector<std::string> section_names;
  for (int i = 0; i < zoom_levels; ++i) {
    std::string key = aifocore::fmt::format(kKeyHierLevelValSection,
                                            slide_zoom_level_index, i);
    std::string section_name;
    AIFOCORE_ASSIGN_OR_RETURN(section_name,
                              ini.GetString(kGroupHierarchical, key));
    section_names.push_back(section_name);
  }

  for (const auto& section : section_names) {
    if (!ini.HasSection(section)) {
      continue;  // Skip if section doesn't exist
    }

    SlideZoomLevel level;
    level.section_name = section;

    AIFOCORE_ASSIGN_OR_RETURN(level.x_overlap_pixels,
                              ini.GetDouble(section, kKeyOverlapX));
    AIFOCORE_ASSIGN_OR_RETURN(level.y_overlap_pixels,
                              ini.GetDouble(section, kKeyOverlapY));
    AIFOCORE_ASSIGN_OR_RETURN(level.mpp_x, ini.GetDouble(section, kKeyMppX));
    AIFOCORE_ASSIGN_OR_RETURN(level.mpp_y, ini.GetDouble(section, kKeyMppY));
    std::string format_str;
    AIFOCORE_ASSIGN_OR_RETURN(format_str,
                              ini.GetString(section, kKeyImageFormat));
    AIFOCORE_ASSIGN_OR_RETURN(level.image_format, ParseImageFormat(format_str));
    auto fill_color = ini.GetInt(section, kKeyImageFillColorBgr);
    level.background_color_rgb =
        fill_color.ok() ? static_cast<uint32_t>(*fill_color) : 0xFFFFFFFF;

    AIFOCORE_ASSIGN_OR_RETURN(level.image_width,
                              ini.GetInt(section, kKeyDigitizerWidth));
    AIFOCORE_ASSIGN_OR_RETURN(level.image_height,
                              ini.GetInt(section, kKeyDigitizerHeight));

    auto concat_exp = ini.GetInt(section, kKeyImageConcatFactor);
    level.downsample_exponent = concat_exp.ok() ? *concat_exp : 0;

    slide_info.zoom_levels.push_back(level);
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status ParseNonTiledLayers(const IniFile& ini,
                                     SlideDataInfo& slide_info) {
  // NONHIER_COUNT is optional - older MRXS files may omit it.
  auto nonhier_count_or = ini.GetInt(kGroupHierarchical, kKeyNonHierCount);
  if (!nonhier_count_or.ok()) {
    // No nonhier sections - use synthetic positions (valid for older slides)
    slide_info.using_synthetic_positions = true;
    return aifocore::Status::OkStatus();
  }

  const int nonhier_count = *nonhier_count_or;

  slide_info.nonhier_layers.clear();
  int record_offset = 0;

  bool found_position_layer = false;

  for (int i = 0; i < nonhier_count; ++i) {
    NonHierarchicalLayer layer;

    std::string name_key = aifocore::fmt::format(kKeyNonHierName, i);
    auto layer_name_or = ini.GetString(kGroupHierarchical, name_key);
    if (!layer_name_or.ok()) {
      // Missing layer name - skip this entry rather than fail outright, since
      // older / malformed slides occasionally drop names.
      std::cerr << "Warning: Missing name for non-hierarchical layer " << i
                << ". Skipping.\n";
      continue;
    }
    layer.name = *layer_name_or;

    std::string count_key = aifocore::fmt::format(kKeyNonHierLevelCount, i);
    AIFOCORE_ASSIGN_OR_RETURN(layer.count,
                              ini.GetInt(kGroupHierarchical, count_key));
    layer.record_offset = record_offset;

    for (int j = 0; j < layer.count; ++j) {
      NonHierarchicalRecord record;
      record.layer_name = layer.name;
      record.record_index = record_offset + j;
      record.layer_index = j;

      std::string val_key = aifocore::fmt::format(kKeyNonHierVal, i, j);
      auto val = ini.GetString(kGroupHierarchical, val_key);
      if (val.ok()) {
        record.value_name = *val;
      }

      std::string section_key =
          aifocore::fmt::format(kKeyNonHierValSection, i, j);
      auto section = ini.GetString(kGroupHierarchical, section_key);
      if (section.ok()) {
        record.section_name = *section;
      }

      layer.records.push_back(record);
    }

    if (layer.name == "VIMSLIDE_POSITION_BUFFER") {
      slide_info.position_layer_name = layer.name;
      slide_info.position_layer_record_offset = record_offset;
      slide_info.position_layer_compressed = false;
      found_position_layer = true;
    } else if (layer.name == "StitchingIntensityLayer") {
      slide_info.position_layer_name = layer.name;
      slide_info.position_layer_record_offset = record_offset;
      slide_info.position_layer_compressed = true;
      found_position_layer = true;
    }

    slide_info.nonhier_layers.push_back(layer);
    record_offset += layer.count;
  }

  slide_info.using_synthetic_positions = !found_position_layer;

  return aifocore::Status::OkStatus();
}

aifocore::Status ParseFilterChannels(const IniFile& ini,
                                     SlideDataInfo& slide_info) {
  slide_info.filters.clear();

  if (!ini.HasSection(kGroupHierarchical)) {
    return aifocore::Status::OkStatus();  // brightfield slides won't have it
  }

  auto hier_count_or = ini.GetInt(kGroupHierarchical, kKeyHierCount);
  if (!hier_count_or.ok()) {
    return aifocore::Status::OkStatus();
  }
  const int hier_count = *hier_count_or;

  // Prefer the cached `filter_hier_index` populated by ParseTiledLayers so
  // both parsers agree on which hierarchy holds the filter levels.
  int filter_hier_index = slide_info.filter_hier_index;
  if (filter_hier_index < 0) {
    for (int i = 0; i < hier_count; ++i) {
      std::string name_key = aifocore::fmt::format(kKeyHierLevelName, i);
      auto name_or = ini.GetString(kGroupHierarchical, name_key);
      if (name_or.ok() && *name_or == kValueSlideFilterLevel) {
        filter_hier_index = i;
        slide_info.filter_hier_index = i;
        break;
      }
    }
  }

  if (filter_hier_index < 0) {
    return aifocore::Status::OkStatus();  // not a fluorescence slide
  }

  std::string count_key =
      aifocore::fmt::format(kKeyHierLevelCount, filter_hier_index);
  int filter_count;
  AIFOCORE_ASSIGN_OR_RETURN(filter_count,
                            ini.GetInt(kGroupHierarchical, count_key));

  for (int k = 0; k < filter_count; ++k) {
    std::string section_key =
        aifocore::fmt::format(kKeyHierLevelValSection, filter_hier_index, k);
    auto section_or = ini.GetString(kGroupHierarchical, section_key);
    if (!section_or.ok() || !ini.HasSection(*section_or)) {
      continue;
    }
    const std::string& section = *section_or;

    FilterChannel ch;
    ch.index = k;

    auto name_or = ini.GetString(section, kKeyFilterName);
    if (name_or.ok())
      ch.name = *name_or;

    auto ex_or = ini.GetDouble(section, kKeyExcitationWavelength);
    if (ex_or.ok())
      ch.excitation_wavelength = *ex_or;
    auto em_or = ini.GetDouble(section, kKeyEmissionWavelength);
    if (em_or.ok())
      ch.emission_wavelength = *em_or;

    auto cr = ini.GetInt(section, kKeyColorR);
    auto cg = ini.GetInt(section, kKeyColorG);
    auto cb = ini.GetInt(section, kKeyColorB);
    ch.color_rgb = {cr.ok() ? static_cast<uint8_t>(*cr) : uint8_t{0},
                    cg.ok() ? static_cast<uint8_t>(*cg) : uint8_t{0},
                    cb.ok() ? static_cast<uint8_t>(*cb) : uint8_t{0}};

    auto storing_or = ini.GetInt(section, kKeyStoringChannel);
    if (storing_or.ok())
      ch.storing_channel = *storing_or;

    auto data_filter_or = ini.GetString(section, kKeyDataInFilter);
    if (data_filter_or.ok())
      ch.data_filter_level = *data_filter_or;

    auto exposure_or = ini.GetInt(section, kKeyExposureTime);
    if (exposure_or.ok())
      ch.exposure_time_us = *exposure_or;
    auto gain_or = ini.GetInt(section, kKeyDigitalGain);
    if (gain_or.ok())
      ch.digital_gain = *gain_or;

    auto master_or = ini.GetString(section, kKeyIsMasterFilter);
    if (master_or.ok()) {
      ch.is_master = (*master_or == "True" || *master_or == "1");
    }
    auto stitch_or = ini.GetString(section, kKeyIsStitchingFilter);
    if (stitch_or.ok()) {
      ch.is_stitching = (*stitch_or == "True" || *stitch_or == "1");
    }

    slide_info.filters.push_back(std::move(ch));
  }

  return aifocore::Status::OkStatus();
}

}  // namespace internal
}  // namespace mrxs
}  // namespace fastslide
