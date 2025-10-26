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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_H_

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fastslide/image.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/readers/tiff_reader_factory.h"
#include "fastslide/utilities/colors.h"

/**
 * @file qptiff.h
 * @brief PerkinElmer QPTIFF slide reader
 * 
 * This header defines the QpTiffReader class for reading PerkinElmer QPTIFF
 * (Quantitative Pathology TIFF) whole slide images. QPTIFF is used for
 * multiplex immunofluorescence and spectral imaging.
 * 
 * **Format Details:**
 * - Multi-page TIFF with one page per channel per pyramid level
 * - XML metadata embedded in ImageDescription tags
 * - Supports both RGB brightfield and multi-channel fluorescence
 * - Channel metadata includes biomarker info, exposure times, and colors
 * - Optional pyramid levels for each channel
 * 
 * **Features:**
 * - Multi-channel spectral image support
 * - Flexible planar configuration (interleaved or separated)
 * - Selective channel loading for performance
 * - Two-stage pipeline (PrepareRequest + ExecutePlan)
 * - Per-channel metadata (biomarkers, colors, exposure times)
 * 
 * **Channel Handling:**
 * QPTIFF supports two image formats:
 * - RGB: Single 3-channel brightfield image
 * - Spectral: Multiple single-channel fluorescence images
 * 
 * For spectral images, channels can be:
 * - Loaded individually or in combination
 * - Displayed with custom colors
 * - Processed separately for quantitative analysis
 * 
 * **Usage:**
 * ```cpp
 * auto reader_or = QpTiffReader::Create("/path/to/file.qptiff");
 * if (!reader_or.ok()) {
 *   // Handle error
 * }
 * auto reader = std::move(*reader_or);
 * 
 * // Set visible channels for fluorescence images
 * reader->SetVisibleChannels({0, 2, 4});  // Load channels 0, 2, 4 only
 * 
 * auto image = reader->ReadRegion({.top_left = {0, 0},
 *                                   .size = {512, 512},
 *                                   .level = 0});
 * ```
 * 
 * @see TiffBasedReader for the base class
 * @see TiffReaderFactory for the CRTP factory pattern used
 */

namespace fs = std::filesystem;

namespace fastslide {

/// @brief Channel metadata for QPTIFF
struct QpTiffChannelInfo {
  uint16_t page;             ///< TIFF page number
  uint32_t width;            ///< Channel width
  uint32_t height;           ///< Channel height
  std::string name;          ///< Channel name (e.g., "ATTO 550")
  std::string biomarker;     ///< Biomarker information (e.g., "Ki-67")
  uint32_t exposure_time;    ///< Exposure time in microseconds
  ColorRGB color;            ///< Channel color from XML
  uint32_t signal_units;     ///< Signal units (bit depth related)
  bool tiled;                ///< Whether this channel's page is tiled
  bool allow_random_access;  ///< Whether this channel allows random access
  // (true for tiled, false for non-tiled)
};

/// @brief General QPTIFF metadata extracted from XML
struct QpTiffMetadata {
  std::string acquisition_software;  ///< Software used for acquisition
  std::string camera_name;           ///< Camera model name
  std::string camera_type;           ///< Camera type identifier
  std::string instrument_type;       ///< Instrument type
  std::string lamp_type;             ///< Lamp type used
  std::string slide_id;              ///< Slide identifier
  std::string computer_name;         ///< Computer used for acquisition
  std::string study_name;            ///< Study name
  std::string operator_name;         ///< Operator name
  uint16_t bit_depth;                ///< Camera bit depth
  uint16_t binning;                  ///< Camera binning factor
  uint16_t gain;                     ///< Camera gain
  uint16_t offset_counts;            ///< Camera offset counts

  /// @brief Default constructor
  QpTiffMetadata() : bit_depth(0), binning(0), gain(0), offset_counts(0) {}
};

/// @brief Pyramid level metadata for QPTIFF
struct QpTiffLevelInfo {
  std::vector<uint16_t> pages;    ///< TIFF pages for this level
  ImageDimensions size = {0, 0};  ///< Level dimensions (width, height)
  bool tiled;                     ///< Whether all pages in this level are tiled
  bool allow_random_access;       ///< Whether this level allows random access

  /// @brief Reserve space for vectors
  /// @param n Number of elements to reserve
  void Reserve(size_t n) { pages.reserve(n); }
};

/// @brief Associated image metadata for QPTIFF
struct QpTiffAssociatedInfo {
  uint16_t page;                  ///< TIFF page number
  ImageDimensions size = {0, 0};  ///< Image dimensions (width, height)
};

/// @brief Slide metadata structure
struct SlideMetadata {
  double mpp_x;                ///< Microns per pixel in X direction
  double mpp_y;                ///< Microns per pixel in Y direction
  double magnification;        ///< Objective magnification
  std::string objective_name;  ///< Objective name (e.g., "10x")

  /// @brief Default constructor
  SlideMetadata() : mpp_x(0.0), mpp_y(0.0), magnification(0.0) {}
};

/// @brief QPTIFF reader class implementing the SlideReader interface
class QpTiffReader : public TiffBasedReader,
                     public TiffReaderFactory<QpTiffReader> {
 public:
  /// @brief Factory method to create a QpTiffReader instance
  /// @param filename Path to the QPTIFF file
  /// @return StatusOr containing the reader instance or an error
  static absl::StatusOr<std::unique_ptr<QpTiffReader>> Create(
      std::string_view filename);

  /// @brief Destructor
  ~QpTiffReader() override = default;

  // SlideReader interface implementation
  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] absl::StatusOr<LevelInfo> GetLevelInfo(
      int level) const override;
  [[nodiscard]] const SlideProperties& GetProperties() const override;
  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;
  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override;
  [[nodiscard]] absl::StatusOr<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;
  [[nodiscard]] absl::StatusOr<RGBImage> ReadAssociatedImage(
      std::string_view name) const override;

  [[nodiscard]] Metadata GetMetadata() const override;

  [[nodiscard]] std::string GetFormatName() const override { return "QPTIFF"; }

  [[nodiscard]] ImageFormat GetImageFormat() const override { return format_; }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  // Two-stage pipeline implementation
  [[nodiscard]] absl::StatusOr<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] absl::Status ExecutePlan(
      const core::TilePlan& plan, runtime::TileWriter& writer) const override;

  // Additional QPTIFF-specific methods
  /// @brief Get QPTIFF-specific metadata
  /// @return QPTIFF metadata structure with camera info, etc.
  [[nodiscard]] const QpTiffMetadata& GetQpTiffMetadata() const {
    return qptiff_metadata_;
  }

  // Additional interface methods
  /// @brief Get pyramid metadata
  /// @return Reference to pyramid levels vector
  [[nodiscard]] const std::vector<QpTiffLevelInfo>& GetPyramid() const {
    return pyramid_;
  }

  /// @brief Get associated images metadata
  /// @return Reference to associated images map
  [[nodiscard]] const std::map<std::string, QpTiffAssociatedInfo>&
  GetAssociatedImages() const {
    return associated_images_;
  }

  /// @brief Get slide metadata
  /// @return Reference to slide metadata
  [[nodiscard]] const SlideMetadata& GetSlideMetadata() const {
    return metadata_;
  }

  /// @brief Set output planar configuration for ReadRegion
  /// @param config Planar configuration (kContig for interleaved,
  /// kSeparate for channel-separated)
  /// @details For spectral images, kSeparate is often preferred for
  /// per-channel processing, while kContig provides better cache locality
  /// for pixel-wise operations.
  void SetOutputPlanarConfig(PlanarConfig config) {
    output_planar_config_ = config;
  }

  /// @brief Get current output planar configuration
  /// @return Current planar configuration setting
  [[nodiscard]] PlanarConfig GetOutputPlanarConfig() const {
    return output_planar_config_;
  }

  /// @brief Get the actual number of channels that will be in the resulting image
  /// @return Actual channel count (3 for RGB, logical count for spectral)
  /// @details For RGB images, returns 3 even though there's only 1 logical channel.
  /// For spectral images, returns the number of spectral channels.
  [[nodiscard]] uint32_t GetActualChannelCount() const;

  /// @brief Extract text of a simple child XML tag
  /// @param node XML node
  /// @param tag Tag name
  /// @return Text content
  static std::string GetText(const void* node, const char* tag);

 private:
  /// @brief Allow factory access to private constructor and methods
  friend class TiffReaderFactory<QpTiffReader>;

  /// @brief Private constructor - use Create() factory method instead
  /// @param filename Path to the QPTIFF file
  explicit QpTiffReader(std::string_view filename);

  std::vector<QpTiffLevelInfo>
      pyramid_;  ///< Pyramid levels (index = level number)
  std::map<std::string, QpTiffAssociatedInfo>
      associated_images_;           ///< Associated images
  SlideMetadata metadata_;          ///< Slide metadata
  QpTiffMetadata qptiff_metadata_;  ///< QPTIFF-specific metadata
  std::vector<QpTiffChannelInfo>
      channels_;  ///< Channel information for all channels
  PlanarConfig output_planar_config_ =
      PlanarConfig::kSeparate;  ///< Default to kSeparate for spectral images
  ImageFormat format_ =
      ImageFormat::kSpectral;  ///< Image format (RGB or Spectral)

  /// @brief Process TIFF metadata and build pyramid structure
  /// @return Status indicating success or failure
  absl::Status ProcessMetadata();

  /// @brief Convert internal structures to new interface structures
  void PopulateSlideProperties();
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_H_
