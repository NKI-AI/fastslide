// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_IMAGE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_IMAGE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"

namespace fastslide {

namespace runtime {
class Canvas;
}  // namespace runtime

class IccTransform;

/// @brief One navigable pyramid inside a slide file ("series" / "scene").
///
/// A `SlideReader` is the file/container; a `SlideImage` is one navigable
/// pyramid inside it. Most formats expose exactly one image (the slide
/// itself). Formats like Olympus VSI store multiple independent pyramids
/// per file (a low-resolution navigator plus one main scan per imaged
/// region); each becomes a separate `SlideImage` with its own dimensions,
/// pyramid levels, MPP and channel layout.
///
/// Handles are stateless: callers retrieve a `SlideImage*` from
/// `SlideReader::GetImage(i)` and operate on it directly, with no
/// "current image" cursor on the container. This makes the API safe to
/// use from multiple threads in parallel without synchronisation.
///
/// `SlideImage` deliberately does not own a cache, file handles, or
/// associated thumbnails: those are container-level concerns and stay
/// on `SlideReader`.
class SlideImage {
 public:
  virtual ~SlideImage() = default;

  /// Not copyable or movable; held by reference via the container.
  SlideImage(const SlideImage&) = delete;
  SlideImage& operator=(const SlideImage&) = delete;
  SlideImage(SlideImage&&) = delete;
  SlideImage& operator=(SlideImage&&) = delete;

  /// @brief Human-readable image name (e.g. "image 0", "navigator",
  /// "region 0"). Stable across opens; safe to display in UIs.
  [[nodiscard]] virtual std::string GetName() const = 0;

  /// @brief Number of pyramid levels in this image (level 0 = full
  /// resolution).
  [[nodiscard]] virtual int GetLevelCount() const = 0;

  /// @brief Dimensions and downsample factor for a single level.
  [[nodiscard]] virtual aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const = 0;

  /// @brief Per-image physical properties (MPP, bounds, ...).
  [[nodiscard]] virtual const SlideProperties& GetProperties() const = 0;

  /// @brief Channel layout for this image (one entry per channel).
  [[nodiscard]] virtual std::vector<ChannelMetadata> GetChannelMetadata()
      const = 0;

  /// @brief RGB / RGBA / spectral / grayscale.
  [[nodiscard]] virtual ImageFormat GetImageFormat() const = 0;

  /// @brief Pixel sample data type (e.g. kUInt8, kUInt16).
  [[nodiscard]] virtual DataType GetDataType() const = 0;

  /// @brief Native tile size for efficient reads, or {0, 0} if untiled.
  [[nodiscard]] virtual ImageDimensions GetTileSize() const = 0;

  /// @brief Z (focal) / T (time) stack extent and spacing for this image.
  ///
  /// Default reports a single plane (`z_count == t_count == 1`). Multi-plane
  /// images override this; callers select a plane via `RegionSpec::plane`.
  [[nodiscard]] virtual StackInfo GetStackInfo() const { return {}; }

  /// @brief Plan a tile/region read (stage 1: side-effect-free).
  [[nodiscard]] virtual aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const = 0;

  /// @brief Execute a prepared plan (stage 2: I/O + decode + blit).
  [[nodiscard]] virtual aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& canvas) const = 0;

  // ---------------------------------------------------------------------
  // Shared concrete helpers (no further overriding needed).
  // ---------------------------------------------------------------------

  /// @brief Read a rectangular region from this image. Routes through the
  /// two-stage pipeline (`PrepareRequest` -> `ExecutePlan`).
  [[nodiscard]] aifocore::Result<Image> ReadRegion(
      const RegionSpec& region) const;

  /// @brief Pick the pyramid level whose downsample is closest to
  /// `downsample`. Falls back to level 0 if no levels are present or
  /// `GetLevelInfo` fails for every level.
  [[nodiscard]] int GetBestLevelForDownsample(double downsample) const;

  /// @brief Restrict which channels `ReadRegion` returns (empty = all).
  /// Per-image: setting this on image i does not affect image j.
  void SetVisibleChannels(std::vector<size_t> channel_indices) {
    visible_channels_ = std::move(channel_indices);
  }

  /// @brief Currently-visible channel indices (empty = all visible).
  [[nodiscard]] const std::vector<size_t>& GetVisibleChannels() const {
    return visible_channels_;
  }

  /// @brief Reset to "show all channels".
  void ShowAllChannels() { visible_channels_.clear(); }

  /// @brief Inject the ICC color transform to apply during `ReadRegion`.
  ///
  /// Set by the owning `SlideReader::SetColorTransform` so per-image reads
  /// produce the same color-managed output as the container's `ReadRegion`.
  /// Passing `nullptr` disables color management for this image. Treated as an
  /// injected service (like a cache), hence const with a mutable backing field.
  void SetColorTransform(std::shared_ptr<const IccTransform> transform) const {
    color_transform_ = std::move(transform);
  }

 protected:
  SlideImage() = default;

  /// @brief Convert a `RegionSpec` into a `TileRequest` populated with the
  /// per-image visible channels and fractional region bounds. Pure (no
  /// I/O), shared with both `ReadRegion` and external callers that want
  /// to drive the plan/execute pipeline manually.
  [[nodiscard]] aifocore::Result<core::TileRequest> RegionToTileRequest(
      const RegionSpec& region) const;

  /// @brief Apply the injected ICC color transform to a region in place.
  /// No-op (returns OK) when no transform is set or the layout is not
  /// color-managed. Called by `ReadRegion` before returning.
  [[nodiscard]] aifocore::Status MaybeApplyColorTransform(Image& image) const;

  std::vector<size_t> visible_channels_;

 private:
  /// @brief Optional ICC color transform injected by the owning reader.
  mutable std::shared_ptr<const IccTransform> color_transform_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SLIDE_IMAGE_H_
