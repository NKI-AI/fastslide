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

#include <tiffio.h>

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

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "aifocore/concepts/numeric.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/qptiff/metadata_parser.h"
#include "fastslide/readers/qptiff/qptiff_metadata_loader.h"
#include "fastslide/readers/qptiff/qptiff_plan_builder.h"
#include "fastslide/readers/qptiff/qptiff_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/colors.h"

namespace fastslide {

using aifocore::Size;

absl::StatusOr<std::unique_ptr<QpTiffReader>> QpTiffReader::Create(
    std::string_view filename) {
  return CreateImpl(filename);
}

QpTiffReader::QpTiffReader(std::string_view filename)
    : TiffBasedReader(std::string(filename)) {
  // Constructor is now private - initialization is done in Create()
}

// SlideReader interface implementations
int QpTiffReader::GetLevelCount() const {
  return static_cast<int>(pyramid_.size());
}

absl::StatusOr<LevelInfo> QpTiffReader::GetLevelInfo(int level) const {
  if (level < 0) {
    return absl::StatusOr<LevelInfo>(MAKE_STATUS(
        absl::StatusCode::kInvalidArgument, "Level cannot be negative"));
  }

  if (level < 0 || static_cast<size_t>(level) >= pyramid_.size()) {
    return absl::StatusOr<LevelInfo>(
        MAKE_STATUS(absl::StatusCode::kNotFound,
                    aifocore::fmt::format("Level {} not found", level)));
  }

  const auto& pyramid_level = pyramid_[level];
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

const SlideProperties& QpTiffReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> QpTiffReader::GetChannelMetadata() const {
  std::vector<ChannelMetadata> metadata;
  metadata.reserve(channels_.size());

  for (const auto& ch : channels_) {
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
    return 3;  // RGB has 3 channels
  }

  // For spectral images, logical channels = actual channels
  return static_cast<uint32_t>(channels_.size());
}

std::vector<std::string> QpTiffReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto& [name, info] : associated_images_) {
    names.push_back(name);
  }
  return names;
}

absl::StatusOr<ImageDimensions> QpTiffReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  if (!associated_images_.contains(std::string(name))) {
    return absl::StatusOr<ImageDimensions>(MAKE_STATUS(
        absl::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image '{}' not found", name)));
  }

  const auto& info = associated_images_.at(std::string(name));
  return ImageDimensions{info.size[0], info.size[1]};
}

absl::StatusOr<RGBImage> QpTiffReader::ReadAssociatedImage(
    std::string_view name) const {
  if (!associated_images_.contains(std::string(name))) {
    return MAKE_STATUSOR(
        RGBImage, absl::StatusCode::kNotFound,
        "Associated image '" + std::string(name) + "' not found");
  }

  const QpTiffAssociatedInfo& info = associated_images_.at(std::string(name));

  // Create TiffFile wrapper once
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return MAKE_STATUSOR(RGBImage, absl::StatusCode::kInternal,
                         "Failed to create TiffFile wrapper");
  }
  auto tiff_file = std::move(tiff_file_result.value());

  return ReadAssociatedImageFromPageWithFile(tiff_file, info.page, info.size[0],
                                             info.size[1], std::string(name));
}

// TODO(jonasteuwen): This function could fail,
// make it absl::StatusOr<ImageDimensions>
ImageDimensions QpTiffReader::GetTileSize() const {
  // Try to get tile size from level 0
  if (pyramid_.empty() || pyramid_[0].pages.empty()) {
    return ImageDimensions{512, 512};  // Default for QPTIFF
  }

  // Get tile dimensions from the first page of level 0
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return ImageDimensions{512, 512};  // Default fallback
  }
  auto tiff_file = std::move(tiff_file_result.value());

  auto status = tiff_file.SetDirectory(pyramid_[0].pages[0]);

  if (!status.ok()) {
    return ImageDimensions{512, 512};  // Default fallback
  }

  if (tiff_file.IsTiled()) {
    auto tile_dims_result = tiff_file.GetTileDimensions();
    if (tile_dims_result.ok()) {
      const auto& dims = tile_dims_result.value();
      return ImageDimensions{dims[0], dims[1]};
    }
  }

  return ImageDimensions{512, 512};  // Default for QPTIFF
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

absl::StatusOr<core::TilePlan> QpTiffReader::PrepareRequest(
    const core::TileRequest& request) const {
  // Create TiffFile wrapper for querying tile structure
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return tiff_file_result.status();
  }
  auto tiff_file = std::move(tiff_file_result.value());

  // Use the plan builder helper to create the plan
  return QptiffPlanBuilder::BuildPlan(request, pyramid_, output_planar_config_,
                                      tiff_file);
}

absl::Status QpTiffReader::ExecutePlan(const core::TilePlan& plan,
                                       runtime::TileWriter& writer) const {
  // Create TiffFile wrapper for reading
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return absl::InternalError("Failed to create TiffFile wrapper");
  }
  auto tiff_file = std::move(tiff_file_result.value());

  // Use the tile executor helper to execute the plan
  return QptiffTileExecutor::ExecutePlan(plan, pyramid_, tiff_file, writer);
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
    const auto& level0 = *level0_or;
    properties_.bounds =
        SlideBounds(0, 0, level0.dimensions[0], level0.dimensions[1]);
  }
}

// Utility methods and implementation

absl::Status QpTiffReader::ProcessMetadata() {
  // Create TiffFile wrapper using the handle pool
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to create TiffFile wrapper");
  }
  auto tiff_file = std::move(tiff_file_result.value());

  // Use the metadata loader helper to process metadata
  RETURN_IF_ERROR(
      QptiffMetadataLoader::LoadMetadata(tiff_file, metadata_, channels_,
                                         pyramid_, associated_images_, format_),
      "Failed to load metadata");

  // Set output planar config based on format
  if (format_ == ImageFormat::kRGB) {
    output_planar_config_ = PlanarConfig::kContiguous;  // RGB is interleaved
  }

  return absl::OkStatus();
}

}  // namespace fastslide
