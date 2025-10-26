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

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fastslide/python/cache.h"
#include "fastslide/python/reader.h"
#include "fastslide/runtime/global_cache_manager.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/cache.h"

namespace py = pybind11;
using fastslide::GlobalTileCache;
using fastslide::RegionSpec;
using fastslide::SlideReader;
using fastslide::TileCache;
using fastslide::python::AssociatedData;
using fastslide::python::AssociatedImages;
using fastslide::python::FastSlide;

namespace {

/// @brief Convert absl::Status to Python exception
void ThrowPyErrorFromStatus(const absl::Status& status) {
  if (status.code() == absl::StatusCode::kInvalidArgument) {
    throw py::value_error(status.ToString());
  }
  // Raise generic runtime error for all other cases
  throw std::runtime_error(status.ToString());
}

}  // namespace

PYBIND11_MODULE(_fastslide, m) {
  m.doc() =
      "FastSlide: High-performance, thread-safe digital pathology slide reader";

  using fastslide::python::CacheInspectionStats;
  using fastslide::python::CacheManager;
  using fastslide::python::TileCache;
  using RuntimeGlobalCacheManager = fastslide::runtime::GlobalCacheManager;

  // Cache-related classes
  py::class_<CacheInspectionStats>(m, "CacheInspectionStats")
      .def_readwrite("capacity", &CacheInspectionStats::capacity)
      .def_readwrite("size", &CacheInspectionStats::size)
      .def_readwrite("hits", &CacheInspectionStats::hits)
      .def_readwrite("misses", &CacheInspectionStats::misses)
      .def_readwrite("hit_ratio", &CacheInspectionStats::hit_ratio)
      .def_readwrite("memory_usage_mb", &CacheInspectionStats::memory_usage_mb)
      .def_readwrite("recent_keys", &CacheInspectionStats::recent_keys)
      .def_readwrite("key_frequencies", &CacheInspectionStats::key_frequencies);

  py::class_<TileCache, std::shared_ptr<TileCache>>(m, "TileCache")
      .def("get_stats", &TileCache::GetStats, "Get cache statistics")
      .def("get_capacity", &TileCache::GetCapacity, "Get cache capacity")
      .def("get_size", &TileCache::GetSize, "Get cache size")
      .def("clear", &TileCache::Clear, "Clear cache");

  py::class_<CacheManager, std::shared_ptr<CacheManager>>(m, "CacheManager")
      .def_static(
          "create",
          [](size_t capacity) -> std::shared_ptr<CacheManager> {
            auto result = CacheManager::Create(capacity);
            if (!result.ok()) {
              ThrowPyErrorFromStatus(result.status());
            }
            return std::move(result).value();
          },
          "Create cache manager", py::arg("capacity") = 1000)
      .def("clear", &CacheManager::Clear, "Clear all cached tiles")
      .def("get_basic_stats", &CacheManager::GetBasicStats,
           "Get basic cache statistics")
      .def("get_detailed_stats", &CacheManager::GetDetailedStats,
           "Get detailed cache statistics")
      .def(
          "resize",
          [](CacheManager& self, size_t new_capacity) {
            auto status = self.Resize(new_capacity);
            if (!status.ok()) {
              ThrowPyErrorFromStatus(status);
            }
          },
          "Resize cache capacity", py::arg("new_capacity"));

  // Runtime global cache manager (for dependency injection)
  // Use std::unique_ptr with custom deleter since it's a singleton with private
  // destructor
  py::class_<RuntimeGlobalCacheManager,
             std::unique_ptr<RuntimeGlobalCacheManager, py::nodelete>>(
      m, "RuntimeGlobalCacheManager")
      .def_static("instance", &RuntimeGlobalCacheManager::Instance,
                  "Get runtime global cache manager instance",
                  py::return_value_policy::reference)
      .def(
          "set_capacity",
          [](RuntimeGlobalCacheManager& self, size_t capacity) {
            auto status = self.SetCapacity(capacity);
            if (!status.ok()) {
              ThrowPyErrorFromStatus(status);
            }
          },
          "Set global cache capacity", py::arg("capacity"))
      .def("get_capacity", &RuntimeGlobalCacheManager::GetCapacity,
           "Get global cache capacity")
      .def("get_size", &RuntimeGlobalCacheManager::GetSize,
           "Get current cache size")
      .def("clear", &RuntimeGlobalCacheManager::Clear, "Clear global cache");

  // Expose ITileCache::Stats
  py::class_<fastslide::runtime::ITileCache::Stats>(m, "RuntimeCacheStats")
      .def_readwrite("capacity",
                     &fastslide::runtime::ITileCache::Stats::capacity)
      .def_readwrite("size", &fastslide::runtime::ITileCache::Stats::size)
      .def_readwrite("hits", &fastslide::runtime::ITileCache::Stats::hits)
      .def_readwrite("misses", &fastslide::runtime::ITileCache::Stats::misses)
      .def_readwrite("hit_ratio",
                     &fastslide::runtime::ITileCache::Stats::hit_ratio)
      .def_readwrite(
          "memory_usage_bytes",
          &fastslide::runtime::ITileCache::Stats::memory_usage_bytes);

  // Basic cache statistics (for compatibility)
  py::class_<TileCache::Stats>(m, "CacheStats")
      .def_readwrite("capacity", &TileCache::Stats::capacity)
      .def_readwrite("size", &TileCache::Stats::size)
      .def_readwrite("hits", &TileCache::Stats::hits)
      .def_readwrite("misses", &TileCache::Stats::misses)
      .def_readwrite("hit_ratio", &TileCache::Stats::hit_ratio)
      .def_readwrite("memory_usage_bytes",
                     &TileCache::Stats::memory_usage_bytes);

  // Associated images accessor
  py::class_<AssociatedImages>(m, "AssociatedImages")
      .def("__getitem__", &AssociatedImages::GetItem,
           "Get associated image by name (lazy loaded)", py::arg("name"))
      .def("__contains__", &AssociatedImages::Contains,
           "Check if associated image exists", py::arg("name"))
      .def("keys", &AssociatedImages::Keys,
           "Get list of associated image names")
      .def("get_dimensions", &AssociatedImages::GetDimensions,
           "Get dimensions of associated image", py::arg("name"))
      .def("get_cache_size", &AssociatedImages::GetCacheSize,
           "Get number of cached images in memory")
      .def("clear_cache", &AssociatedImages::ClearCache,
           "Clear the associated image cache");

  // Associated data accessor (MRXS: XML, binary)
  py::class_<AssociatedData>(m, "AssociatedData")
      .def("__getitem__", &AssociatedData::GetItem,
           "Get associated data by name (lazy loaded)\n\n"
           "Returns:\n"
           "    str for XML data\n"
           "    bytes for binary data",
           py::arg("name"))
      .def("__contains__", &AssociatedData::Contains,
           "Check if associated data exists", py::arg("name"))
      .def("keys", &AssociatedData::Keys,
           "Get list of associated data names (non-image)")
      .def("get_type", &AssociatedData::GetType,
           "Get data type without loading", py::arg("name"))
      .def("get_cache_size", &AssociatedData::GetCacheSize,
           "Get number of cached data items in memory")
      .def("clear_cache", &AssociatedData::ClearCache,
           "Clear the associated data cache");

  // Main slide reader class with factory methods
  py::class_<FastSlide>(m, "FastSlide")
      .def_static(
          "from_file_path",
          [](const py::object& file_path) {
            std::string path_str;
            if (py::isinstance<py::str>(file_path)) {
              path_str = py::cast<std::string>(file_path);
            } else if (py::hasattr(file_path, "__fspath__")) {
              // Handle pathlib.Path objects
              auto path_obj = file_path.attr("__fspath__")();
              path_str = py::cast<std::string>(path_obj);
            } else {
              path_str = py::cast<std::string>(file_path);
            }
            return FastSlide::FromFilePath(path_str);
          },
          "Create FastSlide from file path (accepts str or pathlib.Path)",
          py::arg("file_path"))
      .def_static("from_uri", &FastSlide::FromUri,
                  "Create FastSlide from URI (future)", py::arg("uri"))

      // Main reading method (level-native coordinates)
      .def(
          "read_region",
          [](FastSlide& self, const std::tuple<uint32_t, uint32_t>& location,
             int level, const std::tuple<uint32_t, uint32_t>& size) {
            auto [x, y] = location;
            auto [width, height] = size;
            return self.ReadRegion(x, y, width, height, level);
          },
          "Read a region from the slide using level-native coordinates\n\n"
          "Args:\n"
          "    location: Tuple (x, y) coordinates in level-native space\n"
          "    level: Pyramid level (0=highest resolution)\n"
          "    size: Tuple (width, height) of region\n\n"
          "Returns:\n"
          "    numpy.ndarray: RGB image (height, width, 3), dtype=uint8\n\n"
          "Note: Coordinates are in level-native space. To convert from "
          "level-0\n"
          "coordinates, use convert_level0_to_level_native().",
          py::arg("location"), py::arg("level"), py::arg("size"))

      // Coordinate conversion utilities
      .def("convert_level0_to_level_native",
           &FastSlide::ConvertLevel0ToLevelNative,
           "Convert level-0 coordinates to level-native coordinates\n\n"
           "Args:\n"
           "    x: X coordinate in level-0 space\n"
           "    y: Y coordinate in level-0 space\n"
           "    level: Target level\n\n"
           "Returns:\n"
           "    tuple: (level_native_x, level_native_y)",
           py::arg("x"), py::arg("y"), py::arg("level"))

      .def("convert_level_native_to_level0",
           &FastSlide::ConvertLevelNativeToLevel0,
           "Convert level-native coordinates to level-0 coordinates\n\n"
           "Args:\n"
           "    x: X coordinate in level-native space\n"
           "    y: Y coordinate in level-native space\n"
           "    level: Source level\n\n"
           "Returns:\n"
           "    tuple: (level_0_x, level_0_y)",
           py::arg("x"), py::arg("y"), py::arg("level"))

      // Properties
      .def_property_readonly("dimensions", &FastSlide::GetDimensions,
                             "Slide dimensions (width, height) at level 0")
      .def_property_readonly("level_dimensions", &FastSlide::GetLevelDimensions,
                             "Tuple of (width, height) for each level")
      .def_property_readonly("level_downsamples",
                             &FastSlide::GetLevelDownsamples,
                             "Tuple of downsample factors for each level")
      .def_property_readonly("level_count", &FastSlide::GetLevelCount,
                             "Number of levels in the slide")
      .def_property_readonly("properties", &FastSlide::GetProperties,
                             "Dictionary of slide properties and metadata")
      .def_property_readonly("mpp", &FastSlide::GetMpp,
                             "Microns per pixel as (mpp_x, mpp_y) tuple")
      .def_property_readonly("bounds", &FastSlide::GetBounds,
                             "Bounding box of non-empty region\n\n"
                             "Returns dict with keys:\n"
                             "  'x', 'y': Top-left coordinates (level 0)\n"
                             "  'width', 'height': Bounding box size\n"
                             "  'coordinates': (x, y) tuple\n"
                             "  'size': (width, height) tuple")
      .def_property_readonly("format", &FastSlide::GetFormat,
                             "File format name")
      .def_property_readonly(
          "quickhash", &FastSlide::GetQuickHash,
          "SHA-256 quickhash (unique identifier, OpenSlide-compatible)")
      .def_property_readonly("channel_metadata", &FastSlide::GetChannelMetadata,
                             "List of channel metadata dictionaries")
      .def_property_readonly(
          "associated_images", &FastSlide::GetAssociatedImages,
          "Dictionary-like access to associated images (lazy loaded)",
          py::return_value_policy::reference_internal)
      .def_property_readonly("associated_data", &FastSlide::GetAssociatedData,
                             "Dictionary-like access to associated data: XML, "
                             "binary (lazy loaded, MRXS only)",
                             py::return_value_policy::reference_internal)
      .def_property_readonly("closed", &FastSlide::IsClosed,
                             "True if the slide reader is closed")
      .def_property_readonly("source_path", &FastSlide::GetSourcePath,
                             "Source file path")

      // Utility methods
      .def("get_best_level_for_downsample",
           &FastSlide::GetBestLevelForDownsample,
           "Get the best level for a given downsample factor",
           py::arg("downsample"))

      // Cache management
      .def("set_cache_manager", &FastSlide::SetCacheManager,
           "Set custom cache manager", py::arg("cache_manager"))
      .def("get_cache_manager", &FastSlide::GetCacheManager,
           "Get current cache manager")
      .def("use_global_cache", &FastSlide::UseGlobalCache,
           "Use the global cache for this slide")
      .def_property_readonly("cache_enabled", &FastSlide::IsCacheEnabled,
                             "True if caching is enabled")

      // Resource management
      .def("close", &FastSlide::Close,
           "Close the slide reader and release resources")

      // Channel visibility controls
      .def(
          "set_visible_channels",
          [](FastSlide& self, const py::object& channels) {
            std::vector<size_t> channel_indices;

            // Handle None - show all channels
            if (channels.is_none()) {
              self.ShowAllChannels();
              return;
            }

            // Handle single integer
            if (py::isinstance<py::int_>(channels)) {
              int channel = py::cast<int>(channels);
              if (channel < 0) {
                throw py::value_error("Channel index cannot be negative");
              }
              channel_indices.push_back(static_cast<size_t>(channel));
            } else if (py::isinstance<py::sequence>(channels) &&
                       !py::isinstance<py::str>(channels)) {
              // Handle sequence (list, tuple, etc.)
              py::sequence seq = py::cast<py::sequence>(channels);
              channel_indices.reserve(seq.size());

              for (size_t i = 0; i < seq.size(); ++i) {
                if (!py::isinstance<py::int_>(seq[i])) {
                  throw py::type_error("All channel indices must be integers");
                }
                int channel = py::cast<int>(seq[i]);
                if (channel < 0) {
                  throw py::value_error("Channel indices cannot be negative");
                }
                channel_indices.push_back(static_cast<size_t>(channel));
              }

              // Remove duplicates and sort
              std::sort(channel_indices.begin(), channel_indices.end());
              channel_indices.erase(
                  std::unique(channel_indices.begin(), channel_indices.end()),
                  channel_indices.end());
            } else {
              throw py::type_error(
                  "channels must be an integer, list/tuple of integers, or "
                  "None");
            }

            self.SetVisibleChannels(channel_indices);
          },
          "Set which channels are visible during read operations\n\n"
          "Args:\n"
          "    channels: Channel index (int), list/tuple of channel indices,\n"
          "              or None to show all channels\n\n"
          "Examples:\n"
          "    slide.set_visible_channels(0)        # Show only channel 0\n"
          "    slide.set_visible_channels([0, 2])   # Show channels 0 and 2\n"
          "    slide.set_visible_channels(None)     # Show all channels\n\n"
          "Note: Channel indices are 0-based. Invalid indices will be ignored\n"
          "during read operations.",
          py::arg("channels"))

      .def("get_visible_channels", &FastSlide::GetVisibleChannels,
           "Get currently visible channel indices\n\n"
           "Returns:\n"
           "    list: List of visible channel indices (empty = all visible)",
           py::return_value_policy::copy)

      .def("show_all_channels", &FastSlide::ShowAllChannels,
           "Reset to show all channels\n\n"
           "Equivalent to set_visible_channels(None)")

      // Context manager support
      .def("__enter__", &FastSlide::__enter__,
           py::return_value_policy::reference_internal)
      .def("__exit__", &FastSlide::__exit__);

  // Utility functions
  m.def(
      "get_supported_extensions",
      []() {
        return fastslide::runtime::GetGlobalRegistry().GetSupportedExtensions();
      },
      "Get list of supported file extensions");

  m.def(
      "is_supported",
      [](const std::string& filename) {
        auto reader_or =
            fastslide::runtime::GetGlobalRegistry().CreateReader(filename);
        return reader_or.ok();
      },
      "Check if file format is supported", py::arg("filename"));

  // Version and constants
  m.attr("__version__") = "1.0.0";
  m.attr("RGB_CHANNELS") = 3;
}
