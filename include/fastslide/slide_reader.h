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
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"
#include "fastslide/metadata.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/slide_options.h"
#include "fastslide/utilities/colors.h"

// Forward declarations to avoid circular dependencies
namespace fastslide {
namespace runtime {
class Canvas;
}
class SlideImage;
class SelfImageView;
class IccTransform;
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
  /// @brief Virtual destructor (out-of-line to allow `unique_ptr` to an
  /// incomplete `SelfImageView`).
  virtual ~SlideReader();

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
  [[nodiscard]] virtual aifocore::Result<LevelInfo> GetLevelInfo(
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
  [[nodiscard]] virtual aifocore::Result<ImageDimensions>
  GetAssociatedImageDimensions(std::string_view name) const = 0;

  /// @brief Get best level for a given downsample factor
  /// @param downsample Desired downsample factor
  /// @return Best matching level
  [[nodiscard]] virtual int GetBestLevelForDownsample(double downsample) const;

  // =========================================================================
  // Multi-image container API
  // =========================================================================
  //
  // A `SlideReader` is the file/container; a `SlideImage` is one navigable
  // pyramid inside it. Most formats expose exactly one image; formats like
  // Olympus VSI expose several (navigator + per-region scans).
  //
  // The default implementations below make every existing reader report a
  // single image (`GetImage(0)` returns a `SelfImageView` that forwards
  // back to the reader's existing virtuals). Multi-image readers override
  // these four methods and own their own `SlideImage` instances.
  //
  // Note: associated images (label, macro, thumbnail) are a separate
  // concept exposed by `GetAssociatedImageNames`/`ReadAssociatedImage`;
  // they are not `SlideImage`s.

  /// @brief Number of navigable images (pyramids) in this file.
  ///
  /// Always >= 1. Default implementation returns 1 (single-image readers).
  [[nodiscard]] virtual int GetImageCount() const { return 1; }

  /// @brief Index of the "primary" image. The container's own
  /// `ReadRegion`, `GetLevelCount` etc. all target this image.
  ///
  /// Always a valid index in `[0, GetImageCount())`. Default returns 0.
  [[nodiscard]] virtual int GetPrimaryImageIndex() const { return 0; }

  /// @brief Human-readable name for each image, in index order.
  ///
  /// Default produces `{"image 0"}`; multi-image readers override to
  /// surface format-specific names (e.g. "navigator", "region 0").
  [[nodiscard]] virtual std::vector<std::string> GetImageNames() const;

  /// @brief Get a stable, non-owning handle to the i-th image.
  ///
  /// The returned pointer is owned by the reader and lives for as long
  /// as the reader does. Default implementation returns a lazily
  /// constructed `SelfImageView` for index 0 and `kNotFound` otherwise.
  /// Multi-image readers override and return one entry per pyramid.
  [[nodiscard]] virtual aifocore::Result<const SlideImage*> GetImage(
      int index) const;

  // =========================================================================
  // Two-Stage Tile Reading Pipeline
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
  [[nodiscard]] virtual aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
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
  /// - Stream results (if using streaming Canvas)
  ///
  /// @param plan Execution plan from PrepareRequest()
  /// @param writer Destination for decoded pixels
  /// @return Status indicating success or errors
  /// @note Default implementation returns UnimplementedError
  [[nodiscard]] virtual aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& canvas) const {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
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
  [[nodiscard]] virtual aifocore::Result<core::BatchTilePlan> PrepareBatch(
      std::span<const core::TileRequest> requests) const {
    core::BatchTilePlan batch;
    batch.plans.reserve(requests.size());

    for (const auto& req : requests) {
      core::TilePlan plan;
      AIFOCORE_ASSIGN_OR_RETURN(plan, PrepareRequest(req));
      batch.plans.push_back(plan);
    }

    return batch;
  }

  // =========================================================================
  // Region Reading API
  // =========================================================================

  /// @brief Read a region from the slide
  ///
  /// Routes through the two-stage pipeline: RegionToTileRequest ->
  /// PrepareRequest -> ExecutePlan. All format-specific logic lives in
  /// PrepareRequest and ExecutePlan overrides.
  ///
  /// @param region Region specification
  /// @return Image or error status
  [[nodiscard]] aifocore::Result<Image> ReadRegion(
      const RegionSpec& region) const;

  /// @brief Read an associated image
  /// @param name Associated image name
  /// @return Image or error status
  [[nodiscard]] virtual aifocore::Result<Image> ReadAssociatedImage(
      std::string_view name) const = 0;

  /// @brief Get format-specific metadata as key-value pairs
  /// @return Metadata map
  [[nodiscard]] virtual Metadata GetMetadata() const = 0;

  /// @brief Get the slide's embedded ICC color profile, if any.
  ///
  /// Returns the raw ICC profile bytes embedded in the slide (e.g. the level-0
  /// TIFF ICC tag, or the DICOM ICC profile in iSyntax and DICOM WSI).
  /// Consumers can use these bytes to run their own color management, or enable
  /// in-library conversion via `SetColorTransform`.
  ///
  /// Two failure modes are distinguished so callers can tell "this format does
  /// not support colour management yet" from "this particular slide has no
  /// profile":
  ///   - The default implementation returns `kUnimplemented`: the reader does
  ///     not extract ICC profiles for this format.
  ///   - Readers that do support extraction return `kNotFound` when the slide
  ///     simply carries no embedded profile.
  ///
  /// @return Raw ICC profile bytes; `kNotFound` when the format supports ICC
  ///         extraction but the slide has none; `kUnimplemented` when the
  ///         format does not support ICC extraction at all.
  [[nodiscard]] virtual aifocore::Result<std::vector<uint8_t>> GetIccProfile()
      const {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kUnimplemented,
        "ICC profile extraction is not implemented for this format");
  }

  /// @brief Get file format name
  /// @return Format name (e.g., "QPTIFF", "SVS", "NDPI")
  [[nodiscard]] virtual std::string GetFormatName() const = 0;

  /// @brief Get image format (RGB or Spectral)
  /// @return ImageFormat indicating whether this slide contains RGB or spectral
  /// data
  [[nodiscard]] virtual ImageFormat GetImageFormat() const = 0;

  /// @brief Get pixel data type (e.g. UInt8, UInt16)
  /// @return DataType for pixel values in this slide
  [[nodiscard]] virtual DataType GetDataType() const = 0;

  /// @brief Get optimal tile size for efficient reading
  /// @return Tile size (width, height) in pixels, or {0, 0} if not tiled
  [[nodiscard]] virtual ImageDimensions GetTileSize() const = 0;

  /// @brief Z (focal) / T (time) stack extent for the primary image.
  /// @return Stack info; default forwards to the primary `SlideImage`
  /// (`GetImage(GetPrimaryImageIndex())`), which reports a single plane for
  /// 2D formats.
  [[nodiscard]] virtual StackInfo GetStackInfo() const;

  /// @brief Get QuickHash (unique identifier for slide data)
  /// @return SHA-256 hash string (compatible with OpenSlide), or empty string
  /// if unavailable
  /// @details The quickhash is a unique identifier computed from:
  ///   - For MRXS: Slidedat.ini + all lowest resolution tile data
  ///   - For SVS/TIFF: TIFF header/metadata + lowest resolution tile data
  [[nodiscard]] virtual aifocore::Result<std::string> GetQuickHash() const {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kUnimplemented,
                                "GetQuickHash not implemented for this reader");
  }

  /// @brief Set tile cache for caching decoded internal tiles
  /// @param cache Shared pointer to tile cache (nullptr to disable caching)
  virtual void SetCache(std::shared_ptr<ITileCache> cache) {
    cache_ = std::move(cache);
  }

  /// @brief Get current tile cache
  /// @return Shared pointer to current cache (nullptr if disabled)
  [[nodiscard]] virtual std::shared_ptr<ITileCache> GetCache() const {
    return cache_;
  }

  /// @brief Check if caching is enabled
  /// @return True if cache is set and enabled
  [[nodiscard]] virtual bool IsCacheEnabled() const {
    return cache_ != nullptr;
  }

  /// @brief Enable in-library ICC color management for `ReadRegion`.
  ///
  /// Builds a color transform from the slide's embedded ICC profile (see
  /// `GetIccProfile`) to the given target color space and caches it on the
  /// reader. Once enabled, `ReadRegion` returns pixels already converted to the
  /// target space; the transform is applied in place on the decoded region
  /// buffer, so no extra allocation or copy is performed.
  ///
  /// Calling this on a slide without an embedded profile is a no-op (the reader
  /// keeps returning native pixels) and returns OK.
  ///
  /// @param target Target color space (sRGB by default via `kAutomatic`).
  /// @param intent ICC rendering intent.
  /// @param use_lut When true, build the 256^3 8-bit LUT fast path (48 MiB,
  ///        ~200 ms one-time cost) so 8-bit RGB(A) regions are color-managed
  ///        with an O(1) table gather instead of an lcms2 pass.
  /// @return OK on success or when the slide has no profile; an error only if
  ///         a profile is present but the transform could not be built.
  aifocore::Status SetColorTransform(
      ColorSpace target = ColorSpace::kSRGB,
      RenderingIntent intent = RenderingIntent::kPerceptual,
      bool use_lut = false);

  /// @brief Whether an ICC color transform is currently active.
  [[nodiscard]] bool IsColorTransformEnabled() const {
    return color_transform_ != nullptr;
  }

  /// @brief Utility function to clamp region to image bounds
  /// @param region Input region specification
  /// @param image_dims Image dimensions to clamp against
  /// @return Clamped region specification
  static RegionSpec ClampRegion(const RegionSpec& region,
                                const ImageDimensions& image_dims);

 protected:
  /// @brief Protected constructor (only derived classes can instantiate).
  /// Defined out-of-line to keep `SelfImageView` an incomplete type at the
  /// point of use here.
  SlideReader();

  // =========================================================================
  // Protected Helpers
  // =========================================================================

  /// @brief Convert RegionSpec to TileRequest
  ///
  /// Converts a region specification into a tile request suitable for
  /// the two-stage pipeline. This is a pure function that performs no I/O.
  ///
  /// @param region Region specification
  /// @return Tile request or error status
  [[nodiscard]] aifocore::Result<core::TileRequest> RegionToTileRequest(
      const RegionSpec& region) const;

  /// @brief Apply the active ICC color transform to a region in place.
  ///
  /// No-op (returns OK) when no transform is enabled or when the image is not a
  /// color-managed layout (see `IccTransform::ApplyInPlace`). Called by
  /// `ReadRegion` implementations just before returning the decoded image.
  ///
  /// @param image Decoded region image, transformed in place.
  /// @return OK on success or when skipped; error only on transform failure.
  [[nodiscard]] aifocore::Status MaybeApplyColorTransform(Image& image) const;

 private:
  /// @brief Optional tile cache for decoded internal tiles
  std::shared_ptr<ITileCache> cache_;

  /// @brief Optional ICC color transform applied during `ReadRegion`.
  /// Built by `SetColorTransform`; null when color management is disabled.
  /// Shared with this reader's `SlideImage`s so per-image reads apply it too.
  std::shared_ptr<IccTransform> color_transform_;

  /// @brief Lazily constructed default adapter returned by `GetImage(0)`
  /// when a reader does not override the multi-image API.
  ///
  /// Guarded by `self_image_view_once_` to keep `GetImage` thread-safe
  /// even though the underlying storage is logically `const`.
  mutable std::unique_ptr<SelfImageView> self_image_view_;
  mutable std::once_flag self_image_view_once_;
};

// Format plugins and ReaderRegistry are defined in fastslide/runtime/ and
// fastslide/readers/ (include separately when needed)

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_READER_H_
