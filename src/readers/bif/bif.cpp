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

#include "fastslide/readers/bif/bif.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/metadata.h"
#include "fastslide/readers/bif/bif_exec_context.h"
#include "fastslide/readers/bif/bif_plan_builder.h"
#include "fastslide/readers/bif/bif_plan_context.h"
#include "fastslide/readers/bif/bif_tile_executor.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_parser.h"

namespace fs = std::filesystem;

namespace fastslide {
namespace {

// A pyramid IFD is a tiled page whose ImageDescription carries a "level="
// token (see bif.rst). IFD 0 (overview) and IFD 1 (mask) are not pyramidal.
[[nodiscard]] bool IsPyramidPage(const simpletiff::PageHeader& page) {
  return page.storage == simpletiff::Storage::kTiles &&
         page.description.find("level=") != std::string::npos;
}

[[nodiscard]] uint32_t TilesStride(const simpletiff::TiffIndex& tiff,
                                   uint16_t page_index) {
  const auto& page = tiff.Page(page_index);
  if (page.storage != simpletiff::Storage::kTiles) {
    return 0;
  }
  return tiff.Tiles(page.payload_id).tiles_x;
}

}  // namespace

aifocore::Result<std::unique_ptr<BifReader>> BifReader::Create(
    std::string_view filename) {
  return CreateImpl(filename);
}

BifReader::BifReader(std::string_view filename)
    : TiffBasedReader(fs::path(filename)) {
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd_val = -1;
  (void)simpletiff::OpenTiff(std::string(filename), *tiff_index_, fd_val);
}

aifocore::Status BifReader::ProcessMetadata() {
  if (!tiff_index_ || tiff_index_->NumPages() == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "BIF: no TIFF pages");
  }

  // IFD 0 carries the <iScan> scanner calibration packet.
  {
    const auto& page0 = tiff_index_->Page(0);
    AIFOCORE_ASSIGN_OR_RETURN(scanner_info_,
                              bif::ParseScannerInfo(page0.xmp_packet));
  }

  // Collect pyramid pages and order them by decreasing width (level 0 first).
  std::vector<uint16_t> pyramid_pages;
  for (uint16_t p = 0; p < tiff_index_->NumPages(); ++p) {
    if (IsPyramidPage(tiff_index_->Page(p))) {
      pyramid_pages.push_back(p);
    }
  }
  if (pyramid_pages.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "BIF: no pyramid (tiled) pages found");
  }
  std::sort(pyramid_pages.begin(), pyramid_pages.end(),
            [this](uint16_t a, uint16_t b) {
              return tiff_index_->Page(a).width > tiff_index_->Page(b).width;
            });

  const uint16_t level0_page = pyramid_pages.front();
  const auto& l0 = tiff_index_->Page(level0_page);
  const auto& l0_tiles = tiff_index_->Tiles(l0.payload_id);
  const uint32_t tile_w = l0_tiles.tile_w;
  const uint32_t tile_h = l0_tiles.tile_h;
  const uint32_t grid_cols = l0_tiles.tiles_x;

  // The level-0 pyramid IFD carries the <EncodeInfo> stitch geometry.
  AIFOCORE_ASSIGN_OR_RETURN(auto encode, bif::ParseEncodeInfo(l0.xmp_packet));
  AIFOCORE_ASSIGN_OR_RETURN(
      stitch_, bif::StitchLevel0(encode, tile_w, tile_h, grid_cols));

  // Build per-level metadata. Downsample is the dyadic ratio of raw widths.
  const uint32_t l0_raw_width = std::max<uint32_t>(l0.width, 1);
  levels_.clear();
  for (uint16_t page : pyramid_pages) {
    const auto& ph = tiff_index_->Page(page);
    const uint32_t raw_width = std::max<uint32_t>(ph.width, 1);
    const auto ds = static_cast<uint32_t>(
        std::max<long>(1, std::lround(static_cast<double>(l0_raw_width) /
                                      static_cast<double>(raw_width))));

    BifLevelInfo info;
    info.page = page;
    info.downsample = ds;
    info.downsample_factor = static_cast<double>(ds);
    info.size = {
        std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(
                                  static_cast<double>(stitch_.level0_width) /
                                  static_cast<double>(ds)))),
        std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(
                                  static_cast<double>(stitch_.level0_height) /
                                  static_cast<double>(ds))))};
    info.grid_cols = TilesStride(*tiff_index_, page);
    levels_.push_back(info);
  }

  spatial_indices_.assign(levels_.size(), nullptr);

  // Associated images: IFD 0 overview ("macro") and IFD 1 tissue mask ("mask").
  associated_images_.clear();
  {
    const auto& page0 = tiff_index_->Page(0);
    if (page0.width > 0 && page0.height > 0) {
      associated_images_.push_back(
          {0, {page0.width, page0.height}, std::string("macro")});
    }
  }
  if (tiff_index_->NumPages() > 1) {
    const auto& page1 = tiff_index_->Page(1);
    if (page1.width > 0 && page1.height > 0 &&
        page1.storage != simpletiff::Storage::kUnknown) {
      associated_images_.push_back(
          {1, {page1.width, page1.height}, std::string("mask")});
    }
  }

  return aifocore::Status::OkStatus();
}

void BifReader::PopulateSlideProperties() {
  properties_.mpp = {scanner_info_.scan_res, scanner_info_.scan_res};
  properties_.objective_magnification = scanner_info_.magnification;
  properties_.scanner_model = scanner_info_.scanner_model;
}

int BifReader::GetLevelCount() const {
  return static_cast<int>(levels_.size());
}

aifocore::Result<LevelInfo> BifReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= static_cast<int>(levels_.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }
  const auto& lvl = levels_[level];
  LevelInfo info;
  info.dimensions = {lvl.size[0], lvl.size[1]};
  info.downsample_factor = lvl.downsample_factor;
  return info;
}

const SlideProperties& BifReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> BifReader::GetChannelMetadata() const {
  std::vector<ChannelMetadata> metadata;
  metadata.emplace_back("RGB", "Color", ColorRGB{255, 255, 255});
  return metadata;
}

std::vector<std::string> BifReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto& img : associated_images_) {
    names.push_back(img.name);
  }
  return names;
}

aifocore::Result<ImageDimensions> BifReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      return ImageDimensions{img.size[0], img.size[1]};
    }
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image '{}' not found", name));
}

aifocore::Result<RGBImage> BifReader::ReadAssociatedImage(
    std::string_view name) const {
  const BifAssociatedInfo* info = nullptr;
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
  if (!tiff_index_ || info->page >= tiff_index_->NumPages()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Invalid page {} for associated image '{}'",
                              info->page, name));
  }

  const auto& page = tiff_index_->Page(info->page);
  const uint32_t width = info->size[0];
  const uint32_t height = info->size[1];
  const uint16_t spp = page.samples_per_pixel == 0 ? 3 : page.samples_per_pixel;
  const ImageFormat format = spp == 1 ? ImageFormat::kGray : ImageFormat::kRGB;

  RGBImage image({width, height}, format, DataType::kUInt8);
  simpletiff::DecodeContext ctx;
  simpletiff::Roi roi{0, 0, width, height};
  const int stride = static_cast<int>(width) * spp;
  auto result = simpletiff::ReadPage(*tiff_index_, info->page, roi, ctx,
                                     image.GetData(), stride);
  if (!result) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read associated image '{}': {}", name,
                              result.error().message()));
  }
  return image;
}

Metadata BifReader::GetMetadata() const {
  Metadata metadata;
  metadata[std::string(MetadataKeys::kFormat)] = std::string("BIF");
  metadata[std::string(MetadataKeys::kLevels)] = levels_.size();
  metadata[std::string(MetadataKeys::kChannels)] = static_cast<size_t>(3);
  if (!scanner_info_.scanner_model.empty()) {
    metadata[std::string(MetadataKeys::kScannerModel)] =
        scanner_info_.scanner_model;
  }
  if (scanner_info_.scan_res > 0) {
    metadata[std::string(MetadataKeys::kMppX)] = scanner_info_.scan_res;
    metadata[std::string(MetadataKeys::kMppY)] = scanner_info_.scan_res;
  }
  if (scanner_info_.magnification > 0) {
    metadata[std::string(MetadataKeys::kMagnification)] =
        scanner_info_.magnification;
  }
  if (!scanner_info_.barcode_1d.empty()) {
    metadata[std::string("bif.Barcode1D")] = scanner_info_.barcode_1d;
  }
  if (!scanner_info_.barcode_2d.empty()) {
    metadata[std::string("bif.Barcode2D")] = scanner_info_.barcode_2d;
  }
  return metadata;
}

ImageDimensions BifReader::GetTileSize() const {
  if (levels_.empty() || !tiff_index_) {
    return ImageDimensions{512, 512};
  }
  const uint16_t page = levels_[0].page;
  if (page >= tiff_index_->NumPages()) {
    return ImageDimensions{512, 512};
  }
  const auto& ph = tiff_index_->Page(page);
  if (ph.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index_->Tiles(ph.payload_id);
    return ImageDimensions{tiles.tile_w, tiles.tile_h};
  }
  return ImageDimensions{512, 512};
}

aifocore::Result<const bif::BifSpatialIndex*> BifReader::GetSpatialIndex(
    int level) const {
  if (level < 0 || level >= static_cast<int>(levels_.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }

  std::lock_guard<std::mutex> lock(spatial_mutex_);
  if (spatial_indices_[level]) {
    return spatial_indices_[level].get();
  }

  const auto& lvl = levels_[level];
  AIFOCORE_ASSIGN_OR_RETURN(
      auto index, bif::BifSpatialIndex::Build(stitch_, lvl.page, lvl.downsample,
                                              lvl.grid_cols));
  spatial_indices_[level] = std::move(index);
  return spatial_indices_[level].get();
}

aifocore::Result<core::TilePlan> BifReader::PrepareRequest(
    const core::TileRequest& request) const {
  const int level = request.level;
  AIFOCORE_ASSIGN_OR_RETURN(const bif::BifSpatialIndex* index,
                            GetSpatialIndex(level));

  BifPlanContext context;
  context.spatial_index = index;
  context.level_dims = levels_[level].size;
  context.level = level;
  context.scan_white_point = scanner_info_.scan_white_point;
  context.channels = 3;
  return BifPlanBuilder::BuildPlan(request, context);
}

aifocore::Status BifReader::ExecutePlan(const core::TilePlan& plan,
                                        runtime::Canvas& writer) const {
  const BifExecContext context(GetTiffIndex(), GetFilename(), GetCache());
  return BifTileExecutor::ExecutePlan(plan, context, writer);
}

}  // namespace fastslide
