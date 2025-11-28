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

#include "fastslide/readers/qptiff/qptiff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <pugixml.hpp>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/qptiff/metadata_parser.h"
#include "fastslide/readers/qptiff/qptiff_metadata_loader.h"
#include "fastslide/readers/qptiff/qptiff_plan_builder.h"
#include "fastslide/readers/qptiff/qptiff_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/colors.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide {

using aifocore::Size;

aifocore::Result<std::unique_ptr<QpTiffReader>>
QpTiffReader::Create(std::string_view filename) {
  return CreateImpl(filename);
}

QpTiffReader::QpTiffReader(std::string_view filename)
    : TiffBasedReader(std::string(filename)) {
  // Initialize SimpleTiff index during construction
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd = -1;
  if (!simpletiff::OpenTiff(std::string(filename), *tiff_index_, fd)) {
    // Note: Error will be caught during ProcessMetadata() if index is invalid
  } else {
  }
}

// SlideReader interface implementations
int QpTiffReader::GetLevelCount() const {
  return static_cast<int>(pyramid_.size());
}

aifocore::Result<LevelInfo> QpTiffReader::GetLevelInfo(int level) const {
  if (level < 0) {
    return aifocore::Result<LevelInfo>(aifocore::Status(
        aifocore::StatusCode::kInvalidArgument, "Level cannot be negative"));
  }

  if (level < 0 || static_cast<size_t>(level) >= pyramid_.size()) {
    return aifocore::Result<LevelInfo>(
        aifocore::Status(aifocore::StatusCode::kNotFound,
                         aifocore::fmt::format("Level {} not found", level)));
  }

  const auto &pyramid_level = pyramid_[level];
  LevelInfo level_info;
  level_info.dimensions = pyramid_level.size;

  // Calculate downsample factor relative to level 0
  // This should be consistent with openslide.
  if (level == 0) {
    level_info.downsample_factor = 1.0;
  } else {
    if (!pyramid_.empty()) {
      Size<double, 2> proportion =
          static_cast<Size<double, 2>>(pyramid_[0].size) /
          static_cast<Size<double, 2>>(pyramid_level.size);
      level_info.downsample_factor = (proportion[0] + proportion[1]) / 2.0;
    }
  }

  return level_info;
}

const SlideProperties &QpTiffReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> QpTiffReader::GetChannelMetadata() const {
  std::vector<ChannelMetadata> metadata;
  metadata.reserve(channels_.size());

  for (const auto &ch : channels_) {
    ChannelMetadata md;
    md.name = ch.name;
    md.biomarker = ch.biomarker;
    md.color = ch.color;
    md.exposure_time = ch.exposure_time;
    md.signal_units = ch.signal_units;
    metadata.push_back(std::move(md));
  }

  return metadata;
}

uint32_t QpTiffReader::GetActualChannelCount() const {
  if (channels_.empty()) {
    return 0;
  }

  // For RGB images, we have 1 logical channel but 3 actual color channels
  if (format_ == ImageFormat::kRGB) {
    return 3; // RGB has 3 channels
  }

  // For spectral images, logical channels = actual channels
  return static_cast<uint32_t>(channels_.size());
}

std::vector<std::string> QpTiffReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto &[name, info] : associated_images_) {
    names.push_back(name);
  }
  return names;
}

aifocore::Result<ImageDimensions>
QpTiffReader::GetAssociatedImageDimensions(std::string_view name) const {
  if (!associated_images_.contains(std::string(name))) {
    return aifocore::Result<ImageDimensions>(aifocore::Status(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image '{}' not found", name)));
  }

  const auto &info = associated_images_.at(std::string(name));
  return ImageDimensions{info.size[0], info.size[1]};
}

aifocore::Result<RGBImage>
QpTiffReader::ReadAssociatedImage(std::string_view name) const {
  if (!associated_images_.contains(std::string(name))) {
    return aifocore::Status(aifocore::StatusCode::kNotFound,
                            "Associated image '" + std::string(name) +
                                "' not found");
  }

  const QpTiffAssociatedInfo &info = associated_images_.at(std::string(name));

  // Use simpletiff to read the associated image page
  if (!tiff_index_ || info.page >= tiff_index_->NumPages()) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Invalid page {} for associated image '{}'",
                              info.page, name));
  }

  const auto &page_header = tiff_index_->Page(info.page);
  const uint32_t width = info.size[0];
  const uint32_t height = info.size[1];
  const uint16_t samples_per_pixel = page_header.samples_per_pixel;

  // Create RGBImage with proper dimensions
  RGBImage rgb_image({width, height}, ImageFormat::kRGB, DataType::kUInt8);

  // Read the page using simpletiff directly into the image buffer
  simpletiff::DecodeContext ctx;
  simpletiff::Roi roi{0, 0, width, height};
  const int stride = static_cast<int>(width) * samples_per_pixel;
  auto result = simpletiff::ReadPage(*tiff_index_, info.page, roi, ctx,
                                     rgb_image.GetData(), stride);

  if (!result) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read associated image '{}': {}", name,
                              result.error().message()));
  }

  return rgb_image;
}

// TODO(jonasteuwen): This function could fail,
// make it aifocore::Result<ImageDimensions>
ImageDimensions QpTiffReader::GetTileSize() const {
  // Try to get tile size from level 0
  if (pyramid_.empty() || pyramid_[0].pages.empty()) {
    return ImageDimensions{512, 512}; // Default for QPTIFF
  }

  if (!tiff_index_) {
    return ImageDimensions{512, 512}; // Default fallback
  }

  const uint16_t page = pyramid_[0].pages[0];
  if (page >= tiff_index_->NumPages()) {
    return ImageDimensions{512, 512}; // Default fallback
  }

  const auto &page_header = tiff_index_->Page(page);

  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto &tiles = tiff_index_->Tiles(page_header.payload_id);
    return ImageDimensions{tiles.tile_w, tiles.tile_h};
  }

  return ImageDimensions{512, 512}; // Default for QPTIFF
}

Metadata QpTiffReader::GetMetadata() const {
  Metadata metadata;

  // Mandatory keys
  metadata[std::string(MetadataKeys::kFormat)] = std::string("QPTIFF");
  metadata[std::string(MetadataKeys::kLevels)] = pyramid_.size();

  // Optional keys
  metadata[std::string(MetadataKeys::kMppX)] = metadata_.mpp_x;
  metadata[std::string(MetadataKeys::kMppY)] = metadata_.mpp_y;
  metadata[std::string(MetadataKeys::kMagnification)] = metadata_.magnification;
  metadata[std::string(MetadataKeys::kObjective)] = metadata_.objective_name;
  metadata[std::string(MetadataKeys::kScannerModel)] =
      std::string("PerkinElmer/QPTIFF");
  metadata[std::string(MetadataKeys::kChannels)] = channels_.size();
  metadata[std::string(MetadataKeys::kAssociatedImages)] =
      associated_images_.size();

  return metadata;
}

// ============================================================================
// Two-Stage Pipeline Implementation
// ============================================================================

aifocore::Result<core::TilePlan>
QpTiffReader::PrepareRequest(const core::TileRequest &request) const {
  // Use the plan builder helper to create the plan with tiff_index_
  return QptiffPlanBuilder::BuildPlan(request, pyramid_, output_planar_config_,
                                      *tiff_index_);
}

aifocore::Status QpTiffReader::ExecutePlan(const core::TilePlan &plan,
                                           runtime::TileWriter &writer) const {
  // Use the tile executor helper to execute the plan with tiff_index_
  return QptiffTileExecutor::ExecutePlan(plan, pyramid_, *tiff_index_, writer);
}

void QpTiffReader::PopulateSlideProperties() {
  properties_.mpp[0] = metadata_.mpp_x;
  properties_.mpp[1] = metadata_.mpp_y;
  properties_.objective_magnification = metadata_.magnification;
  properties_.objective_name = metadata_.objective_name;
  properties_.scanner_model = "PerkinElmer/QPTIFF";
  // scan_date is optional and not available in metadata

  // Set bounds to full slide (QPTIFF has complete coverage)
  auto level0_or = GetLevelInfo(0);
  if (level0_or.ok()) {
    const auto &level0 = *level0_or;
    properties_.bounds =
        SlideBounds(0, 0, level0.dimensions[0], level0.dimensions[1]);
  }
}

// Utility methods and implementation

aifocore::Status QpTiffReader::ProcessMetadata() {
  // Use the metadata loader helper to process metadata with tiff_index_
  AIFOCORE_RETURN_IF_ERROR(QptiffMetadataLoader::LoadMetadata(
      *tiff_index_, metadata_, channels_, pyramid_, associated_images_,
      format_));

  // Set output planar config based on format
  if (format_ == ImageFormat::kRGB) {
    output_planar_config_ = PlanarConfig::kContiguous; // RGB is interleaved
  } else {
  }

  return aifocore::Status::OkStatus();
}

} // namespace fastslide
