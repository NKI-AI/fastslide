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

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/isyntax/isyntax_exec_context.h"
#include "fastslide/readers/isyntax/isyntax_plan_builder.h"
#include "fastslide/readers/isyntax/isyntax_plan_context.h"
#include "fastslide/readers/isyntax/isyntax_tile_executor.h"
#include "fastslide/readers/isyntax/third_party/file.h"
#include "fastslide/readers/isyntax/third_party/isyntax.h"
#include "fastslide/runtime/io/filesystem_utils.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/hash.h"

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
  // Accept both iSyntax v1 (".isyntax") and v2 (".i2syntax"), case-insensitive.
  std::string ext = filename.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (ext != ".isyntax" && ext != ".i2syntax") {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid extension for iSyntax: {}", ext));
  }
  AIFOCORE_RETURN_IF_ERROR(
      runtime::io::RequireExists(filename, "iSyntax slide file"));
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

  AIFOCORE_ASSIGN_OR_RETURN(
      isyntax_file_, isyntax::IsyntaxFile::Open(filename_, dump_xml_header));

  PopulateSlideProperties();
  return aifocore::Status::OkStatus();
}

namespace {

/// Valid data envelopes of the WSI image: the polygons of the header
/// (iSyntax v2), or else the groups of level 0 tiles present (v1).
IsyntaxEnvelopes ReadEnvelopes(const isyntax_t& isx, const isyntax_image_t& wsi) {
  if (isx.valid_data_envelope_count > 0) {
    std::vector<IsyntaxEnvelopes::Polygon> polygons;
    for (int32_t e = 0; e < isx.valid_data_envelope_count; ++e) {
      const isyntax_valid_data_envelope_t& envelope =
          isx.valid_data_envelopes[e];
      IsyntaxEnvelopes::Polygon& polygon = polygons.emplace_back();
      for (int32_t v = 0; v < envelope.vertex_count; ++v) {
        polygon.push_back({envelope.vertices[v].x, envelope.vertices[v].y});
      }
    }
    return IsyntaxEnvelopes::FromHeader(std::move(polygons),
                                        wsi.level0_padding);
  }
  if (wsi.level_count <= 0 || wsi.levels[0].tiles == nullptr) {
    return IsyntaxEnvelopes::FromHeader({}, 0);
  }
  const isyntax_level_t& level0 = wsi.levels[0];
  std::vector<uint8_t> present(
      static_cast<size_t>(level0.width_in_tiles) * level0.height_in_tiles);
  for (size_t i = 0; i < present.size(); ++i) {
    present[i] = level0.tiles[i].exists ? 1 : 0;
  }
  return IsyntaxEnvelopes::FromTiles(present, level0.width_in_tiles,
                                     isx.tile_width, isx.tile_height,
                                     wsi.level0_padding, wsi.width, wsi.height);
}

}  // namespace

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

      // Bounds: the box around the valid data envelopes, as the Philips SDK
      // reports them; the whole image when there is none.
      envelopes_ = ReadEnvelopes(*isx, *wsi);
      const SlideBounds bounds = envelopes_.Bounds(wsi->width, wsi->height);
      properties_.bounds = bounds.IsValid()
                               ? bounds
                               : SlideBounds(0, 0, level0->width, level0->height);
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
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No WSI image");
  }
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* wsi = isx->images + isx->wsi_image_index;
  if (!wsi) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No WSI image");
  }

  if (level < 0 || level >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  const isyntax_level_t* lvl = &wsi->levels[level];
  if (!lvl) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to get level");
  }

  // The level is exposed as the range the Philips SDK reports as valid for
  // it, see IsyntaxPlanBuilder::ComputeValidRange.
  const isyntax_level_t* level0 = &wsi->levels[0];
  const auto range = IsyntaxPlanBuilder::ComputeValidRange(
      level, level0->width, level0->height);
  LevelInfo info;
  info.dimensions[0] =
      static_cast<uint32_t>(std::max<int64_t>(0, range.end_x - range.start + 1));
  info.dimensions[1] =
      static_cast<uint32_t>(std::max<int64_t>(0, range.end_y - range.start + 1));

  // Downsample calculation
  // scale 0 -> 1.0
  // scale 1 -> 2.0
  int scale = lvl->scale;
  info.downsample_factor = std::pow(2.0, scale);

  return info;
}

aifocore::Result<std::vector<SlideBounds>> IsyntaxReader::GetDataRegions(
    int level) const {
  if (envelopes_.empty()) {
    return SlideReader::GetDataRegions(level);
  }
  if (auto level_info = GetLevelInfo(level); !level_info.ok()) {
    return level_info.status();
  }
  // SDK pixels of the level, moved to the reader's: its pixel 0 is the first
  // pixel of the valid range.
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* wsi = isx->images + isx->wsi_image_index;
  const auto range = IsyntaxPlanBuilder::ComputeValidRange(
      level, wsi->levels[0].width, wsi->levels[0].height);
  std::vector<SlideBounds> regions = envelopes_.Regions(level);
  for (SlideBounds& region : regions) {
    region.x -= range.start;
    region.y -= range.start;
  }
  return regions;
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
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No iSyntax file");
  }
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* img = nullptr;
  if (name == "label" && isx->label_image_index >= 0) {
    img = isx->images + isx->label_image_index;
  } else if (name == "macro" && isx->macro_image_index >= 0) {
    img = isx->images + isx->macro_image_index;
  }

  if (!img) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image not found: {}", name));
  }

  const isyntax_level_t* lvl = &img->levels[0];
  if (!lvl) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Failed to get level 0 of associated image");
  }

  return ImageDimensions{static_cast<uint32_t>(lvl->width),
                         static_cast<uint32_t>(lvl->height)};
}

aifocore::Result<RGBImage> IsyntaxReader::ReadAssociatedImage(
    std::string_view name) const {
  if (!isyntax_file_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "No iSyntax file");
  }
  aifocore::Result<isyntax::RgbaImage> rgba_or = AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound, "Unknown associated image");
  if (name == "label") {
    rgba_or = isyntax_file_->ReadLabelImage(isyntax::PixelFormat::kRgba);
  } else if (name == "macro") {
    rgba_or = isyntax_file_->ReadMacroImage(isyntax::PixelFormat::kRgba);
  }
  AIFOCORE_RETURN_IF_ERROR(rgba_or);
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

aifocore::Result<std::string> IsyntaxReader::GetQuickHash() const {
  const isyntax_t* handle = GetIsyntaxFile().handle();
  if (handle == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "iSyntax file is not open");
  }
  const int32_t wsi_index = handle->wsi_image_index;
  if (wsi_index < 0 || wsi_index >= handle->image_count) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "iSyntax file has no WSI image to hash");
  }
  const isyntax_image_t& wsi = handle->images[wsi_index];
  if (wsi.codeblocks == nullptr || wsi.codeblock_count <= 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kFailedPrecondition,
        "iSyntax WSI image has no codeblocks to identify the slide");
  }

  QuickHashBuilder hasher;
  if (handle->is_barcode_read) {
    // NUL-terminated, matching how OpenSlide folds identifier strings in.
    const std::string_view barcode(handle->barcode);
    AIFOCORE_RETURN_IF_ERROR(hasher.HashData(
        reinterpret_cast<const uint8_t*>(barcode.data()), barcode.size() + 1));
  }

  // The coarsest scale is the top of the wavelet pyramid: a handful of
  // codeblocks, hashed in file order so the digest does not depend on how the
  // header happened to enumerate them.
  std::vector<const isyntax_codeblock_t*> coarsest;
  for (int32_t i = 0; i < wsi.codeblock_count; ++i) {
    const isyntax_codeblock_t& block = wsi.codeblocks[i];
    if (static_cast<int32_t>(block.scale) == wsi.max_scale &&
        block.block_size > 0) {
      coarsest.push_back(&block);
    }
  }
  if (coarsest.empty()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kFailedPrecondition,
        aifocore::fmt::format(
            "iSyntax WSI image has no codeblock data at its coarsest scale {}",
            wsi.max_scale));
  }
  std::sort(coarsest.begin(), coarsest.end(),
            [](const isyntax_codeblock_t* lhs, const isyntax_codeblock_t* rhs) {
              return lhs->block_data_offset < rhs->block_data_offset;
            });
  for (const isyntax_codeblock_t* block : coarsest) {
    AIFOCORE_RETURN_IF_ERROR(hasher.HashFilePart(
        filename_, static_cast<int64_t>(block->block_data_offset),
        static_cast<int64_t>(block->block_size)));
  }
  return hasher.Finalize();
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

aifocore::Result<std::vector<uint8_t>> IsyntaxReader::GetIccProfile() const {
  if (!isyntax_file_) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kFailedPrecondition,
                                "iSyntax file is not open");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  isyntax::IccProfile profile;
  AIFOCORE_ASSIGN_OR_RETURN(profile, isyntax_file_->ReadIccProfileForWsi());
  return std::move(profile.bytes);
}

ImageDimensions IsyntaxReader::GetTileSize() const {
  return ImageDimensions{
      static_cast<uint32_t>(isyntax_file_ ? isyntax_file_->tile_width() : 0),
      static_cast<uint32_t>(isyntax_file_ ? isyntax_file_->tile_height() : 0)};
}

aifocore::Result<core::TilePlan> IsyntaxReader::PrepareRequest(
    const core::TileRequest& request) const {
  AIFOCORE_ASSIGN_OR_RETURN(const auto level_info, GetLevelInfo(request.level));
  const isyntax_t* isx = isyntax_file_->handle();
  const isyntax_image_t* wsi = isx->images + isx->wsi_image_index;
  const isyntax_level_t* level = wsi->levels + request.level;
  // Pixel 0 of the level is the first pixel of its valid range.
  const auto range = IsyntaxPlanBuilder::ComputeValidRange(
      request.level, wsi->levels[0].width, wsi->levels[0].height);
  const IsyntaxPlanContext context{
      .tile_size = GetTileSize(),
      .level_info = level_info,
      .grid_offset =
          IsyntaxPlanBuilder::ComputeGridOffset(
              request.level, wsi->level0_padding,
              request.level == 0 ? 0 : level->origin_offset_in_pixels) +
          range.start,
      .grid_width_in_tiles = level->width_in_tiles,
      .grid_height_in_tiles = level->height_in_tiles,
  };
  return IsyntaxPlanBuilder::BuildPlan(request, context);
}

aifocore::Status IsyntaxReader::ExecutePlan(const core::TilePlan& plan,
                                            runtime::Canvas& writer) const {
  const IsyntaxExecContext context(GetFilename(), GetIsyntaxFile(), GetMutex(),
                                   GetTileSize(), GetCache());
  return IsyntaxTileExecutor::ExecutePlan(plan, context, writer);
}

}  // namespace fastslide
