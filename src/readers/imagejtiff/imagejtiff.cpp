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

#include "fastslide/readers/imagejtiff/imagejtiff.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/imagejtiff/imagejtiff_exec_context.h"
#include "fastslide/readers/imagejtiff/imagejtiff_plan_builder.h"
#include "fastslide/readers/imagejtiff/imagejtiff_plan_context.h"
#include "fastslide/readers/imagejtiff/imagejtiff_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/colors.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide {

namespace {

constexpr std::string_view kImageJMarker = "ImageJ=";

/// @brief Parse a positive integer value for `key` from an ImageJ description.
///
/// ImageJ descriptions are newline-separated `key=value` pairs. Returns the
/// parsed value, or `fallback` when the key is absent or malformed.
uint32_t ParseImageJInt(std::string_view description, std::string_view key,
                        uint32_t fallback) {
  size_t pos = 0;
  while (pos < description.size()) {
    size_t line_end = description.find('\n', pos);
    if (line_end == std::string_view::npos) {
      line_end = description.size();
    }
    std::string_view line = description.substr(pos, line_end - pos);
    if (line.size() > key.size() && line.substr(0, key.size()) == key &&
        line[key.size()] == '=') {
      std::string_view value = line.substr(key.size() + 1);
      uint32_t parsed = 0;
      const auto* begin = value.data();
      const auto* end = value.data() + value.size();
      if (std::from_chars(begin, end, parsed).ec == std::errc{}) {
        return parsed;
      }
      return fallback;
    }
    pos = line_end + 1;
  }
  return fallback;
}

}  // namespace

aifocore::Result<std::unique_ptr<ImageJTiffReader>> ImageJTiffReader::Create(
    const fs::path& filename) {
  return CreateImpl(filename);
}

ImageJTiffReader::ImageJTiffReader(const fs::path& filename)
    : TiffBasedReader(filename) {
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd_val = -1;
  if (!simpletiff::OpenTiff(filename.string(), *tiff_index_, fd_val)) {
    // Reported during ProcessMetadata() if the index is invalid.
  }
}

bool ImageJTiffReader::IsImageJTiff(const simpletiff::TiffIndex& index) {
  if (index.NumPages() == 0) {
    return false;
  }
  const std::string_view description = index.Page(0).description;
  return description.rfind(kImageJMarker, 0) == 0;
}

int ImageJTiffReader::GetLevelCount() const {
  return static_cast<int>(pyramid_.size());
}

aifocore::Result<LevelInfo> ImageJTiffReader::GetLevelInfo(int level) const {
  if (level < 0 || static_cast<size_t>(level) >= pyramid_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }
  LevelInfo level_info;
  level_info.dimensions = pyramid_[level].size;
  level_info.downsample_factor = 1.0;
  return level_info;
}

const SlideProperties& ImageJTiffReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> ImageJTiffReader::GetChannelMetadata() const {
  return channels_;
}

std::vector<std::string> ImageJTiffReader::GetAssociatedImageNames() const {
  return {};
}

aifocore::Result<ImageDimensions>
ImageJTiffReader::GetAssociatedImageDimensions(std::string_view name) const {
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image '{}' not found", name));
}

aifocore::Result<RGBImage> ImageJTiffReader::ReadAssociatedImage(
    std::string_view name) const {
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image '{}' not found", name));
}

Metadata ImageJTiffReader::GetMetadata() const {
  Metadata metadata;
  metadata[std::string(MetadataKeys::kFormat)] = std::string("ImageJ TIFF");
  metadata[std::string(MetadataKeys::kLevels)] = pyramid_.size();
  metadata[std::string(MetadataKeys::kChannels)] =
      static_cast<size_t>(num_channels_);

  if (tiff_index_ && tiff_index_->NumPages() > 0) {
    const auto& page0 = tiff_index_->Page(0);
    if (!page0.description.empty()) {
      metadata[std::string("tiff.ImageDescription")] = page0.description;
    }
  }

  if (properties_.mpp[0] > 0) {
    metadata[std::string(MetadataKeys::kMppX)] = properties_.mpp[0];
    metadata[std::string(MetadataKeys::kMppY)] = properties_.mpp[1];
  }

  return metadata;
}

ImageDimensions ImageJTiffReader::GetTileSize() const {
  if (pyramid_.empty() || pyramid_[0].pages.empty() || !tiff_index_) {
    return ImageDimensions{256, 256};
  }
  const uint16_t page = pyramid_[0].pages[0];
  if (page >= tiff_index_->NumPages()) {
    return ImageDimensions{256, 256};
  }
  const auto& page_header = tiff_index_->Page(page);
  if (page_header.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index_->Tiles(page_header.payload_id);
    return ImageDimensions{tiles.tile_w, tiles.tile_h};
  }
  // Strip-based ImageJ pages: expose the whole plane as a single tile.
  return pyramid_[0].size;
}

aifocore::Result<core::TilePlan> ImageJTiffReader::PrepareRequest(
    const core::TileRequest& request) const {
  const ImageJTiffPlanContext context{
      .pyramid = pyramid_,
      .output_planar_config = output_planar_config_,
      .tiff_index = *tiff_index_,
  };
  return ImageJTiffPlanBuilder::BuildPlan(request, context);
}

aifocore::Status ImageJTiffReader::ExecutePlan(const core::TilePlan& plan,
                                               runtime::Canvas& writer) const {
  const ImageJTiffExecContext context(GetFilename(), pyramid_, *tiff_index_,
                                      GetCache());
  return ImageJTiffTileExecutor::ExecutePlan(plan, context, writer);
}

aifocore::Status ImageJTiffReader::ProcessMetadata() {
  if (!tiff_index_ || tiff_index_->NumPages() == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No valid images found in ImageJ TIFF");
  }

  const auto& page0 = tiff_index_->Page(0);
  if (page0.width == 0 || page0.height == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "ImageJ TIFF page 0 has zero dimensions");
  }

  data_type_ =
      DataTypeFromSampleFormat(page0.bits_per_sample, page0.sample_format);

  const uint16_t samples_per_pixel = page0.samples_per_pixel;
  const std::string_view description = page0.description;
  const uint32_t total_pages = static_cast<uint32_t>(tiff_index_->NumPages());

  ImageJTiffLevelInfo level;
  level.size = {page0.width, page0.height};

  if (samples_per_pixel >= 3) {
    // Interleaved RGB(A) ImageJ image: single page, contiguous output.
    format_ = ImageFormat::kRGB;
    output_planar_config_ = PlanarConfig::kContiguous;
    num_channels_ = 1;
    level.pages.push_back(0);
    channels_.emplace_back("RGB", "Color", ColorRGB{255, 255, 255});
  } else {
    // Single-sample pages: ImageJ stores one page per channel (c-fastest).
    const uint32_t parsed_channels = ParseImageJInt(description, "channels", 1);
    const uint32_t channels =
        std::clamp<uint32_t>(parsed_channels, 1, total_pages);

    level.Reserve(channels);
    for (uint32_t c = 0; c < channels; ++c) {
      level.pages.push_back(static_cast<uint16_t>(c));
    }

    num_channels_ = channels;
    output_planar_config_ = PlanarConfig::kSeparate;

    if (channels > 1) {
      format_ = ImageFormat::kSpectral;
      channels_.reserve(channels);
      for (uint32_t c = 0; c < channels; ++c) {
        const ColorRGB color = GetDefaultChannelColor(static_cast<int>(c));
        channels_.emplace_back(aifocore::fmt::format("Channel {}", c + 1), "",
                               color);
      }
    } else {
      format_ = ImageFormat::kGray;
      channels_.emplace_back("Gray", "Intensity", ColorRGB{255, 255, 255});
    }
  }

  pyramid_.push_back(std::move(level));
  return aifocore::Status::OkStatus();
}

void ImageJTiffReader::PopulateSlideProperties() {
  properties_.mpp = {0.0, 0.0};
  properties_.objective_magnification = 0.0;

  if (pyramid_.empty() || !tiff_index_) {
    return;
  }

  const auto& level0 = pyramid_[0];
  properties_.bounds = SlideBounds(0, 0, level0.size[0], level0.size[1]);

  if (level0.pages.empty()) {
    return;
  }
  const auto& page = tiff_index_->Page(level0.pages[0]);

  if (page.x_resolution.has_value() && page.resolution_unit.has_value()) {
    const double res = *page.x_resolution;
    const uint16_t unit = *page.resolution_unit;
    if (res > 0) {
      if (unit == 2) {  // Inch.
        properties_.mpp[0] = 25400.0 / res;
      } else if (unit == 3) {  // Centimetre.
        properties_.mpp[0] = 10000.0 / res;
      }
    }
  }
  if (page.y_resolution.has_value() && page.resolution_unit.has_value()) {
    const double res = *page.y_resolution;
    const uint16_t unit = *page.resolution_unit;
    if (res > 0) {
      if (unit == 2) {
        properties_.mpp[1] = 25400.0 / res;
      } else if (unit == 3) {
        properties_.mpp[1] = 10000.0 / res;
      }
    }
  }
}

}  // namespace fastslide
