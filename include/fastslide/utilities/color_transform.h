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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLOR_TRANSFORM_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLOR_TRANSFORM_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/slide_options.h"

namespace fastslide {

/// @brief ICC color transform from an embedded slide profile to a target
/// color space (sRGB by default), backed by Little CMS (lcms2).
///
/// A transform is built once from a source ICC profile and reused for every
/// region. `ApplyInPlace` runs the transform directly on the decoded pixel
/// buffer with source == destination, so no extra allocation or copy is made.
///
/// Only interleaved 8-/16-bit RGB and RGBA images are color-managed; any other
/// layout, format, or data type is left untouched (`ApplyInPlace` is a no-op
/// that returns OK). This matches the brightfield WSI use case and leaves
/// spectral/float imagery unmodified.
///
/// An optional precomputed 8-bit LUT fast path can be enabled at construction
/// (`build_lut`). It materializes the full 256^3 RGB->RGB mapping produced by
/// the lcms2 `TYPE_RGB_8` transform into a 48 MiB table, so per-pixel color
/// management for 8-bit interleaved RGB(A) images becomes a single O(1) table
/// gather instead of an lcms2 (potentially CLUT-interpolated) pass. The table
/// is filled once (about 200 ms) from that same transform, so LUT output is
/// byte-identical to the lcms2 8-bit path. 16-bit, planar, or otherwise
/// unsupported images always fall back to lcms2.
///
/// The concrete lcms2 per-format transforms are created lazily and cached, so
/// the object is safe to share across threads (`ApplyInPlace` is `const` and
/// internally synchronized).
class IccTransform {
 public:
  IccTransform(const IccTransform&) = delete;
  IccTransform& operator=(const IccTransform&) = delete;
  IccTransform(IccTransform&&) = delete;
  IccTransform& operator=(IccTransform&&) = delete;
  ~IccTransform();

  /// @brief Build a transform from ICC profile bytes to a target color space.
  /// @param profile_bytes Raw embedded ICC profile bytes (copied internally).
  /// @param target        Target color space. `kAutomatic`/`kRGB`/`kSRGB` map
  ///                      to sRGB; `kLinear` maps to a linear-light RGB space
  ///                      built from the sRGB primaries.
  /// @param intent        ICC rendering intent.
  /// @param build_lut     When true, eagerly build the 256^3 8-bit RGB->RGB
  ///                      LUT fast path (48 MiB, ~200 ms one-time cost). When
  ///                      false, every image uses the lcms2 transform.
  /// @return An owned transform, or an error status if the profile is invalid.
  [[nodiscard]] static aifocore::Result<std::unique_ptr<IccTransform>> Create(
      std::span<const uint8_t> profile_bytes, ColorSpace target,
      RenderingIntent intent, bool build_lut = false);

  /// @brief Apply the transform in place on an interleaved RGB(A) image.
  /// @param image Image whose pixel buffer is transformed in place.
  /// @return OK on success (including the no-op skip cases), or an error if the
  ///         lcms2 transform could not be created for the image's format.
  [[nodiscard]] aifocore::Status ApplyInPlace(Image& image) const;

 private:
  /// @brief Custom deleter for an lcms2 `cmsHPROFILE` handle.
  struct ProfileDeleter {
    void operator()(void* handle) const noexcept;
  };

  /// @brief Custom deleter for an lcms2 `cmsHTRANSFORM` handle.
  struct TransformDeleter {
    void operator()(void* handle) const noexcept;
  };

  using ProfileHandle = std::unique_ptr<void, ProfileDeleter>;
  using TransformHandle = std::unique_ptr<void, TransformDeleter>;

  /// @brief A cached lcms2 transform for a specific image format/data type.
  struct CachedTransform {
    ImageFormat format;
    DataType dtype;
    TransformHandle handle;
  };

  IccTransform(ProfileHandle src_profile, ProfileHandle dst_profile,
               RenderingIntent intent);

  /// @brief Look up (or lazily build and cache) a transform for a format/dtype.
  /// @return The lcms2 `cmsHTRANSFORM` handle, or nullptr if the combination is
  ///         not color-managed (unsupported format/dtype).
  [[nodiscard]] aifocore::Result<void*> GetOrBuildTransform(
      ImageFormat format, DataType dtype) const;

  /// @brief Build the 256^3 8-bit RGB->RGB LUT from the lcms2 transform.
  ///
  /// Runs the `TYPE_RGB_8` transform over every possible input triple so LUT
  /// output matches the lcms2 8-bit path exactly. Populates `lut8_` on success;
  /// leaves it empty (silent fallback to lcms2) on any failure.
  void BuildLut8();

  /// @brief Apply the 8-bit LUT gather to an interleaved RGB/RGBA image.
  ///
  /// Precondition: `lut8_` is populated and `image` is 8-bit interleaved
  /// `kRGB`/`kRGBA`. Alpha (RGBA) is copied through untouched.
  void ApplyLut8(Image& image) const;

  ProfileHandle src_profile_;
  ProfileHandle dst_profile_;
  RenderingIntent intent_;

  /// @brief Optional 256^3 RGB->RGB byte table (3 bytes/entry); empty when the
  /// LUT fast path is disabled or could not be built.
  std::vector<uint8_t> lut8_;

  mutable std::mutex mutex_;
  mutable std::vector<CachedTransform> cache_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_COLOR_TRANSFORM_H_
