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

#include "fastslide/readers/generictiff/generictiff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/generictiff/generictiff_plan_builder.h"
#include "fastslide/readers/generictiff/generictiff_tile_executor.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "fastslide/readers/tiff_quickhash.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/hash.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fs = std::filesystem;

namespace fastslide {

aifocore::Result<std::unique_ptr<GenericTiffReader>> GenericTiffReader::Create(
    const fs::path& filename) {
  return CreateImpl(filename);
}

GenericTiffReader::GenericTiffReader(const fs::path& filename)
    : TiffBasedReader(filename) {
  // Initialize SimpleTiff index during construction
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd_val = -1;
  if (!simpletiff::OpenTiff(filename.string(), *tiff_index_, fd_val)) {
    // Error will be caught during ProcessMetadata() if index is invalid
  }
}

int GenericTiffReader::GetLevelCount() const {
  return static_cast<int>(pyramid_levels_.size());
}

aifocore::Result<LevelInfo> GenericTiffReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= static_cast<int>(pyramid_levels_.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }

  const auto& tiff_level = pyramid_levels_[level];
  LevelInfo level_info;
  level_info.dimensions = {tiff_level.size[0], tiff_level.size[1]};
  level_info.downsample_factor = tiff_level.downsample_factor;

  return level_info;
}

const SlideProperties& GenericTiffReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> GenericTiffReader::GetChannelMetadata() const {
  std::vector<ChannelMetadata> metadata;
  if (tiff_index_ && !pyramid_levels_.empty() &&
      pyramid_levels_[0].page < tiff_index_->NumPages()) {
    const auto& page_header = tiff_index_->Page(pyramid_levels_[0].page);
    if (page_header.samples_per_pixel == 1) {
      metadata.emplace_back("Gray", "Intensity", ColorRGB{255, 255, 255});
      return metadata;
    }
  }
  metadata.emplace_back("RGB", "Color", ColorRGB{255, 255, 255});
  return metadata;
}

std::vector<std::string> GenericTiffReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto& img : associated_images_) {
    names.push_back(img.name);
  }
  return names;
}

aifocore::Result<ImageDimensions>
GenericTiffReader::GetAssociatedImageDimensions(std::string_view name) const {
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      return ImageDimensions{img.size[0], img.size[1]};
    }
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image '{}' not found", name));
}

aifocore::Result<RGBImage> GenericTiffReader::ReadAssociatedImage(
    std::string_view name) const {
  const GenericTiffAssociatedInfo* info = nullptr;
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      info = &img;
      break;
    }
  }

  if (info == nullptr) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image '{}' not found", name));
  }
  if (!tiff_index_) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("TIFF index not initialized for '{}'", name));
  }
  return readers::simpletiff_decode::ReadAssociatedTiffPage(
      *tiff_index_, info->page, info->size, name);
}

Metadata GenericTiffReader::GetMetadata() const {
  Metadata metadata;
  metadata[std::string(MetadataKeys::kFormat)] = std::string("GenericTIFF");
  metadata[std::string(MetadataKeys::kLevels)] = pyramid_levels_.size();
  size_t channels = 3;
  if (tiff_index_ && !pyramid_levels_.empty() &&
      pyramid_levels_[0].page < tiff_index_->NumPages()) {
    channels = tiff_index_->Page(pyramid_levels_[0].page).samples_per_pixel;
  }
  metadata[std::string(MetadataKeys::kChannels)] = channels;

  // OpenSlide-compatible TIFF property key (used by quickhash and external
  // tools).
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

ImageDimensions GenericTiffReader::GetTileSize() const {
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

aifocore::Result<std::string> GenericTiffReader::GetQuickHash() const {
  // OpenSlide-compatible TIFF quickhash:
  // 1) hash raw compressed bytes from the smallest (lowest-res) level
  // 2) hash selected TIFF properties as NUL-terminated name + value strings
  //
  // OpenSlide also disables quickhash if the smallest level exceeds ~5MiB
  // compressed to keep open() fast.
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

uint16_t GenericTiffReader::GetLevel0Page() const {
  if (pyramid_levels_.empty()) {
    return 0;
  }
  return pyramid_levels_[0].page;
}

// Pipeline Implementation

aifocore::Result<core::TilePlan> GenericTiffReader::PrepareRequest(
    const core::TileRequest& request) const {
  return GenericTiffPlanBuilder::BuildPlan(request, *this);
}

aifocore::Status GenericTiffReader::ExecutePlan(const core::TilePlan& plan,
                                                runtime::Canvas& writer) const {
  return GenericTiffTileExecutor::ExecutePlan(plan, *this, writer);
}

aifocore::Status GenericTiffReader::ProcessMetadata() {
  AIFOCORE_RETURN_IF_ERROR(LoadDirectories());
  return aifocore::Status::OkStatus();
}

aifocore::Status GenericTiffReader::LoadDirectories() {
  if (!tiff_index_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "TIFF index not initialized");
  }

  pyramid_levels_.clear();
  associated_images_.clear();

  // Candidates for pyramid levels.
  // - only tiled directories are levels
  // - include directory 0 and reduced-resolution directories
  // - define downsample from width ratio and report height as
  //   level0_height / downsample
  struct LevelCandidate {
    uint16_t page;
    uint32_t width;
    uint32_t height;
    uint64_t area;
    bool is_tiled;
    bool is_reduced;
  };

  std::vector<LevelCandidate> all_candidates;
  all_candidates.reserve(tiff_index_->NumPages());
  std::vector<LevelCandidate> openslide_levels;

  for (size_t i = 0; i < tiff_index_->NumPages(); ++i) {
    const auto& page = tiff_index_->Page(i);

    if (page.width == 0 || page.height == 0) {
      continue;
    }

    // Standard TIFF NewSubfileType: ReducedImage bit.
    const bool is_reduced = (page.new_subfile_type & 0x1U) != 0;
    const bool is_tiled = (page.storage == simpletiff::Storage::kTiles);

    LevelCandidate candidate{
        .page = static_cast<uint16_t>(i),
        .width = page.width,
        .height = page.height,
        .area = static_cast<uint64_t>(page.width) * page.height,
        .is_tiled = is_tiled,
        .is_reduced = is_reduced,
    };
    all_candidates.push_back(candidate);

    if (is_tiled && (i == 0 || is_reduced)) {
      openslide_levels.push_back(candidate);
    }
  }

  if (all_candidates.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No valid images found in TIFF");
  }

  if (!openslide_levels.empty()) {
    std::sort(openslide_levels.begin(), openslide_levels.end(),
              [](const LevelCandidate& lhs, const LevelCandidate& rhs) {
                // Match OpenSlide: sort by width descending.
                if (lhs.width != rhs.width) {
                  return lhs.width > rhs.width;
                }
                return lhs.height > rhs.height;
              });

    const LevelCandidate& level0 = openslide_levels.front();
    const uint32_t base_w = level0.width;
    const uint32_t base_h = level0.height;

    pyramid_levels_.reserve(openslide_levels.size());
    for (const auto& cand : openslide_levels) {
      // OpenSlide computes missing downsamples in openslide.c as:
      //   downsample = (((blh / l->h) + (blw / l->w)) / 2)
      // where blw/blh are level 0 dimensions, and l->w/l->h come from the TIFF.
      const double width_ratio =
          static_cast<double>(base_w) /
          static_cast<double>(std::max<uint32_t>(cand.width, 1));
      const double height_ratio =
          static_cast<double>(base_h) /
          static_cast<double>(std::max<uint32_t>(cand.height, 1));
      const double downsample = (width_ratio + height_ratio) / 2.0;

      pyramid_levels_.push_back({.page = cand.page,
                                 .size = {cand.width, cand.height},
                                 .downsample_factor = downsample});
    }

    return aifocore::Status::OkStatus();
  }

  // Fallback: accept non-tiled pyramids or single-page TIFFs.
  std::sort(
      all_candidates.begin(), all_candidates.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.area > rhs.area; });
  const LevelCandidate& level0 = all_candidates.front();
  pyramid_levels_.push_back({level0.page, {level0.width, level0.height}, 1.0});

  std::vector<LevelCandidate> pyramid_candidates;
  pyramid_candidates.reserve(all_candidates.size());
  for (const auto& cand : all_candidates) {
    if (cand.area < level0.area) {
      pyramid_candidates.push_back(cand);
    }
  }
  std::sort(
      pyramid_candidates.begin(), pyramid_candidates.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.area > rhs.area; });
  for (const auto& cand : pyramid_candidates) {
    if (cand.area == static_cast<uint64_t>(pyramid_levels_.back().size[0]) *
                         pyramid_levels_.back().size[1]) {
      continue;
    }
    const double downsample = std::sqrt(static_cast<double>(level0.area) /
                                        static_cast<double>(cand.area));
    pyramid_levels_.push_back(
        {cand.page, {cand.width, cand.height}, downsample});
  }

  return aifocore::Status::OkStatus();
}

void GenericTiffReader::PopulateSlideProperties() {
  // Basic defaults
  properties_.mpp = {0.0, 0.0};
  properties_.objective_magnification = 0.0;

  if (!pyramid_levels_.empty()) {
    const auto& level0_info = pyramid_levels_[0];
    const auto& page = tiff_index_->Page(level0_info.page);

    properties_.bounds =
        SlideBounds(0, 0, level0_info.size[0], level0_info.size[1]);

    if (page.x_resolution.has_value() && page.resolution_unit.has_value()) {
      double res = *page.x_resolution;
      uint16_t unit = *page.resolution_unit;
      if (res > 0) {
        if (unit == 2) {  // Inch
          properties_.mpp[0] = 25400.0 / res;
        } else if (unit == 3) {  // cm
          properties_.mpp[0] = 10000.0 / res;
        }
      }
    }

    if (page.y_resolution.has_value() && page.resolution_unit.has_value()) {
      double res = *page.y_resolution;
      uint16_t unit = *page.resolution_unit;
      if (res > 0) {
        if (unit == 2) {
          properties_.mpp[1] = 25400.0 / res;
        } else if (unit == 3) {
          properties_.mpp[1] = 10000.0 / res;
        }
      }
    }
  }
}

}  // namespace fastslide
