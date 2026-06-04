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

#include "fastslide/readers/philipstiff/philipstiff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/philipstiff/philipstiff_exec_context.h"
#include "fastslide/readers/philipstiff/philipstiff_plan_builder.h"
#include "fastslide/readers/philipstiff/philipstiff_plan_context.h"
#include "fastslide/readers/philipstiff/philipstiff_tile_executor.h"
#include "fastslide/readers/tiff_quickhash.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/hash.h"
#include "pugixml.hpp"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fs = std::filesystem;

namespace fastslide {

namespace {

aifocore::Result<std::pair<double, double>> ParsePixelSpacing(
    std::string_view spacing_text) {
  std::string normalized(spacing_text);
  for (char& character : normalized) {
    if (character == '"') {
      character = ' ';
    }
  }
  // Also handle &quot; if present.
  for (;;) {
    const size_t pos = normalized.find("&quot;");
    if (pos == std::string::npos) {
      break;
    }
    normalized.replace(pos, 6, " ");
  }

  const char* ptr = normalized.c_str();
  char* end_ptr = nullptr;
  const double first = std::strtod(ptr, &end_ptr);
  if (end_ptr == ptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Could not parse pixel spacing");
  }
  ptr = end_ptr;
  const double second = std::strtod(ptr, &end_ptr);
  if (end_ptr == ptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Could not parse pixel spacing");
  }
  return std::make_pair(second, first);
}

aifocore::Result<pugi::xml_document> ParsePhilipsXml(
    std::string_view xml_text) {
  pugi::xml_document doc;
  const std::string xml_string(xml_text);
  const pugi::xml_parse_result parse_result =
      doc.load_string(xml_string.c_str());
  if (!parse_result) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Failed to parse Philips XML ImageDescription");
  }
  const pugi::xml_node root = doc.document_element();
  if (std::string_view(root.name()) != "DataObject") {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Philips XML root tag is not DataObject");
  }
  const pugi::xml_attribute root_type = root.attribute("ObjectType");
  if (root_type.empty() ||
      std::string_view(root_type.value()) != "DPUfsImport") {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Philips XML DataObject is not DPUfsImport");
  }
  return doc;
}

pugi::xml_node FindChildAttributeByName(pugi::xml_node parent,
                                        std::string_view name_value) {
  for (pugi::xml_node attr = parent.child("Attribute"); !attr.empty();
       attr = attr.next_sibling("Attribute")) {
    const pugi::xml_attribute name_attr = attr.attribute("Name");
    if (!name_attr.empty() &&
        std::string_view(name_attr.value()) == name_value) {
      return attr;
    }
  }
  return {};
}

pugi::xml_node FindWsiScannedImage(pugi::xml_node root) {
  const pugi::xml_node scanned_images_attr =
      FindChildAttributeByName(root, "PIM_DP_SCANNED_IMAGES");
  if (scanned_images_attr.empty()) {
    return {};
  }
  const pugi::xml_node scanned_array = scanned_images_attr.child("Array");
  if (scanned_array.empty()) {
    return {};
  }
  for (pugi::xml_node data_object = scanned_array.child("DataObject");
       !data_object.empty();
       data_object = data_object.next_sibling("DataObject")) {
    const pugi::xml_node image_type_attr =
        FindChildAttributeByName(data_object, "PIM_DP_IMAGE_TYPE");
    if (image_type_attr.empty()) {
      continue;
    }
    const std::string_view text = image_type_attr.text().get();
    if (text == "WSI") {
      return data_object;
    }
  }
  return {};
}

aifocore::Result<std::vector<std::pair<double, double>>>
ExtractPixelDataRepresentationSpacings(pugi::xml_node wsi_scanned_image) {
  const pugi::xml_node pixel_data_seq_attr = FindChildAttributeByName(
      wsi_scanned_image, "PIIM_PIXEL_DATA_REPRESENTATION_SEQUENCE");
  if (pixel_data_seq_attr.empty()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        "Missing PIIM_PIXEL_DATA_REPRESENTATION_SEQUENCE");
  }
  const pugi::xml_node pixel_array = pixel_data_seq_attr.child("Array");
  if (pixel_array.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Missing PixelDataRepresentation Array");
  }

  std::vector<std::pair<double, double>> spacings;
  for (pugi::xml_node repr = pixel_array.child("DataObject"); !repr.empty();
       repr = repr.next_sibling("DataObject")) {
    const pugi::xml_attribute object_type = repr.attribute("ObjectType");
    if (object_type.empty() ||
        std::string_view(object_type.value()) != "PixelDataRepresentation") {
      continue;
    }
    const pugi::xml_node spacing_attr =
        FindChildAttributeByName(repr, "DICOM_PIXEL_SPACING");
    if (spacing_attr.empty()) {
      continue;
    }
    const std::string_view spacing_text = spacing_attr.text().get();
    if (spacing_text.empty()) {
      continue;
    }
    AIFOCORE_ASSIGN_OR_RETURN(auto spacing, ParsePixelSpacing(spacing_text));
    if (spacing.first > 0.0 && spacing.second > 0.0) {
      spacings.push_back(spacing);
    }
  }
  if (spacings.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No Philips DICOM_PIXEL_SPACING entries found");
  }
  return spacings;
}

aifocore::Result<std::vector<std::pair<double, double>>>
ExtractLevelPixelSpacingsFromXml(std::string_view xml_text) {
  AIFOCORE_ASSIGN_OR_RETURN(auto doc, ParsePhilipsXml(xml_text));
  const pugi::xml_node root = doc.document_element();
  const pugi::xml_node wsi_scanned_image = FindWsiScannedImage(root);
  if (wsi_scanned_image.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Missing WSI scanned image in XML");
  }
  return ExtractPixelDataRepresentationSpacings(wsi_scanned_image);
}

}  // namespace

aifocore::Result<std::unique_ptr<PhilipsTiffReader>> PhilipsTiffReader::Create(
    const fs::path& filename) {
  return CreateImpl(filename);
}

PhilipsTiffReader::PhilipsTiffReader(const fs::path& filename)
    : TiffBasedReader(filename) {
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd_val = -1;
  (void)simpletiff::OpenTiff(filename.string(), *tiff_index_, fd_val);
}

int PhilipsTiffReader::GetLevelCount() const {
  return static_cast<int>(pyramid_levels_.size());
}

aifocore::Result<LevelInfo> PhilipsTiffReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= static_cast<int>(pyramid_levels_.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }
  const auto& tiff_level = pyramid_levels_[level];
  LevelInfo info;
  info.dimensions = {tiff_level.size[0], tiff_level.size[1]};
  info.downsample_factor = tiff_level.downsample_factor;
  return info;
}

const SlideProperties& PhilipsTiffReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> PhilipsTiffReader::GetChannelMetadata() const {
  std::vector<ChannelMetadata> metadata;
  metadata.emplace_back("RGB", "Color", ColorRGB{255, 255, 255});
  return metadata;
}

std::vector<std::string> PhilipsTiffReader::GetAssociatedImageNames() const {
  return {};
}

aifocore::Result<ImageDimensions>
PhilipsTiffReader::GetAssociatedImageDimensions(std::string_view name) const {
  (void)name;
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                              "No associated images");
}

aifocore::Result<RGBImage> PhilipsTiffReader::ReadAssociatedImage(
    std::string_view name) const {
  (void)name;
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                              "No associated images");
}

Metadata PhilipsTiffReader::GetMetadata() const {
  Metadata metadata;
  metadata[std::string(MetadataKeys::kFormat)] = std::string("PhilipsTIFF");
  metadata[std::string(MetadataKeys::kLevels)] = pyramid_levels_.size();
  metadata[std::string(MetadataKeys::kChannels)] = static_cast<size_t>(3);
  return metadata;
}

ImageDimensions PhilipsTiffReader::GetTileSize() const {
  if (pyramid_levels_.empty() || !tiff_index_) {
    return ImageDimensions{256, 256};
  }
  const uint16_t page = pyramid_levels_[0].page;
  if (page >= tiff_index_->NumPages()) {
    return ImageDimensions{256, 256};
  }
  const auto& page_header = tiff_index_->Page(page);
  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index_->Tiles(page_header.payload_id);
    return ImageDimensions{tiles.tile_w, tiles.tile_h};
  }
  return ImageDimensions{256, 256};
}

aifocore::Result<std::string> PhilipsTiffReader::GetQuickHash() const {
  // Reuse OpenSlide-compatible quickhash logic already implemented for
  // GenericTIFF. The OpenSlide tifflike quickhash is based on smallest level
  // raw compressed bytes plus hashed TIFF properties.
  if (!tiff_index_ || pyramid_levels_.empty()) {
    return std::string("");
  }
  QuickHashBuilder hasher;
  const uint16_t lowest_page = pyramid_levels_.back().page;
  if (!readers::tiff_quickhash::HashPageRawCompressedBytes(
           *tiff_index_, lowest_page, fs::path(GetFilename()), hasher)
           .ok()) {
    return std::string("");
  }
  readers::tiff_quickhash::HashTiffProperties(*tiff_index_, hasher);

  return hasher.Finalize();
}

aifocore::Result<core::TilePlan> PhilipsTiffReader::PrepareRequest(
    const core::TileRequest& request) const {
  const PhilipsTiffPlanContext context{
      .pyramid_levels = pyramid_levels_,
      .tiff_index = GetTiffIndex(),
  };
  return PhilipsTiffPlanBuilder::BuildPlan(request, context);
}

aifocore::Status PhilipsTiffReader::ExecutePlan(const core::TilePlan& plan,
                                                runtime::Canvas& writer) const {
  const PhilipsTiffExecContext context{
      .tiff_index = GetTiffIndex(),
      .level_count = GetLevelCount(),
  };
  return PhilipsTiffTileExecutor::ExecutePlan(plan, context, writer);
}

aifocore::Status PhilipsTiffReader::ProcessMetadata() {
  AIFOCORE_RETURN_IF_ERROR(LoadDirectories());
  return aifocore::Status::OkStatus();
}

void PhilipsTiffReader::PopulateSlideProperties() {
  PopulateSlidePropertiesFromXml();
}

aifocore::Status PhilipsTiffReader::LoadDirectories() {
  if (!tiff_index_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "TIFF index not initialized");
  }
  if (tiff_index_->NumPages() == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No TIFF pages found");
  }
  if (tiff_index_->Page(0).storage != simpletiff::Storage::kTiles) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Philips TIFF is not tiled");
  }
  if (tiff_index_->Page(0).description.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Philips TIFF missing XML ImageDescription");
  }

  std::vector<uint16_t> level_pages;
  level_pages.reserve(tiff_index_->NumPages());
  for (size_t i = 0; i < tiff_index_->NumPages(); ++i) {
    const auto& page = tiff_index_->Page(i);
    if (page.storage != simpletiff::Storage::kTiles) {
      continue;
    }
    if (i != 0) {
      const bool is_reduced = (page.new_subfile_type & 0x1U) != 0;
      if (!is_reduced) {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInvalidArgument,
            aifocore::fmt::format("Directory {} is not reduced-resolution", i));
      }
    }
    level_pages.push_back(static_cast<uint16_t>(i));
  }

  if (level_pages.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No tiled levels found");
  }

  AIFOCORE_ASSIGN_OR_RETURN(
      auto spacings,
      ExtractLevelPixelSpacingsFromXml(tiff_index_->Page(0).description));
  if (spacings.size() != level_pages.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Philips XML level count does not match TIFF levels");
  }

  const auto& level0_header = tiff_index_->Page(level_pages[0]);
  const uint32_t base_w = level0_header.width;
  const uint32_t base_h = level0_header.height;
  const double l0_w = spacings[0].first;
  const double l0_h = spacings[0].second;
  if (l0_w <= 0.0 || l0_h <= 0.0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid Philips level 0 pixel spacing");
  }

  pyramid_levels_.clear();
  pyramid_levels_.reserve(level_pages.size());
  for (size_t i = 0; i < level_pages.size(); ++i) {
    const double spacing_w = spacings[i].first;
    const double spacing_h = spacings[i].second;
    if (spacing_w <= 0.0 || spacing_h <= 0.0) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Invalid Philips pixel spacing");
    }
    double downsample = 1.0;
    if (i > 0) {
      downsample = std::round(((spacing_w / l0_w) + (spacing_h / l0_h)) / 2.0);
      downsample = std::max(1.0, downsample);
    }

    const uint32_t out_w =
        static_cast<uint32_t>(static_cast<double>(base_w) / downsample);
    const uint32_t out_h =
        static_cast<uint32_t>(static_cast<double>(base_h) / downsample);

    pyramid_levels_.push_back({.page = level_pages[i],
                               .size = {out_w, out_h},
                               .downsample_factor = downsample});
  }

  // Bounds from level 0.
  properties_.bounds =
      SlideBounds(0, 0, pyramid_levels_[0].size[0], pyramid_levels_[0].size[1]);
  return aifocore::Status::OkStatus();
}

void PhilipsTiffReader::PopulateSlidePropertiesFromXml() {
  properties_.mpp = {0.0, 0.0};
  properties_.objective_magnification = 0.0;
  if (!tiff_index_ || tiff_index_->NumPages() == 0) {
    return;
  }
  if (tiff_index_->Page(0).description.empty()) {
    return;
  }
  const auto spacings_or =
      ExtractLevelPixelSpacingsFromXml(tiff_index_->Page(0).description);
  if (!spacings_or.ok() || spacings_or->empty()) {
    return;
  }
  properties_.mpp[0] = 1e3 * (*spacings_or)[0].first;
  properties_.mpp[1] = 1e3 * (*spacings_or)[0].second;
}

}  // namespace fastslide
