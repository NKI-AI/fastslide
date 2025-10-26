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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_READER_H_

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/metadata.h"
#include "fastslide/slide_options.h"
#include "fastslide/utilities/cache.h"
#include "fastslide/utilities/colors.h"

// Forward declarations to avoid circular dependencies
namespace fastslide {
namespace runtime {
class TileWriter;
}
}  // namespace fastslide

namespace fastslide {

// ImageCoordinate and ImageDimensions are defined in fastslide/image.h
// Metadata and MetadataKeys are defined in fastslide/metadata.h
// ReaderRegistry is defined in fastslide/runtime/reader_registry.h (include
// separately when needed)

// Domain models are now in fastslide/core/
// Re-export core types for backward compatibility
using core::ChannelMetadata;
using core::LevelInfo;
using core::RegionSpec;
using core::SlideBounds;
using core::SlideProperties;

// ImageFormat remains in fastslide/image.h

/// @brief Abstract base class for slide readers
class SlideReader {
 public:
  /// @brief Virtual destructor
  virtual ~SlideReader() = default;

  /// @brief Delete copy constructor and assignment
  SlideReader(const SlideReader&) = delete;
  SlideReader& operator=(const SlideReader&) = delete;

  /// @brief Delete move constructor and assignment
  SlideReader(SlideReader&&) = delete;
  SlideReader& operator=(SlideReader&&) = delete;

  /// @brief Get number of pyramid levels
  /// @return Number of levels (level 0 is full resolution)
  [[nodiscard]] virtual int GetLevelCount() const = 0;

  /// @brief Get level information
  /// @param level Pyramid level
  /// @return Level information or error status
  [[nodiscard]] virtual absl::StatusOr<LevelInfo> GetLevelInfo(
      int level) const = 0;

  /// @brief Get slide physical properties
  /// @return Slide properties
  [[nodiscard]] virtual const SlideProperties& GetProperties() const = 0;

  /// @brief Get channel metadata for all channels
  /// @return Vector of channel metadata (index corresponds to channel index)
  [[nodiscard]] virtual std::vector<ChannelMetadata> GetChannelMetadata()
      const = 0;

  /// @brief Get available associated image names
  /// @return Vector of associated image names (e.g., "thumbnail", "macro")
  [[nodiscard]] virtual std::vector<std::string> GetAssociatedImageNames()
      const = 0;

  /// @brief Get dimensions of an associated image
  /// @param name Associated image name
  /// @return Image dimensions or error status
  [[nodiscard]] virtual absl::StatusOr<ImageDimensions>
  GetAssociatedImageDimensions(std::string_view name) const = 0;

  /// @brief Get best level for a given downsample factor
  /// @param downsample Desired downsample factor
  /// @return Best matching level
  [[nodiscard]] virtual int GetBestLevelForDownsample(double downsample) const;

  // =========================================================================
  // Two-Stage Tile Reading Pipeline (v2.0)
  // =========================================================================

  /// @brief Prepare a tile reading plan (stage 1: planning)
  ///
  /// Creates an execution plan for reading a tile region. This stage is:
  /// - Side-effect free (no I/O, no caching checks)
  /// - Unit testable (uses only metadata)
  /// - Fast (pure computation)
  ///
  /// The plan specifies WHAT tiles to read and HOW to transform them
  /// into the output image, but does not perform any I/O or decoding.
  ///
  /// Benefits:
  /// - Unit test planning logic without filesystem
  /// - Batch multiple requests before I/O
  /// - Estimate costs before execution
  /// - Cache and reuse plans
  ///
  /// @param request Tile request specification
  /// @return Execution plan or error status
  /// @note Default implementation returns UnimplementedError
  [[nodiscard]] virtual absl::StatusOr<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const {
    return absl::UnimplementedError(
        "PrepareRequest not implemented for this reader");
  }

  /// @brief Execute a tile reading plan (stage 2: execution)
  ///
  /// Executes a previously prepared plan by:
  /// 1. Reading compressed tile data (I/O)
  /// 2. Decompressing tiles (via decoders)
  /// 3. Transforming pixels (crop, scale, channel select)
  /// 4. Writing to destination (via writer)
  ///
  /// This stage performs all I/O and can:
  /// - Use cache to skip reads
  /// - Execute in parallel (if thread pool available)
  /// - Stream results (if using StreamingTileWriter)
  ///
  /// @param plan Execution plan from PrepareRequest()
  /// @param writer Destination for decoded pixels
  /// @return Status indicating success or errors
  /// @note Default implementation returns UnimplementedError
  [[nodiscard]] virtual absl::Status ExecutePlan(
      const core::TilePlan& plan, runtime::TileWriter& writer) const {
    return absl::UnimplementedError(
        "ExecutePlan not implemented for this reader");
  }

  /// @brief Prepare multiple requests in batch
  ///
  /// Creates a batch plan that may share tiles between requests,
  /// enabling optimizations like tile deduplication and sorted I/O.
  ///
  /// @param requests Vector of tile requests
  /// @return Batch execution plan or error
  /// @note Default implementation creates independent plans
  [[nodiscard]] virtual absl::StatusOr<core::BatchTilePlan> PrepareBatch(
      std::span<const core::TileRequest> requests) const {
    core::BatchTilePlan batch;
    batch.plans.reserve(requests.size());

    for (const auto& req : requests) {
      core::TilePlan plan;
      ASSIGN_OR_RETURN(plan, PrepareRequest(req));
      batch.plans.push_back(plan);
    }

    return batch;
  }

  // =========================================================================
  // Region Reading API
  // =========================================================================

  /// @brief Read a region from the slide
  /// @param region Region specification
  /// @return Image or error status
  ///
  /// @note **ARCHITECTURAL REQUIREMENT**: All readers MUST implement the
  ///       two-stage pipeline (PrepareRequest + ExecutePlan). This method
  ///       is now FINAL and routes through ReadRegionViaPipeline(), which
  ///       calls the two-stage pipeline.
  ///
  ///       **Status (v2.0)**:
  ///       - ✅ QPTIFF: Implements PrepareRequest/ExecutePlan
  ///       - ✅ SVS: Implements PrepareRequest/ExecutePlan
  ///       - ✅ MRXS: Implements PrepareRequest/ExecutePlan
  ///
  ///       All built-in readers now comply with v2.0 architecture.
  [[nodiscard]] virtual absl::StatusOr<Image> ReadRegion(
      const RegionSpec& region) const {
    return ReadRegionViaPipeline(region);
  }

  /// @brief Read a region from the slide with options
  /// @param region Region specification
  /// @param options Read options including coordinate space and color
  /// correction
  /// @return Image or error status
  [[nodiscard]] virtual absl::StatusOr<Image> ReadRegion(
      const RegionSpec& region, const RegionReadOptions& options) const {
    return ReadRegion(region);
  }

  /// @brief Read an associated image
  /// @param name Associated image name
  /// @return Image or error status
  [[nodiscard]] virtual absl::StatusOr<Image> ReadAssociatedImage(
      std::string_view name) const = 0;

  /// @brief Get format-specific metadata as key-value pairs
  /// @return Metadata map
  [[nodiscard]] virtual Metadata GetMetadata() const = 0;

  /// @brief Get file format name
  /// @return Format name (e.g., "QPTIFF", "SVS", "NDPI")
  [[nodiscard]] virtual std::string GetFormatName() const = 0;

  /// @brief Get image format (RGB or Spectral)
  /// @return ImageFormat indicating whether this slide contains RGB or spectral
  /// data
  [[nodiscard]] virtual ImageFormat GetImageFormat() const = 0;

  /// @brief Get optimal tile size for efficient reading
  /// @return Tile size (width, height) in pixels, or {0, 0} if not tiled
  [[nodiscard]] virtual ImageDimensions GetTileSize() const = 0;

  /// @brief Get QuickHash (unique identifier for slide data)
  /// @return SHA-256 hash string (compatible with OpenSlide), or empty string
  /// if unavailable
  /// @details The quickhash is a unique identifier computed from:
  ///   - For MRXS: Slidedat.ini + all lowest resolution tile data
  ///   - For SVS/TIFF: TIFF header/metadata + lowest resolution tile data
  [[nodiscard]] virtual absl::StatusOr<std::string> GetQuickHash() const {
    return MAKE_STATUS(absl::StatusCode::kUnimplemented,
                       "GetQuickHash not implemented for this reader");
  }

  /// @brief Set tile cache for caching decoded internal tiles
  /// @param cache Shared pointer to tile cache (nullptr to disable caching)
  virtual void SetCache(std::shared_ptr<TileCache> cache) {
    cache_ = std::move(cache);
  }

  /// @brief Get current tile cache
  /// @return Shared pointer to current cache (nullptr if disabled)
  [[nodiscard]] virtual std::shared_ptr<TileCache> GetCache() const {
    return cache_;
  }

  /// @brief Check if caching is enabled
  /// @return True if cache is set and enabled
  [[nodiscard]] virtual bool IsCacheEnabled() const {
    return cache_ != nullptr;
  }

  /// @brief Set which channels are visible during ReadRegion operations
  /// @param channel_indices Vector of channel indices to load
  /// (empty = all channels)
  /// @details Only the specified channels will be loaded and combined in
  /// ReadRegion. This can significantly improve performance for multichannel
  /// formats when only a subset of channels is needed for visualization.
  virtual void SetVisibleChannels(const std::vector<size_t>& channel_indices) {
    visible_channels_ = channel_indices;
  }

  /// @brief Get currently visible channel indices
  /// @return Vector of visible channel indices (empty = all channels visible)
  [[nodiscard]] virtual const std::vector<size_t>& GetVisibleChannels() const {
    return visible_channels_;
  }

  /// @brief Reset to show all channels
  virtual void ShowAllChannels() { visible_channels_.clear(); }

 protected:
  /// @brief Protected constructor (only derived classes can instantiate)
  SlideReader() = default;

  /// @brief Utility function to clamp region to image bounds
  /// @param region Input region specification
  /// @param image_dims Image dimensions to clamp against
  /// @return Clamped region specification
  static RegionSpec ClampRegion(const RegionSpec& region,
                                const ImageDimensions& image_dims);

  // =========================================================================
  // Protected Helpers for Two-Stage Pipeline Migration
  // =========================================================================

  /// @brief Read region via two-stage pipeline (helper for migration)
  ///
  /// This protected helper routes a RegionSpec through the two-stage
  /// pipeline (PrepareRequest → ExecutePlan), providing a migration path
  /// for readers to adopt the v2.0 architecture.
  ///
  /// **Migration Pattern:**
  /// 1. Implement `PrepareRequest()` (planning logic)
  /// 2. Implement `ExecutePlan()` (I/O + decode logic)
  /// 3. Update `ReadRegion()` to call this helper
  ///
  /// **Example Usage:**
  /// ```cpp
  /// absl::StatusOr<Image> MyReader::ReadRegion(const RegionSpec& region) const
  /// {
  ///   // Route through two-stage pipeline
  ///   return ReadRegionViaPipeline(region);
  /// }
  /// ```
  ///
  /// @param region Region specification
  /// @return Image or error status
  /// @note Requires PrepareRequest() and ExecutePlan() to be implemented
  [[nodiscard]] absl::StatusOr<Image> ReadRegionViaPipeline(
      const RegionSpec& region) const;

  /// @brief Convert RegionSpec to TileRequest (helper)
  ///
  /// Converts a region specification into a tile request suitable for
  /// the two-stage pipeline. This is a pure function that performs no I/O.
  ///
  /// @param region Region specification
  /// @return Tile request or error status
  [[nodiscard]] absl::StatusOr<core::TileRequest> RegionToTileRequest(
      const RegionSpec& region) const;

  /// @brief Channel indices to load (empty = all channels)
  /// @details Protected so derived classes can access for implementing
  /// selective loading
  std::vector<size_t> visible_channels_;

 private:
  /// @brief Optional tile cache for decoded internal tiles
  std::shared_ptr<TileCache> cache_;
};

// Format plugins and ReaderRegistry are defined in fastslide/runtime/ and
// fastslide/readers/ (include separately when needed)

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_READER_H_
