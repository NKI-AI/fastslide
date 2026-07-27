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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_OPTIONS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_OPTIONS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "fastslide/utilities/colors.h"

namespace fastslide {

// Forward declarations
class TileCache;

/// @brief Color space for image data
enum class ColorSpace {
  kRGB,       ///< Standard RGB color space
  kLinear,    ///< Linear color space (no gamma correction)
  kSRGB,      ///< sRGB color space
  kAutomatic  ///< Automatically determine from metadata
};

/// @brief ICC rendering intent used when applying color transforms
///
/// Mirrors the four ICC rendering intents. Perceptual is the default and
/// matches OpenSlide's behaviour when converting through an embedded slide
/// profile to sRGB.
enum class RenderingIntent {
  kPerceptual,            ///< Perceptual (default, OpenSlide-compatible)
  kRelativeColorimetric,  ///< Relative colorimetric
  kSaturation,            ///< Saturation
  kAbsoluteColorimetric   ///< Absolute colorimetric
};

/// @brief Bundle of optional dependencies that can be injected into readers
///
/// This struct allows readers to receive shared services without tight
/// coupling. All fields are optional; readers should provide sensible defaults
/// if not set.
///
/// Example usage:
/// @code
/// DependencyBundle deps;
/// deps.tile_cache = std::make_shared<TileCache>(1024 * 1024 * 1024); // 1GB
/// deps.background_color = ColorRGB{255, 255, 255};  // White background
///
/// SlideOpenOptions options;
/// options.dependencies = deps;
///
/// auto reader = registry.CreateReader("slide.mrxs", options);
/// @endcode
struct DependencyBundle {
  /// @brief Optional tile cache for decoded tiles
  ///
  /// Readers can use this cache to store decoded tiles for faster access.
  /// If nullptr, readers should manage their own caching or disable caching.
  std::shared_ptr<TileCache> tile_cache;

  /// @brief Background color for empty regions
  ///
  /// Used when filling regions that don't have tile data (e.g., sparse MRXS
  /// tiles). If not set, readers should use their own default (typically
  /// white).
  std::optional<ColorRGB> background_color;

  /// @brief Maximum number of threads for parallel operations
  ///
  /// Readers can use this hint to limit parallelism for tile decoding or
  /// region stitching. If 0 or not set, readers should use their own default.
  uint32_t max_threads = 0;

  /// @brief Enable debug logging
  ///
  /// When true, readers may emit additional diagnostic information.
  bool debug_mode = false;

  /// @brief Default constructor
  DependencyBundle() = default;
};

/// @brief Options for opening slide files
///
/// This struct provides a forward-compatible way to pass options when opening
/// slides. New options can be added without breaking binary compatibility.
///
/// Example usage:
/// @code
/// SlideOpenOptions options;
/// options.enable_caching = true;
/// options.cache_size_mb = 512;
/// options.dependencies.max_threads = 4;
///
/// auto reader = SlideReaderRegistry::GetInstance().CreateReader(
///     "slide.svs", options);
/// @endcode
struct SlideOpenOptions {
  /// @brief Enable internal tile caching
  ///
  /// If true and no external cache is provided via dependencies, the reader
  /// should create its own internal cache.
  bool enable_caching = true;

  /// @brief Cache size in megabytes
  ///
  /// Hint for cache size if the reader creates its own cache. Ignored if
  /// an external cache is provided via dependencies.
  uint32_t cache_size_mb = 256;

  /// @brief Read-only mode
  ///
  /// If true, the reader should open files in read-only mode and not attempt
  /// any write operations (useful for network-mounted or locked files).
  bool read_only = true;

  /// @brief Preferred color space for image data
  ///
  /// Readers should attempt to provide image data in this color space.
  /// If conversion is not possible, readers should use their native format.
  ColorSpace color_space = ColorSpace::kAutomatic;

  /// @brief Apply the slide's embedded ICC profile during region decode
  ///
  /// When true and the slide carries an embedded ICC profile, `ReadRegion`
  /// returns pixels already converted to `color_space` (sRGB by default) using
  /// the embedded profile. The transform is built once per reader and applied
  /// in place on the decoded region buffer, so there is no extra copy. Slides
  /// without an embedded profile, or non-RGB(A)/8-16-bit outputs, are returned
  /// unchanged.
  bool apply_icc = false;

  /// @brief Rendering intent used when `apply_icc` is enabled
  RenderingIntent rendering_intent = RenderingIntent::kPerceptual;

  /// @brief Use the precomputed 8-bit LUT fast path when `apply_icc` is enabled
  ///
  /// When true, a 256^3 RGB->RGB table (48 MiB, ~200 ms one-time build) is
  /// materialized so 8-bit RGB(A) regions are color-managed with an O(1) table
  /// gather instead of an lcms2 pass. Output is byte-identical to the lcms2
  /// path. Ignored when `apply_icc` is false or the slide has no profile.
  bool icc_use_lut = false;

  /// @brief Optional dependency bundle for shared services
  ///
  /// Allows injection of shared caches, thread pools, or other services.
  DependencyBundle dependencies;

  /// @brief Default constructor
  SlideOpenOptions() = default;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_OPTIONS_H_
