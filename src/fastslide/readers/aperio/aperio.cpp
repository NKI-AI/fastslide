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

#include "fastslide/readers/aperio/aperio.h"

#include <tiffio.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"

#include "fastslide/readers/aperio/aperio_plan_builder.h"
#include "fastslide/readers/aperio/aperio_tile_executor.h"
#include "fastslide/readers/aperio/metadata_parser.h"
#include "fastslide/runtime/tile_writer.h"
#include "fastslide/utilities/hash.h"
#include "fastslide/utilities/tiff/directory_processor.h"
#include "fastslide/utilities/tiff/tiff_file.h"

namespace fs = std::filesystem;

namespace fastslide {

namespace {

/// @brief Callback struct for Aperio-specific directory processing
///
/// This struct uses compile-time polymorphism (no inheritance) for zero
/// vtable overhead. Used with TiffDirectoryProcessorT template.
struct AperioDirectoryCallback {
  /// @brief Nested struct for tracking tiled directories (defined first)
  struct TiledDirectoryInfo {
    uint16_t page;
    std::array<uint32_t, 2> size;
    uint64_t area;
  };

  // Members using aggregate initialization
  std::vector<AperioLevelInfo>& pyramid_levels;
  std::vector<AperioAssociatedInfo>& associated_images;
  formats::aperio::AperioMetadata& metadata;
  bool metadata_extracted = false;
  std::vector<TiledDirectoryInfo> tiled_directories;

  /// @brief Process a single directory (no override keyword - direct call)
  absl::Status ProcessDirectory(const TiffDirectoryInfoWithPage& dir_info) {
    const auto& image_dims = dir_info.info.image_dims;
    if (image_dims[0] == 0 || image_dims[1] == 0) {
      return MAKE_STATUS(
          absl::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Invalid image dimensions: {}x{}",
                                image_dims[0], image_dims[1]));
    }

    // Extract metadata from first directory
    if (!metadata_extracted && dir_info.page == 0) {
      if (dir_info.info.image_description.has_value() &&
          !dir_info.info.image_description->empty()) {
        auto status =
            formats::aperio::AperioMetadataParser::ParseFromDescription(
                *dir_info.info.image_description, metadata);
        if (status.ok()) {
          metadata_extracted = true;
        }
        // Continue even if metadata extraction fails for this directory
      }
    }

    if (dir_info.info.is_tiled) {
      // Store tiled directory info for later processing
      TiledDirectoryInfo tiled_info;
      tiled_info.page = dir_info.page;
      tiled_info.size = {image_dims[0], image_dims[1]};
      tiled_info.area = static_cast<uint64_t>(image_dims[0]) * image_dims[1];

      tiled_directories.push_back(tiled_info);
    } else {
      // Non-tiled → associated image
      std::string name;
      if (dir_info.page == 1) {
        name = "thumbnail";
      } else {
        // Parse name from ImageDescription
        if (dir_info.info.image_description.has_value() &&
            !dir_info.info.image_description->empty()) {
          name =
              formats::aperio::AperioMetadataParser::ParseAssociatedImageName(
                  *dir_info.info.image_description);
        }
        if (name.empty()) {
          name = "unknown";
        }
      }
      associated_images.push_back(
          AperioAssociatedInfo{.page = dir_info.page,
                               .size = {image_dims[0], image_dims[1]},
                               .name = name});
    }

    return absl::OkStatus();
  }

  /// @brief Finalize processing (no override keyword - direct call)
  absl::Status Finalize() {
    // Sort tiled directories by area (largest first)
    std::sort(tiled_directories.begin(), tiled_directories.end(),
              [](const TiledDirectoryInfo& a, const TiledDirectoryInfo& b) {
                return a.area > b.area;
              });

    // Convert to pyramid levels with proper downsample factors
    pyramid_levels.clear();
    pyramid_levels.reserve(tiled_directories.size());

    if (tiled_directories.empty()) {
      return absl::OkStatus();
    }

    // First (largest) becomes level 0
    const auto& level0 = tiled_directories[0];
    pyramid_levels.push_back(
        AperioLevelInfo{.page = level0.page,
                        .size = {level0.size[0], level0.size[1]},
                        .downsample_factor = 1.0});

    // Calculate downsample factors for remaining levels
    for (size_t i = 1; i < tiled_directories.size(); ++i) {
      const auto& level = tiled_directories[i];

      // Calculate downsample as average of width and height ratios
      double downsample = (static_cast<double>(level0.size[0]) /
                               static_cast<double>(level.size[0]) +
                           static_cast<double>(level0.size[1]) /
                               static_cast<double>(level.size[1])) /
                          2.0;

      pyramid_levels.push_back(
          AperioLevelInfo{.page = level.page,
                          .size = {level.size[0], level.size[1]},
                          .downsample_factor = downsample});
    }

    return absl::OkStatus();
  }
};

}  // namespace

// AperioReader implementation
absl::StatusOr<std::unique_ptr<AperioReader>> AperioReader::Create(
    fs::path filename) {
  return CreateImpl(filename);
}

AperioReader::AperioReader(fs::path filename) : TiffBasedReader(filename) {
  // Constructor is now private - initialization is done in Create()
}

int AperioReader::GetLevelCount() const {
  return static_cast<int>(pyramid_levels_.size());
}

absl::StatusOr<LevelInfo> AperioReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= static_cast<int>(pyramid_levels_.size())) {
    return MAKE_STATUSOR(LevelInfo, absl::StatusCode::kNotFound,
                         aifocore::fmt::format("Level {} not found", level));
  }

  const auto& aperio_level = pyramid_levels_[level];
  LevelInfo level_info;
  level_info.dimensions = {aperio_level.size[0], aperio_level.size[1]};
  level_info.downsample_factor = aperio_level.downsample_factor;

  return level_info;
}

const SlideProperties& AperioReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> AperioReader::GetChannelMetadata() const {
  // Aperio files typically have RGB channels
  std::vector<ChannelMetadata> metadata;
  metadata.emplace_back("RGB", "Histological stain", ColorRGB{255, 255, 255});
  return metadata;
}

std::vector<std::string> AperioReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto& img : associated_images_) {
    names.push_back(img.name);
  }
  return names;
}

absl::StatusOr<ImageDimensions> AperioReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      return ImageDimensions{img.size[0], img.size[1]};
    }
  }
  return MAKE_STATUSOR(
      ImageDimensions, absl::StatusCode::kNotFound,
      aifocore::fmt::format("Associated image '{}' not found", name));
}

absl::StatusOr<RGBImage> AperioReader::ReadAssociatedImage(
    std::string_view name) const {
  const AperioAssociatedInfo* info = nullptr;
  for (const auto& img : associated_images_) {
    if (img.name == name) {
      info = &img;
      break;
    }
  }

  if (!info) {
    return MAKE_STATUSOR(
        RGBImage, absl::StatusCode::kNotFound,
        aifocore::fmt::format("Associated image '{}' not found", name));
  }

  return ReadAssociatedImageFromPage(info->page, info->size[0], info->size[1],
                                     std::string(name));
}

// GetBestLevelForDownsample uses the base class implementation

ImageDimensions AperioReader::GetTileSize() const {
  // Try to get tile size from level 0
  if (pyramid_levels_.empty()) {
    return ImageDimensions{256, 256};  // Default for Aperio
  }

  // Get tile dimensions from the first level
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return ImageDimensions{256, 256};  // Default fallback
  }
  auto tiff_file = std::move(tiff_file_result.value());

  auto status = tiff_file.SetDirectory(pyramid_levels_[0].page);
  if (!status.ok()) {
    return ImageDimensions{256, 256};  // Default fallback
  }

  if (tiff_file.IsTiled()) {
    auto tile_dims_result = tiff_file.GetTileDimensions();
    if (tile_dims_result.ok()) {
      const auto& dims = tile_dims_result.value();
      return ImageDimensions{dims[0], dims[1]};
    }
  }

  return ImageDimensions{256, 256};  // Default for Aperio
}

absl::StatusOr<std::string> AperioReader::GetQuickHash() const {
  // OpenSlide-compatible quickhash for TIFF-based formats:
  // 1. Hash raw compressed tile data from lowest resolution level
  // 2. Hash TIFF property strings (name + value, each with null terminators)
  //
  // This matches OpenSlide's _openslide_tifflike_init_properties_and_hash():
  // - Uses TIFFReadRawTile/Strip to get raw compressed bytes (not decoded)
  // - Hashes property name and value as null-terminated strings
  // - Properties hashed: ImageDescription, Make, Model, Software, DateTime,
  //   Artist, HostComputer, Copyright, DocumentName (in this order)
  QuickHashBuilder hasher;

  // Hash tile data from the lowest resolution level
  if (pyramid_levels_.empty()) {
    return hasher.Finalize();
  }

  const auto& lowest_res = pyramid_levels_.back();

  // Get handle for reading
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return hasher.Finalize();
  }
  auto tiff_file = std::move(tiff_file_result.value());

  auto status = tiff_file.SetDirectory(lowest_res.page);
  if (!status.ok()) {
    return hasher.Finalize();
  }

  // Get raw TIFF handle for low-level operations
  TIFF* tif = tiff_file.GetHandle();
  if (!tif) {
    return hasher.Finalize();
  }

  // Get tile/strip info
  const bool is_tiled = tiff_file.IsTiled();
  uint32_t tiles_across = 1;
  uint32_t tiles_down = 1;

  if (is_tiled) {
    auto tile_dims = tiff_file.GetTileDimensions();
    if (tile_dims.ok()) {
      tiles_across =
          (lowest_res.size[0] + (*tile_dims)[0] - 1) / (*tile_dims)[0];
      tiles_down = (lowest_res.size[1] + (*tile_dims)[1] - 1) / (*tile_dims)[1];
    }
  } else {
    // For strips, get rows per strip from TIFF handle
    uint32_t rows_per_strip = 0;
    if (TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &rows_per_strip) &&
        rows_per_strip > 0) {
      tiles_across = 1;
      tiles_down = (lowest_res.size[1] + rows_per_strip - 1) / rows_per_strip;
    }
  }

  // Hash all tiles/strips from lowest resolution
  // Use TIFFReadRawTile/Strip to get the raw compressed data (matches
  // OpenSlide)
  const uint32_t total_tiles = tiles_across * tiles_down;
  for (uint32_t i = 0; i < total_tiles; ++i) {
    // Get the raw tile/strip size from TIFF tags
    tmsize_t raw_size;
    if (is_tiled) {
      // Get tile byte count from TIFF tag
      toff_t* tile_sizes;
      if (!TIFFGetField(tif, TIFFTAG_TILEBYTECOUNTS, &tile_sizes)) {
        continue;
      }
      raw_size = tile_sizes[i];
    } else {
      // Get strip byte count from TIFF tag
      toff_t* strip_sizes;
      if (!TIFFGetField(tif, TIFFTAG_STRIPBYTECOUNTS, &strip_sizes)) {
        continue;
      }
      raw_size = strip_sizes[i];
    }

    if (raw_size <= 0) {
      continue;  // Skip zero-length tiles (can happen with Aperio encoder bug)
    }

    std::vector<uint8_t> tile_data(raw_size);

    // Read RAW tile/strip data (not decoded, just compressed bytes)
    tmsize_t bytes_read;
    if (is_tiled) {
      bytes_read = TIFFReadRawTile(tif, i, tile_data.data(), raw_size);
    } else {
      bytes_read = TIFFReadRawStrip(tif, i, tile_data.data(), raw_size);
    }

    if (bytes_read > 0) {
      tile_data.resize(bytes_read);
      auto hash_status = hasher.HashData(tile_data);
      if (!hash_status.ok()) {
        // Continue hashing even if one tile fails
        continue;
      }
    }
  }

  // Hash TIFF properties from directory 0 (matches OpenSlide's
  // store_and_hash_properties) Get TIFF handle for property_dir = 0
  auto tiff_file_for_props = TiffFile::Create(handle_pool_.get());
  if (tiff_file_for_props.ok()) {
    auto prop_status = tiff_file_for_props->SetDirectory(0);
    if (prop_status.ok()) {
      TIFF* prop_tif = tiff_file_for_props->GetHandle();

      // Helper lambda to hash property name + value (with null terminators)
      auto hash_string_prop = [&](const char* prop_name, uint32_t tiff_tag) {
        // Hash property name (with null terminator)
        auto name_status = hasher.HashData(
            reinterpret_cast<const uint8_t*>(prop_name), strlen(prop_name) + 1);
        if (!name_status.ok()) {
          return;  // Skip this property if hashing fails
        }

        // Get and hash property value (with null terminator)
        char* value = nullptr;
        absl::Status value_status;
        if (TIFFGetField(prop_tif, tiff_tag, &value) && value) {
          value_status = hasher.HashData(
              reinterpret_cast<const uint8_t*>(value), strlen(value) + 1);
        } else {
          // Hash empty string with null terminator if property doesn't exist
          value_status =
              hasher.HashData(reinterpret_cast<const uint8_t*>(""), 1);
        }
        // Continue even if value hashing fails (quickhash is best-effort)
        if (!value_status.ok()) {
          LOG(WARNING) << "Failed to hash TIFF property " << prop_name;
        }
      };

      // Hash TIFF properties in the same order as OpenSlide
      hash_string_prop("tiff.ImageDescription", TIFFTAG_IMAGEDESCRIPTION);
      hash_string_prop("tiff.Make", TIFFTAG_MAKE);
      hash_string_prop("tiff.Model", TIFFTAG_MODEL);
      hash_string_prop("tiff.Software", TIFFTAG_SOFTWARE);
      hash_string_prop("tiff.DateTime", TIFFTAG_DATETIME);
      hash_string_prop("tiff.Artist", TIFFTAG_ARTIST);
      hash_string_prop("tiff.HostComputer", TIFFTAG_HOSTCOMPUTER);
      hash_string_prop("tiff.Copyright", TIFFTAG_COPYRIGHT);
      hash_string_prop("tiff.DocumentName", TIFFTAG_DOCUMENTNAME);
    }
  }

  return hasher.Finalize();
}

Metadata AperioReader::GetMetadata() const {
  Metadata metadata;

  // Mandatory keys
  metadata[std::string(MetadataKeys::kFormat)] = std::string("Aperio");
  metadata[std::string(MetadataKeys::kLevels)] = pyramid_levels_.size();

  // Optional keys
  metadata[std::string(MetadataKeys::kMppX)] = aperio_metadata_.mpp[0];
  metadata[std::string(MetadataKeys::kMppY)] = aperio_metadata_.mpp[1];
  metadata[std::string(MetadataKeys::kMagnification)] =
      aperio_metadata_.app_mag;
  metadata[std::string(MetadataKeys::kScannerID)] = aperio_metadata_.scanner_id;
  metadata[std::string(MetadataKeys::kScannerModel)] = std::string("Aperio");
  metadata[std::string(MetadataKeys::kChannels)] =
      static_cast<size_t>(3);  // RGB
  metadata[std::string(MetadataKeys::kAssociatedImages)] =
      associated_images_.size();

  return metadata;
}

absl::Status AperioReader::ProcessMetadata() {
  // Load directories and extract basic information
  RETURN_IF_ERROR(LoadDirectories(), "Failed to load TIFF directories");

  return absl::OkStatus();
}

absl::Status AperioReader::LoadDirectories() {
  // Create TiffFile wrapper using the handle pool
  auto tiff_file_result = TiffFile::Create(handle_pool_.get());
  if (!tiff_file_result.ok()) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        aifocore::fmt::format("Failed to create TiffFile wrapper: {}",
                              tiff_file_result.status().message()));
  }
  auto tiff_file = std::move(tiff_file_result.value());

  pyramid_levels_.clear();
  associated_images_.clear();

  // Create template-based directory processor (zero vtable overhead)
  TiffDirectoryProcessorT<AperioDirectoryCallback> processor(
      std::move(tiff_file));

  // Create callback using aggregate initialization
  AperioDirectoryCallback callback{
      .pyramid_levels = pyramid_levels_,
      .associated_images = associated_images_,
      .metadata = aperio_metadata_,
  };

  // Process all directories - callbacks are inlined at compile time!
  RETURN_IF_ERROR(processor.ProcessAllDirectories(callback),
                  "Failed to process TIFF directories");

  return absl::OkStatus();
}

void AperioReader::PopulateSlideProperties() {
  properties_.mpp = aifocore::Size<double, 2>{aperio_metadata_.mpp[0],
                                              aperio_metadata_.mpp[1]};
  properties_.objective_magnification = aperio_metadata_.app_mag;
  properties_.objective_name =
      aifocore::fmt::format("{}x", aperio_metadata_.app_mag);
  properties_.scanner_model =
      aifocore::fmt::format("Aperio/{}", aperio_metadata_.scanner_id);

  // Set bounds to full slide (Aperio has complete coverage)
  auto level0_or = GetLevelInfo(0);
  if (level0_or.ok()) {
    const auto& level0 = *level0_or;
    properties_.bounds =
        SlideBounds(0, 0, level0.dimensions[0], level0.dimensions[1]);
  }
}

// ============================================================================
// Two-Stage Pipeline Implementation
// ============================================================================
//
// Aperio uses a two-stage pipeline similar to MRXS:
// 1. PrepareRequest: Analyzes the requested region and determines which TIFF
//    tiles intersect it. Creates a TilePlan with operations for each tile.
//    Handles both tiled and stripped TIFF formats. Queries actual channel
//    count from TIFF (typically RGB/3 for Aperio, but supports other formats).
// 2. ExecutePlan: Reads each TIFF tile via libtiff, extracts the needed
//    sub-region if the tile is partially clipped, and writes to output.
// ============================================================================

absl::StatusOr<core::TilePlan> AperioReader::PrepareRequest(
    const core::TileRequest& request) const {
  // Delegate to plan builder which handles all planning logic
  return AperioPlanBuilder::BuildPlan(request, *this, tiff_metadata_);
}

absl::Status AperioReader::ExecutePlan(const core::TilePlan& plan,
                                       runtime::TileWriter& writer) const {
  // Delegate to executor which handles tile reading with handle pool
  return AperioTileExecutor::ExecutePlan(plan, *this, writer, tiff_metadata_);
}

}  // namespace fastslide
