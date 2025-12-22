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

#include "fastslide/readers/isyntax/isyntax.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/isyntax_plan_builder.h"
#include "fastslide/readers/isyntax/isyntax_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"
#include "readers/isyntax/third_party/file.h"
#include "readers/isyntax/third_party/isyntax.h"

namespace fastslide {

aifocore::Result<std::unique_ptr<IsyntaxReader>> IsyntaxReader::Create(
    std::string_view filename) {
  // Delegate to CreateImpl via path
  return CreateReaderImpl(std::filesystem::path(filename));
}

IsyntaxReader::IsyntaxReader(std::string filename)
    : filename_(std::move(filename)) {}

IsyntaxReader::~IsyntaxReader() {}

aifocore::Status IsyntaxReader::ValidateInput(
    const std::filesystem::path& filename) {
  // Check extension
  std::string ext = filename.extension().string();
  // case insensitive check?
  // For now simple check
  if (ext != ".isyntax") {
    return aifocore::Status(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid extension for iSyntax: {}", ext));
  }
  if (!std::filesystem::exists(filename)) {
    return aifocore::Status(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("File not found: {}", filename.string()));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Result<std::unique_ptr<IsyntaxReader>>
IsyntaxReader::CreateReaderImpl(const std::filesystem::path& filename) {
  auto reader =
      std::unique_ptr<IsyntaxReader>(new IsyntaxReader(filename.string()));
  AIFOCORE_RETURN_IF_ERROR(reader->Initialize());
  return reader;
}

aifocore::Status IsyntaxReader::Initialize() {
  // Open file
  // Check environment variable for XML dump flag (useful for debugging)
  const char* dump_xml = std::getenv("ISYNTAX_DUMP_XML");
  const bool dump_xml_header = (dump_xml && strcmp(dump_xml, "1") == 0);

  auto isyntax_or = isyntax::IsyntaxFile::Open(filename_, dump_xml_header);
  if (!isyntax_or.ok()) {
    return isyntax_or.status();
  }
  isyntax_file_ = std::move(*isyntax_or);

  PopulateSlideProperties();
  return aifocore::Status::OkStatus();
}

void IsyntaxReader::PopulateSlideProperties() {
  // Basic properties
  if (!isyntax_file_ || isyntax_file_->handle() == nullptr) {
    return;
  }
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* wsi = isx->images + isx->wsi_image_index;
  if (!wsi) {
    return;
  }

  int level_count = wsi->level_count;
  if (level_count > 0) {
    const isyntax_level_t* level0 = &wsi->levels[0];
    if (level0) {
      properties_.mpp = {static_cast<double>(level0->um_per_pixel_x),
                         static_cast<double>(level0->um_per_pixel_y)};

      // Bounds
      properties_.bounds = SlideBounds(0, 0, level0->width, level0->height);
    }
  }

  // Scanner model
  properties_.scanner_model = "Philips/iSyntax";

  // Channels: iSyntax is typically RGB (or YCbCr converted to RGB)
  // We report RGB channels
  channels_.clear();
  channels_.emplace_back(std::string("Red"), std::string(""),
                         ColorRGB{255, 0, 0});
  channels_.emplace_back(std::string("Green"), std::string(""),
                         ColorRGB{0, 255, 0});
  channels_.emplace_back(std::string("Blue"), std::string(""),
                         ColorRGB{0, 0, 255});
}

int IsyntaxReader::GetLevelCount() const {
  if (!isyntax_file_ || isyntax_file_->handle() == nullptr) {
    return 0;
  }
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* wsi = isx->images + isx->wsi_image_index;
  return wsi ? wsi->level_count : 0;
}

aifocore::Result<LevelInfo> IsyntaxReader::GetLevelInfo(int level) const {
  if (!isyntax_file_ || isyntax_file_->handle() == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInternal, "No WSI image");
  }
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* wsi = isx->images + isx->wsi_image_index;
  if (!wsi) {
    return aifocore::Status(aifocore::StatusCode::kInternal, "No WSI image");
  }

  if (level < 0 || level >= GetLevelCount()) {
    return aifocore::Status(aifocore::StatusCode::kInvalidArgument,
                            aifocore::fmt::format("Invalid level: {}", level));
  }

  const isyntax_level_t* lvl = &wsi->levels[level];
  if (!lvl) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to get level");
  }

  LevelInfo info;
  info.dimensions[0] = lvl->width;
  info.dimensions[1] = lvl->height;

  // Downsample calculation
  // scale 0 -> 1.0
  // scale 1 -> 2.0
  int scale = lvl->scale;
  info.downsample_factor = std::pow(2.0, scale);

  return info;
}

const SlideProperties& IsyntaxReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> IsyntaxReader::GetChannelMetadata() const {
  return channels_;
}

std::vector<std::string> IsyntaxReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  if (!isyntax_file_ || isyntax_file_->handle() == nullptr) {
    return names;
  }
  const isyntax_t* isx = isyntax_file_->handle();
  if (isx->label_image_index >= 0) {
    names.push_back("label");
  }
  if (isx->macro_image_index >= 0) {
    names.push_back("macro");
  }
  return names;
}

aifocore::Result<ImageDimensions> IsyntaxReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  if (!isyntax_file_ || isyntax_file_->handle() == nullptr) {
    return aifocore::Status(aifocore::StatusCode::kInternal, "No iSyntax file");
  }
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* img = nullptr;
  if (name == "label" && isx->label_image_index >= 0) {
    img = isx->images + isx->label_image_index;
  } else if (name == "macro" && isx->macro_image_index >= 0) {
    img = isx->images + isx->macro_image_index;
  }

  if (!img) {
    return aifocore::Status(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image not found: {}", name));
  }

  const isyntax_level_t* lvl = &img->levels[0];
  if (!lvl) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                            "Failed to get level 0 of associated image");
  }

  return ImageDimensions{static_cast<uint32_t>(lvl->width),
                         static_cast<uint32_t>(lvl->height)};
}

aifocore::Result<RGBImage> IsyntaxReader::ReadAssociatedImage(
    std::string_view name) const {
  if (!isyntax_file_) {
    return aifocore::Status(aifocore::StatusCode::kInternal, "No iSyntax file");
  }
  aifocore::Result<isyntax::RgbaImage> rgba_or = aifocore::Status(
      aifocore::StatusCode::kNotFound, "Unknown associated image");
  if (name == "label") {
    rgba_or = isyntax_file_->ReadLabelImage(isyntax::PixelFormat::kRgba);
  } else if (name == "macro") {
    rgba_or = isyntax_file_->ReadMacroImage(isyntax::PixelFormat::kRgba);
  }
  if (!rgba_or.ok()) {
    return rgba_or.status();
  }
  const isyntax::RgbaImage& rgba = *rgba_or;

  // Create RGBImage
  RGBImage image(
      {static_cast<uint32_t>(rgba.width), static_cast<uint32_t>(rgba.height)},
      ImageFormat::kRGB, DataType::kUInt8, PlanarConfig::kContiguous);
  uint8_t* dst = image.GetData();
  const uint8_t* src = reinterpret_cast<const uint8_t*>(rgba.pixels.data());

  int num_pixels = rgba.width * rgba.height;
  for (int i = 0; i < num_pixels; ++i) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst += 3;
    src += 4;
  }

  return image;
}

Metadata IsyntaxReader::GetMetadata() const {
  Metadata meta;
  meta[std::string(MetadataKeys::kFormat)] = std::string("iSyntax");
  meta[std::string(MetadataKeys::kLevels)] =
      static_cast<size_t>(GetLevelCount());
  meta[std::string(MetadataKeys::kMppX)] = properties_.mpp[0];
  meta[std::string(MetadataKeys::kMppY)] = properties_.mpp[1];
  meta[std::string(MetadataKeys::kScannerModel)] = properties_.scanner_model;

  if (isyntax_file_ && isyntax_file_->barcode() != nullptr) {
    meta[std::string(MetadataKeys::kSlideID)] =
        std::string(isyntax_file_->barcode());
  }

  return meta;
}

ImageDimensions IsyntaxReader::GetTileSize() const {
  return ImageDimensions{
      static_cast<uint32_t>(isyntax_file_ ? isyntax_file_->tile_width() : 0),
      static_cast<uint32_t>(isyntax_file_ ? isyntax_file_->tile_height() : 0)};
}

aifocore::Result<core::TilePlan> IsyntaxReader::PrepareRequest(
    const core::TileRequest& request) const {
  return IsyntaxPlanBuilder::BuildPlan(request, *this);
}

aifocore::Status IsyntaxReader::ExecutePlan(const core::TilePlan& plan,
                                            runtime::TileWriter& writer) const {
  return IsyntaxTileExecutor::ExecutePlan(plan, *this, writer);
}

}  // namespace fastslide
