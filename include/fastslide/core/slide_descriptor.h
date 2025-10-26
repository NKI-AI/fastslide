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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_SLIDE_DESCRIPTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_SLIDE_DESCRIPTOR_H_

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/concepts/numeric.h"  // For aifocore::Size
#include "fastslide/image.h"
#include "fastslide/utilities/colors.h"

/**
 * @file slide_descriptor.h
 * @brief Core domain models for slide structure and metadata
 * 
 * This header defines the fundamental data structures that describe a slide's
 * pyramid structure, physical properties, and channel information. These are
 * pure data types with minimal dependencies, suitable for use in language
 * bindings and format plugins.
 */

namespace fastslide {
namespace core {

/// @brief Channel metadata structure for microscopy channels
///
/// Describes a single imaging channel including its name, biomarker,
/// visualization color, and acquisition parameters. Used for both
/// RGB and spectral imaging formats.
struct ChannelMetadata {
  std::string name;        ///< Channel name (e.g., "DAPI", "ATTO 550")
  std::string biomarker;   ///< Biomarker information (e.g., "Ki-67", "CD20")
  ColorRGB color;          ///< Display color for visualization
  uint32_t exposure_time;  ///< Exposure time in microseconds
  uint32_t signal_units;   ///< Signal units (bit depth related)
  std::map<std::string, std::string>
      additional;  ///< Format-specific additional metadata

  /// @brief Default constructor
  ChannelMetadata() : exposure_time(0), signal_units(0) {}

  /// @brief Constructor with basic fields
  ChannelMetadata(std::string name, std::string biomarker, ColorRGB color,
                  uint32_t exposure_time = 0, uint32_t signal_units = 0)
      : name(std::move(name)),
        biomarker(std::move(biomarker)),
        color(color),
        exposure_time(exposure_time),
        signal_units(signal_units) {}
};

/// @brief Bounding box for non-empty slide region
///
/// Represents the minimal bounding rectangle containing all non-empty tiles.
/// For formats like MRXS where tiles may not cover the full grid, this
/// identifies the actual tissue region. For formats with complete coverage,
/// this is simply (0, 0) with full slide dimensions.
struct SlideBounds {
  int64_t x;       ///< X coordinate of bounding box (level 0 coordinates)
  int64_t y;       ///< Y coordinate of bounding box (level 0 coordinates)
  int64_t width;   ///< Width of bounding box
  int64_t height;  ///< Height of bounding box

  /// @brief Check if bounds are valid (non-zero area)
  [[nodiscard]] bool IsValid() const { return width > 0 && height > 0; }

  /// @brief Default constructor (invalid bounds)
  SlideBounds() : x(0), y(0), width(0), height(0) {}

  /// @brief Constructor with values
  SlideBounds(int64_t x, int64_t y, int64_t width, int64_t height)
      : x(x), y(y), width(width), height(height) {}
};

/// @brief Pyramid level metadata
///
/// Describes a single level in the image pyramid including its dimensions
/// and downsample factor relative to the base level.
struct LevelInfo {
  ImageDimensions dimensions;  ///< Level dimensions in pixels
  double downsample_factor;    ///< Downsample factor relative to level 0

  /// @brief Default constructor
  LevelInfo() : downsample_factor(1.0) {}

  /// @brief Constructor with values
  LevelInfo(ImageDimensions dims, double downsample)
      : dimensions(dims), downsample_factor(downsample) {}
};

/// @brief Physical slide properties
///
/// Contains physical calibration and scanner metadata for the slide,
/// including microns per pixel, magnification, and scanner information.
struct SlideProperties {
  aifocore::Size<double, 2> mpp;   ///< Microns per pixel in X, Y direction
  double objective_magnification;  ///< Objective magnification (e.g., 20.0)
  std::string objective_name;      ///< Objective name (e.g., "Plan Apo 20x")
  std::string scanner_model;       ///< Scanner model/manufacturer
  std::optional<std::string> scan_date;  ///< Scan date/time if available
  SlideBounds bounds;                    ///< Bounding box for non-empty region

  /// @brief Default constructor
  SlideProperties() : mpp{0.0, 0.0}, objective_magnification(0.0), bounds() {}
};

// Note: ImageFormat is defined in fastslide/image.h, not duplicated here

/// @brief Complete slide descriptor
///
/// Aggregates all structural and physical metadata for a slide, providing
/// a complete description of the pyramid, channels, and properties.
/// This is the primary domain model for slide structure.
struct SlideDescriptor {
  std::vector<LevelInfo> levels;               ///< Pyramid level information
  std::vector<ChannelMetadata> channels;       ///< Channel metadata
  SlideProperties properties;                  ///< Physical properties
  ImageFormat format;                          ///< Image format type
  ImageDimensions tile_size;                   ///< Native tile size
  std::vector<std::string> associated_images;  ///< Associated image names

  /// @brief Default constructor
  SlideDescriptor() : format(ImageFormat::kRGB), tile_size{0, 0} {}

  /// @brief Get number of pyramid levels
  [[nodiscard]] size_t GetLevelCount() const { return levels.size(); }

  /// @brief Get number of channels
  [[nodiscard]] size_t GetChannelCount() const { return channels.size(); }

  /// @brief Check if slide has associated images
  [[nodiscard]] bool HasAssociatedImages() const {
    return !associated_images.empty();
  }

  /// @brief Get best level for a given downsample factor
  /// @param downsample Desired downsample factor
  /// @return Best matching level index
  [[nodiscard]] int GetBestLevelForDownsample(double downsample) const {
    if (levels.empty()) {
      return -1;
    }

    int best_level = 0;
    double min_diff = std::abs(levels[0].downsample_factor - downsample);

    for (size_t i = 1; i < levels.size(); ++i) {
      double diff = std::abs(levels[i].downsample_factor - downsample);
      if (diff < min_diff) {
        min_diff = diff;
        best_level = static_cast<int>(i);
      }
    }

    return best_level;
  }
};

}  // namespace core

// Import core types into fastslide namespace for backward compatibility
using core::ChannelMetadata;
using core::LevelInfo;
using core::SlideBounds;
using core::SlideDescriptor;
using core::SlideProperties;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_SLIDE_DESCRIPTOR_H_
