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

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "fastslide/python/cache.h"
#include "fastslide/slide_reader.h"

namespace py = pybind11;

namespace fastslide::python {

using fastslide::SlideReader;

/// @brief Lazy-loading dictionary-like wrapper for associated images
class AssociatedImages {
 private:
  std::weak_ptr<SlideReader> reader_;
  mutable std::unordered_map<std::string, std::optional<py::array_t<uint8_t>>>
      cache_;
  mutable std::vector<std::string> available_names_;
  mutable bool names_loaded_ = false;

  [[nodiscard]] std::shared_ptr<SlideReader> GetReader() const;
  void EnsureNamesLoaded() const;

 public:
  explicit AssociatedImages(std::shared_ptr<SlideReader> reader);

  /// @brief Get associated image by name (lazy loading)
  [[nodiscard]] py::array_t<uint8_t> GetItem(const std::string& name) const;

  /// @brief Check if associated image exists
  [[nodiscard]] bool Contains(const std::string& name) const;

  /// @brief Get list of associated image names
  [[nodiscard]] std::vector<std::string> Keys() const;

  /// @brief Get dimensions of associated image without loading it
  [[nodiscard]] py::tuple GetDimensions(const std::string& name) const;

  /// @brief Get number of cached images
  [[nodiscard]] size_t GetCacheSize() const;

  /// @brief Clear the image cache
  void ClearCache() const;
};

/// @brief Lazy-loading dictionary-like wrapper for associated data (XML, binary)
class AssociatedData {
 private:
  std::weak_ptr<SlideReader> reader_;
  mutable std::unordered_map<std::string, std::optional<py::object>> cache_;
  mutable std::vector<std::string> available_names_;
  mutable bool names_loaded_ = false;

  [[nodiscard]] std::shared_ptr<SlideReader> GetReader() const;
  void EnsureNamesLoaded() const;

 public:
  explicit AssociatedData(std::shared_ptr<SlideReader> reader);

  /// @brief Get associated data by name (lazy loading)
  [[nodiscard]] py::object GetItem(const std::string& name) const;

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

/// @brief Main slide reader class with pythonic interface
class FastSlide {
 private:
  std::shared_ptr<SlideReader> reader_;
  std::unique_ptr<AssociatedImages> associated_images_;
  std::unique_ptr<AssociatedData> associated_data_;
  std::shared_ptr<CacheManager> cache_manager_;
  bool is_closed_;
  std::string source_path_;

 public:
  /// @brief Private constructor - use factory methods instead
  explicit FastSlide(std::shared_ptr<SlideReader> reader,
                     const std::string& source_path);

  /// @brief Factory method to create from file path
  [[nodiscard]] static std::unique_ptr<FastSlide> FromFilePath(
      const std::string& file_path);

  /// @brief Future factory method for URIs
  [[nodiscard]] static std::unique_ptr<FastSlide> FromUri(
      const std::string& uri);

  /// @brief Close the slide reader and release resources
  void Close();

  /// @brief Check if the reader is closed
  [[nodiscard]] bool IsClosed() const;

  /// @brief Context manager support
  FastSlide& __enter__();
  bool __exit__(py::object exc_type, py::object exc_value,
                py::object traceback);

  /// @brief Read a region from the slide using level-native coordinates
  [[nodiscard]] py::array_t<uint8_t> ReadRegion(uint32_t x, uint32_t y,
                                                uint32_t width, uint32_t height,
                                                int level = 0);

  /// @brief Get associated images accessor
  [[nodiscard]] AssociatedImages& GetAssociatedImages();

  /// @brief Get associated data accessor
  [[nodiscard]] AssociatedData& GetAssociatedData();

  // Properties (pythonic getters)
  [[nodiscard]] py::tuple GetDimensions() const;
  [[nodiscard]] py::tuple GetLevelDimensions() const;
  [[nodiscard]] py::tuple GetLevelDownsamples() const;
  [[nodiscard]] int GetLevelCount() const;
  [[nodiscard]] py::dict GetProperties() const;
  [[nodiscard]] py::tuple GetMpp() const;
  [[nodiscard]] py::dict GetBounds() const;
  [[nodiscard]] std::string GetFormat() const;
  [[nodiscard]] std::string GetQuickHash() const;
  [[nodiscard]] py::list GetChannelMetadata() const;
  [[nodiscard]] int GetBestLevelForDownsample(double downsample) const;

  // Cache management
  void SetCacheManager(std::shared_ptr<CacheManager> cache_manager);
  [[nodiscard]] std::shared_ptr<CacheManager> GetCacheManager() const;
  void UseGlobalCache();
  [[nodiscard]] bool IsCacheEnabled() const;

  // Utility methods
  [[nodiscard]] std::string GetSourcePath() const;
  [[nodiscard]] py::tuple ConvertLevel0ToLevelNative(int64_t x, int64_t y,
                                                     int level) const;
  [[nodiscard]] py::tuple ConvertLevelNativeToLevel0(uint32_t x, uint32_t y,
                                                     int level) const;

  // Channel visibility controls
  void SetVisibleChannels(const std::vector<size_t>& channel_indices);
  [[nodiscard]] std::vector<size_t> GetVisibleChannels() const;
  void ShowAllChannels();
};

}  // namespace fastslide::python
