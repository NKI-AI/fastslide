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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_METADATA_LOADER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_METADATA_LOADER_H_

#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "fastslide/readers/qptiff/qptiff.h"
#include "fastslide/utilities/tiff/tiff_file.h"

/**
 * @file qptiff_metadata_loader.h
 * @brief QPTIFF metadata parser and pyramid builder
 * 
 * This header defines QptiffMetadataLoader, a helper class for parsing
 * QPTIFF-specific metadata and building the pyramid structure. This is
 * used during reader initialization to extract all format-specific
 * information from the TIFF file.
 * 
 * **Responsibilities:**
 * - Parse XML metadata from ImageDescription tags
 * - Extract channel information (names, biomarkers, colors, exposure)
 * - Build pyramid structure (determine levels and pages)
 * - Identify associated images (thumbnails, macros)
 * - Detect image format (RGB vs. spectral)
 * - Extract physical properties (MPP, magnification, objective)
 * 
 * **QPTIFF Metadata Structure:**
 * - XML embedded in ImageDescription TIFF tags
 * - Per-channel metadata (one XML per channel page)
 * - Biomarker and fluorophore information
 * - Acquisition parameters (exposure, gain, binning)
 * - Scanner and instrument details
 * 
 * **Pyramid Building:**
 * - Analyzes all TIFF directories to find pyramid levels
 * - Groups pages by resolution to identify levels
 * - Handles cases where not all channels have all levels
 * - Identifies non-pyramid pages (thumbnails, macros)
 * 
 * @see QpTiffReader for the main reader class
 * @see formats::qptiff::QpTiffMetadataParser for XML parsing utilities
 */

namespace fastslide {

/// @brief Helper class for loading QPTIFF metadata and building pyramid structure
class QptiffMetadataLoader {
 public:
  /// @brief Load metadata from QPTIFF file
  /// @param tiff_file TiffFile instance to read from
  /// @param metadata Output slide metadata
  /// @param channels Output channel information
  /// @param pyramid Output pyramid levels
  /// @param associated_images Output associated images
  /// @param format Output image format (RGB or spectral)
  /// @return Status indicating success or failure
  static absl::Status LoadMetadata(
      TiffFile& tiff_file, SlideMetadata& metadata,
      std::vector<QpTiffChannelInfo>& channels,
      std::vector<QpTiffLevelInfo>& pyramid,
      std::map<std::string, QpTiffAssociatedInfo>& associated_images,
      ImageFormat& format);

 private:
  /// @brief Process full resolution channels
  /// @param tiff_file TiffFile instance
  /// @param total_pages Total number of pages in file
  /// @param metadata Output slide metadata
  /// @param channels Output channel information
  /// @param format Output image format
  /// @return Page number where thumbnail starts, or error
  static absl::StatusOr<uint16_t> ProcessFullResolutionChannels(
      TiffFile& tiff_file, uint16_t total_pages, SlideMetadata& metadata,
      std::vector<QpTiffChannelInfo>& channels, ImageFormat& format);

  /// @brief Process thumbnail and reduced resolution levels
  /// @param tiff_file TiffFile instance
  /// @param thumbnail_start_page First thumbnail page
  /// @param total_pages Total number of pages
  /// @param num_channels Number of channels per level
  /// @param pyramid Output pyramid levels
  /// @param associated_images Output associated images
  /// @return Status indicating success or failure
  static absl::Status ProcessThumbnailAndReducedLevels(
      TiffFile& tiff_file, uint16_t thumbnail_start_page, uint16_t total_pages,
      size_t num_channels, std::vector<QpTiffLevelInfo>& pyramid,
      std::map<std::string, QpTiffAssociatedInfo>& associated_images);

  /// @brief Check if a page is a thumbnail
  /// @param tiff_file TiffFile instance
  /// @param page Page number to check
  /// @return true if page is a thumbnail
  static bool IsThumbnailPage(TiffFile& tiff_file, uint16_t page);

  /// @brief Extract resolution metadata from TIFF tags
  /// @param tiff_file TiffFile instance positioned at page 0
  /// @param metadata Output metadata structure
  /// @param xml_root Optional XML root node for validation
  /// @return Status indicating success or failure
  static absl::Status ExtractResolutionMetadata(TiffFile& tiff_file,
                                                SlideMetadata& metadata,
                                                const void* xml_root = nullptr);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_METADATA_LOADER_H_
