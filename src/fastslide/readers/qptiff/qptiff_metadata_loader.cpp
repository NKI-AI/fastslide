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

#include "fastslide/readers/qptiff/qptiff_metadata_loader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <ranges>
#include <string>
#include <utility>

#include <pugixml.hpp>

#include "absl/log/log.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/qptiff/metadata_parser.h"
#include "fastslide/utilities/colors.h"

namespace fastslide {

absl::Status QptiffMetadataLoader::LoadMetadata(
    TiffFile& tiff_file, SlideMetadata& metadata,
    std::vector<QpTiffChannelInfo>& channels,
    std::vector<QpTiffLevelInfo>& pyramid,
    std::map<std::string, QpTiffAssociatedInfo>& associated_images,
    ImageFormat& format) {

  // Get total number of directories upfront
  uint16_t total_pages = 0;
  ASSIGN_OR_RETURN(total_pages, tiff_file.GetDirectoryCount());

  if (total_pages < 4) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        "QPTIFF file has too few pages: " + std::to_string(total_pages));
  }

  // Process full resolution channels
  uint16_t thumbnail_start_page;
  ASSIGN_OR_RETURN(thumbnail_start_page,
                   ProcessFullResolutionChannels(tiff_file, total_pages,
                                                 metadata, channels, format),
                   "Failed to process full resolution channels");

  if (channels.empty()) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "No full resolution channels found");
  }

  // Build level 0 from channels
  QpTiffLevelInfo level0;
  level0.Reserve(channels.size());
  for (const auto& ch : channels) {
    level0.pages.push_back(ch.page);
  }
  level0.size =
      aifocore::Size<uint32_t, 2>{channels[0].width, channels[0].height};
  level0.tiled =
      std::ranges::all_of(channels, [](const auto& ch) { return ch.tiled; });
  level0.allow_random_access = level0.tiled;
  pyramid.push_back(std::move(level0));

  // Process thumbnail and reduced levels
  RETURN_IF_ERROR(ProcessThumbnailAndReducedLevels(
                      tiff_file, thumbnail_start_page, total_pages,
                      channels.size(), pyramid, associated_images),
                  "Failed to process thumbnail and reduced levels");

  return absl::OkStatus();
}

absl::StatusOr<uint16_t> QptiffMetadataLoader::ProcessFullResolutionChannels(
    TiffFile& tiff_file, uint16_t total_pages, SlideMetadata& metadata,
    std::vector<QpTiffChannelInfo>& channels, ImageFormat& format) {

  uint16_t thumbnail_page = 0;

  // Process full resolution channels until we hit thumbnail
  for (auto page :
       std::views::iota(0u, total_pages) | std::views::take_while([&](auto p) {
         return !IsThumbnailPage(tiff_file, p);
       })) {

    RETURN_IF_ERROR(tiff_file.SetDirectory(page),
                    "Failed to set directory " + std::to_string(page));

    // Get basic directory info
    std::array<uint32_t, 2> image_dims;
    ASSIGN_OR_RETURN(
        image_dims, tiff_file.GetImageDimensions(),
        "Failed to get image dimensions for page " + std::to_string(page));

    // Get XML metadata
    auto desc_result = tiff_file.GetImageDescription();
    if (desc_result.empty()) {
      continue;  // Skip pages without XML
    }

    // Parse XML
    pugi::xml_document doc;
    if (!doc.load_string(desc_result.c_str())) {
      return MAKE_STATUS(
          absl::StatusCode::kInvalidArgument,
          "Failed to parse XML metadata on page " + std::to_string(page));
    }

    auto root = doc.child("PerkinElmer-QPI-ImageDescription");
    if (root.empty()) {
      return MAKE_STATUS(
          absl::StatusCode::kInvalidArgument,
          "Invalid XML structure on page " + std::to_string(page));
    }

    std::string image_type =
        formats::qptiff::QpTiffMetadataParser::ExtractImageType(desc_result);

    // This should be a full resolution channel
    if (image_type == "FullResolution" || image_type.empty()) {
      // Check if this is an RGB image
      auto photometric_result = tiff_file.GetPhotometric();
      auto samples_per_pixel_result = tiff_file.GetSamplesPerPixel();

      bool is_rgb_image = photometric_result.ok() &&
                          samples_per_pixel_result.ok() &&
                          photometric_result.value() == TiffPhotometric::RGB &&
                          samples_per_pixel_result.value() == 3;

      if (is_rgb_image) {
        // This is an RGB image - treat as a single channel with RGB format
        if (channels.empty()) {
          format = ImageFormat::kRGB;

          QpTiffChannelInfo channel;
          channel.name = "RGB";
          channel.biomarker = "RGB Brightfield";
          channel.color = ColorRGB(255, 255, 255);
          channel.exposure_time = 0;
          channel.signal_units = 0;
          channel.page = page;
          channel.width = image_dims[0];
          channel.height = image_dims[1];
          channel.tiled = tiff_file.IsTiled();
          channel.allow_random_access = channel.tiled;

          channels.push_back(std::move(channel));

          // Extract metadata from page 0
          RETURN_IF_ERROR(ExtractResolutionMetadata(tiff_file, metadata, &root),
                          "Failed to extract resolution metadata from page 0");
        }
        // Skip additional RGB pages
      } else {
        // This is a spectral/fluorescence channel
        auto channel_result =
            formats::qptiff::QpTiffMetadataParser::ParseChannelInfo(
                desc_result, static_cast<int>(channels.size()));
        if (!channel_result.ok()) {
          return channel_result.status();
        }
        auto new_channel_info = channel_result.value();

        QpTiffChannelInfo channel;
        channel.name = new_channel_info.name;
        channel.biomarker = new_channel_info.biomarker;
        channel.color = new_channel_info.color;
        channel.exposure_time = new_channel_info.exposure_time;
        channel.signal_units = new_channel_info.signal_units;
        channel.page = page;
        channel.width = image_dims[0];
        channel.height = image_dims[1];
        channel.tiled = tiff_file.IsTiled();
        channel.allow_random_access = channel.tiled;

        channels.push_back(std::move(channel));

        // Extract metadata from page 0
        if (page == 0) {
          RETURN_IF_ERROR(ExtractResolutionMetadata(tiff_file, metadata, &root),
                          "Failed to extract resolution metadata from page 0");
        }
      }
    }

    thumbnail_page = page + 1;
  }

  return thumbnail_page;
}

absl::Status QptiffMetadataLoader::ProcessThumbnailAndReducedLevels(
    TiffFile& tiff_file, uint16_t thumbnail_start_page, uint16_t total_pages,
    size_t num_channels, std::vector<QpTiffLevelInfo>& pyramid,
    std::map<std::string, QpTiffAssociatedInfo>& associated_images) {

  // Find and process the thumbnail page
  for (uint16_t current_page = thumbnail_start_page; current_page < total_pages;
       ++current_page) {
    if (IsThumbnailPage(tiff_file, current_page)) {
      RETURN_IF_ERROR(tiff_file.SetDirectory(current_page),
                      "Failed to set directory for thumbnail");

      auto image_dims_result = tiff_file.GetImageDimensions();
      if (image_dims_result.ok()) {
        const auto& image_dims = image_dims_result.value();
        associated_images["Thumbnail"] = QpTiffAssociatedInfo{
            .page = current_page, .size = {image_dims[0], image_dims[1]}};
      }
      thumbnail_start_page = current_page;
      break;
    }
  }

  // Process remaining pages after thumbnail: reduced levels followed by associated images
  uint16_t current_page = thumbnail_start_page + 1;
  std::vector<uint16_t> current_level_pages;

  while (current_page < total_pages) {
    RETURN_IF_ERROR(tiff_file.SetDirectory(current_page),
                    "Failed to set directory " + std::to_string(current_page));

    // Get XML metadata to determine image type
    auto desc_result = tiff_file.GetImageDescription();
    std::string image_type;

    if (!desc_result.empty()) {
      pugi::xml_document doc;
      if (doc.load_string(desc_result.c_str())) {
        auto root = doc.child("PerkinElmer-QPI-ImageDescription");
        if (!root.empty()) {
          image_type = formats::qptiff::QpTiffMetadataParser::ExtractImageType(
              desc_result);
        }
      }
    }

    if (image_type == "ReducedResolution" || image_type.empty()) {
      // This is part of a reduced level
      current_level_pages.push_back(current_page);

      // If we have collected enough pages for one level
      if (current_level_pages.size() == num_channels) {
        QpTiffLevelInfo reduced_level;
        reduced_level.Reserve(num_channels);
        reduced_level.pages = current_level_pages;

        // Get dimensions from first page of this level
        RETURN_IF_ERROR(tiff_file.SetDirectory(current_level_pages[0]),
                        "Failed to set directory for reduced level");
        ASSIGN_OR_RETURN(reduced_level.size, tiff_file.GetImageDimensions());

        reduced_level.tiled = tiff_file.IsTiled();
        reduced_level.allow_random_access = reduced_level.tiled;

        pyramid.push_back(std::move(reduced_level));
        current_level_pages.clear();
      }
    } else {
      // This is an associated image
      ImageDimensions dims;
      ASSIGN_OR_RETURN(dims, tiff_file.GetImageDimensions(),
                       "Failed to get image dimensions for associated image");

      std::string assoc_name =
          image_type.empty() ? "Associated_" + std::to_string(current_page)
                             : image_type;

      associated_images[assoc_name] =
          QpTiffAssociatedInfo{.page = current_page, .size = dims};
    }

    ++current_page;
  }

  // Handle any remaining pages in current_level_pages (partial level)
  if (!current_level_pages.empty()) {
    LOG(WARNING) << "Found incomplete reduced level with "
                 << current_level_pages.size() << " pages (expected "
                 << num_channels << ")";
  }

  return absl::OkStatus();
}

bool QptiffMetadataLoader::IsThumbnailPage(TiffFile& tiff_file, uint16_t page) {
  if (tiff_file.SetDirectory(page) != absl::OkStatus()) {
    return true;  // Stop iteration on error
  }

  auto desc_result = tiff_file.GetImageDescription();
  if (desc_result.empty()) {
    return false;  // Not a thumbnail, continue
  }

  pugi::xml_document doc;
  if (!doc.load_string(desc_result.c_str())) {
    return true;  // Stop iteration on parse error
  }

  auto root = doc.child("PerkinElmer-QPI-ImageDescription");
  if (root.empty()) {
    return true;  // Stop iteration on structure error
  }

  std::string image_type =
      formats::qptiff::QpTiffMetadataParser::ExtractImageType(desc_result);
  return image_type == "Thumbnail";
}

absl::Status QptiffMetadataLoader::ExtractResolutionMetadata(
    TiffFile& tiff_file, SlideMetadata& metadata, const void* xml_root) {

  // Extract MPP from TIFF tags
  auto x_res = tiff_file.GetXResolution();
  auto y_res = tiff_file.GetYResolution();
  uint16_t res_unit = tiff_file.GetResolutionUnit();

  if (!x_res.has_value() || !y_res.has_value()) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       "Missing resolution information in TIFF tags");
  }

  // Convert resolution to microns per pixel
  double mpp_x = 0.0;
  double mpp_y = 0.0;
  switch (res_unit) {
    case RESUNIT_INCH:
      mpp_x = 25400.0 / x_res.value();  // 25400 microns per inch
      mpp_y = 25400.0 / y_res.value();
      break;
    case RESUNIT_CENTIMETER:
      mpp_x = 10000.0 / x_res.value();  // 10000 microns per cm
      mpp_y = 10000.0 / y_res.value();
      break;
    default:
      return MAKE_STATUS(
          absl::StatusCode::kInvalidArgument,
          "Unsupported resolution unit: " + std::to_string(res_unit));
  }

  // Validate isotropic resolution
  if (std::abs(mpp_x - mpp_y) / std::max(mpp_x, mpp_y) > 0.01 || mpp_x <= 0.0 ||
      mpp_y <= 0.0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        "Computed MPP values are not isotropic enough or not positive: " +
            std::to_string(mpp_x) + ", " + std::to_string(mpp_y) + " µm/px");
  }

  metadata.mpp_x = mpp_x;
  metadata.mpp_y = mpp_y;

  // Optionally validate against XML metadata if present
  if (xml_root != nullptr) {
    const auto* root = static_cast<const pugi::xml_node*>(xml_root);
    auto resolution_node = root->child("ScanProfile").child("root");

    if (!resolution_node.empty()) {
      auto pixel_size_node = resolution_node.child("PixelSizeMicrons");
      if (!pixel_size_node.empty()) {
        double xml_pixel_size = pixel_size_node.text().as_double();
        double tolerance = 0.05 * (mpp_x + mpp_y) / 2.0;
        if (std::abs(mpp_y - xml_pixel_size) > tolerance ||
            std::abs(mpp_x - xml_pixel_size) > tolerance) {
          LOG(WARNING) << "TIFF resolution doesn't match XML resolution - "
                       << "TIFF: " << mpp_y << " µm/px, XML: " << xml_pixel_size
                       << " µm/px (tolerance: " << tolerance << ")";
        }
      }

      // Extract magnification from XML
      auto magnification_node = resolution_node.child("Magnification");
      if (!magnification_node.empty()) {
        metadata.magnification = magnification_node.text().as_double();
      }

      // Extract objective name from XML
      auto objective_node = resolution_node.child("ObjectiveName");
      if (!objective_node.empty()) {
        metadata.objective_name = objective_node.text().as_string();
      }
    }
  }

  return absl::OkStatus();
}

}  // namespace fastslide
