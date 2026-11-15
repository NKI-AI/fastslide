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

#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "fastslide/image.h"
#include "fastslide/python/cache.h"
#include "fastslide/slide_image.h"
#include "fastslide/slide_reader.h"

namespace nb = nanobind;

namespace fastslide::python {

using fastslide::SlideImage;
using fastslide::SlideReader;
using fastslide::StackInfo;

/// @brief Lazy-loading dictionary-like wrapper for associated images
class AssociatedImages {
 private:
  std::weak_ptr<SlideReader> reader_;
  mutable std::unordered_map<std::string, std::shared_ptr<fastslide::Image>>
      cache_;
  mutable std::vector<std::string> available_names_;
  mutable bool names_loaded_ = false;

  [[nodiscard]] std::shared_ptr<SlideReader> GetReader() const;
  void EnsureNamesLoaded() const;

 public:
  explicit AssociatedImages(std::shared_ptr<SlideReader> reader);

  /// @brief Get associated image by name (lazy loading)
  [[nodiscard]] std::shared_ptr<fastslide::Image> GetItem(
      const std::string& name) const;

  /// @brief Check if associated image exists
  [[nodiscard]] bool Contains(const std::string& name) const;

  /// @brief Get list of associated image names
  [[nodiscard]] std::vector<std::string> Keys() const;

  /// @brief Get dimensions of associated image without loading it
  [[nodiscard]] nb::tuple GetDimensions(const std::string& name) const;

  /// @brief Get number of cached images
  [[nodiscard]] size_t GetCacheSize() const;

  /// @brief Clear the image cache
  void ClearCache() const;
};

/// @brief Lazy-loading dictionary-like wrapper for associated data (XML,
/// binary)
class AssociatedData {
 private:
  std::weak_ptr<SlideReader> reader_;
  mutable std::unordered_map<std::string, std::optional<nb::object>> cache_;
  mutable std::vector<std::string> available_names_;
  mutable bool names_loaded_ = false;

  [[nodiscard]] std::shared_ptr<SlideReader> GetReader() const;
  void EnsureNamesLoaded() const;

 public:
  explicit AssociatedData(std::shared_ptr<SlideReader> reader);

  /// @brief Get associated data by name (lazy loading)
  [[nodiscard]] nb::object GetItem(const std::string& name) const;

  /// @brief Check if associated data exists
  [[nodiscard]] bool Contains(const std::string& name) const;

  /// @brief Get list of associated data names (non-image)
  [[nodiscard]] std::vector<std::string> Keys() const;

  /// @brief Get data type without loading
  [[nodiscard]] std::string GetType(const std::string& name) const;

  /// @brief Get number of cached data items
  [[nodiscard]] size_t GetCacheSize() const;

  /// @brief Clear the data cache
  void ClearCache() const;
};

/// @brief Lightweight Python view of one `SlideImage` inside a slide.
///
/// Holds a weak reference to the owning reader and the image index, so
/// the C++ side stays the source of truth. Methods raise `RuntimeError`
/// if the underlying slide has been closed.
class SlideImageView {
 public:
  SlideImageView(std::weak_ptr<SlideReader> reader, int index);

  /// @brief Image name (e.g. "navigator", "region 0").
  [[nodiscard]] std::string GetName() const;

  /// @brief Number of pyramid levels.
  [[nodiscard]] int GetLevelCount() const;

  /// @brief Level-0 dimensions as `(width, height)`.
  [[nodiscard]] nb::tuple GetDimensions() const;

  /// @brief Tuple of `(width, height)` for each level.
  [[nodiscard]] nb::tuple GetLevelDimensions() const;

  /// @brief Tuple of downsample factors for each level.
  [[nodiscard]] nb::tuple GetLevelDownsamples() const;

  /// @brief Microns-per-pixel as `(mpp_x, mpp_y)`.
  [[nodiscard]] nb::tuple GetMpp() const;

  /// @brief Read a region using level-native coordinates.
  /// @param z Focal-plane index (0 = first plane).
  /// @param t Time-point index (0 = first time point).
  [[nodiscard]] std::shared_ptr<fastslide::Image> ReadRegion(
      uint32_t x, uint32_t y, uint32_t width, uint32_t height, int level,
      uint32_t z = 0, uint32_t t = 0);

  /// @brief Z (focal) / T (time) stack extent and spacing for this image.
  [[nodiscard]] StackInfo GetStackInfo() const;

  /// @brief Per-channel metadata dictionaries for this image.
  [[nodiscard]] nb::list GetChannelMetadata() const;

  /// @brief Number of channels a `read_region` of this image returns.
  [[nodiscard]] int GetNumChannels() const;

  /// @brief Pick the level closest to a desired downsample factor.
  [[nodiscard]] int GetBestLevelForDownsample(double downsample) const;

  /// @brief Image index inside the container.
  [[nodiscard]] int GetIndex() const { return index_; }

 private:
  [[nodiscard]] std::shared_ptr<SlideReader> GetReader() const;
  [[nodiscard]] const SlideImage* GetImage() const;

  std::weak_ptr<SlideReader> reader_;
  int index_;
};

/// @brief Indexable collection of `SlideImageView`s, like `slide.images`.
class SlideImages {
 public:
  explicit SlideImages(std::weak_ptr<SlideReader> reader);

  [[nodiscard]] size_t Len() const;
  [[nodiscard]] SlideImageView GetItem(int index) const;
  [[nodiscard]] int GetPrimaryIndex() const;
  [[nodiscard]] std::vector<std::string> Names() const;

 private:
  [[nodiscard]] std::shared_ptr<SlideReader> GetReader() const;
  std::weak_ptr<SlideReader> reader_;
};

/// @brief Main slide reader class with pythonic interface
class FastSlide {
 private:
  std::shared_ptr<SlideReader> reader_;
  std::unique_ptr<AssociatedImages> associated_images_;
  std::unique_ptr<AssociatedData> associated_data_;
  std::unique_ptr<SlideImages> images_;
  std::shared_ptr<fastslide::runtime::ITileCache> cache_;
  bool is_closed_;
  std::string source_path_;

 public:
  /// @brief Private constructor - use factory methods instead
  explicit FastSlide(std::shared_ptr<SlideReader> reader,
                     const std::string& source_path);

  /// @brief Factory method to create from file path
  /// @param file_path Path to the slide file.
  /// @param apply_icc When true and the slide carries an embedded ICC profile,
  ///        `read_region` returns pixels converted to sRGB (perceptual intent).
  [[nodiscard]] static std::unique_ptr<FastSlide> FromFilePath(
      const std::string& file_path, bool apply_icc = false);

  /// @brief Future factory method for URIs
  [[nodiscard]] static std::unique_ptr<FastSlide> FromUri(
      const std::string& uri);

  /// @brief Close the slide reader and release resources
  void Close();

  /// @brief Check if the reader is closed
  [[nodiscard]] bool IsClosed() const;

  /// @brief Context manager support
  FastSlide& __enter__();
  bool __exit__(nb::object exc_type, nb::object exc_value,
                nb::object traceback);

  /// @brief Read a region from the slide using level-native coordinates
  /// @param z Focal-plane index (0 = first plane).
  /// @param t Time-point index (0 = first time point).
  [[nodiscard]] std::shared_ptr<fastslide::Image> ReadRegion(
      uint32_t x, uint32_t y, uint32_t width, uint32_t height, int level = 0,
      uint32_t z = 0, uint32_t t = 0);

  /// @brief Z (focal) / T (time) stack extent of the primary image.
  [[nodiscard]] StackInfo GetStackInfo() const;

  /// @brief Get associated images accessor
  [[nodiscard]] AssociatedImages& GetAssociatedImages();

  /// @brief Get associated data accessor
  [[nodiscard]] AssociatedData& GetAssociatedData();

  /// @brief Navigable pyramids inside the file (`slide.images`).
  [[nodiscard]] SlideImages& GetImages();

  // Properties (pythonic getters)
  [[nodiscard]] nb::tuple GetDimensions() const;
  [[nodiscard]] nb::tuple GetLevelDimensions() const;
  [[nodiscard]] nb::tuple GetLevelDownsamples() const;
  [[nodiscard]] int GetLevelCount() const;
  [[nodiscard]] nb::dict GetProperties() const;
  [[nodiscard]] nb::tuple GetMpp() const;
  [[nodiscard]] nb::tuple GetBounds() const;
  [[nodiscard]] std::string GetFormat() const;
  [[nodiscard]] std::string GetDtype() const;

  /// @brief Embedded ICC profile as `bytes`, or `None` if the slide has none.
  [[nodiscard]] nb::object GetIccProfile() const;

  /// @brief Whether ICC color management is currently applied on read.
  [[nodiscard]] bool GetApplyIcc() const;
  [[nodiscard]] std::string GetQuickHash() const;
  [[nodiscard]] nb::list GetChannelMetadata() const;
  [[nodiscard]] int GetNumChannels() const;
  [[nodiscard]] int GetBestLevelForDownsample(double downsample) const;

  // Cache management
  void SetCache(std::shared_ptr<fastslide::runtime::ITileCache> cache);
  [[nodiscard]] std::shared_ptr<fastslide::runtime::ITileCache> GetCache()
      const;
  [[nodiscard]] bool IsCacheEnabled() const;

  // Utility methods
  [[nodiscard]] std::string GetSourcePath() const;
  [[nodiscard]] nb::tuple ConvertLevel0ToLevelNative(int64_t x, int64_t y,
                                                     int level) const;
  [[nodiscard]] nb::tuple ConvertLevelNativeToLevel0(uint32_t x, uint32_t y,
                                                     int level) const;
};

}  // namespace fastslide::python
