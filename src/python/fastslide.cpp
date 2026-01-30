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

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/python/cache.h"
#include "fastslide/python/reader.h"
#include "fastslide/runtime/global_cache_manager.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/slide_reader.h"

namespace py = pybind11;

namespace pybind11::detail {
/// @brief Type caster for NDArrayView to numpy array (view)
/// @tparam T Data type
/// @tparam N Rank
template <typename T, size_t N>
struct type_caster<aifocore::math::NDArrayView<T, N>> {
 public:
  using Type = aifocore::math::NDArrayView<T, N>;
  PYBIND11_TYPE_CASTER(Type, const_name("NDArrayView[") + type_caster<T>::name +
                                 const_name("]"));

  /// @brief Convert Python object to C++ (not implemented for view)
  bool load(handle, bool) { return false; }

  /// @brief Convert C++ object to Python (view to numpy array)
  static handle cast(const aifocore::math::NDArrayView<T, N>& src,
                     return_value_policy /* policy */, handle parent) {
    std::vector<ssize_t> shape(N);
    std::vector<ssize_t> strides(N);
    const auto& src_shape = src.Shape();
    const auto& src_strides = src.Strides();

    for (size_t i = 0; i < N; ++i) {
      shape[i] = static_cast<ssize_t>(src_shape[i]);
      strides[i] = static_cast<ssize_t>(src_strides[i] * sizeof(T));
    }

    // Treat parent as base if it exists (e.g., Image object)
    object base;
    if (parent) {
      base = reinterpret_borrow<object>(parent);
    }

    return py::array_t<T>(shape, strides, src.Data(), base).release();
  }
};
}  // namespace pybind11::detail

using fastslide::RegionSpec;
using fastslide::SlideReader;
using fastslide::python::AssociatedData;
using fastslide::python::AssociatedImages;
using fastslide::python::FastSlide;

namespace {

/// @brief Convert aifocore::Status to Python exception
void ThrowPyErrorFromStatus(const aifocore::Status& status) {
  if (status.code() == aifocore::StatusCode::kInvalidArgument) {
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
  using fastslide::runtime::ITileCache;

  // Enums
  py::enum_<fastslide::ImageFormat>(m, "ImageFormat")
      .value("GRAY", fastslide::ImageFormat::kGray)
      .value("RGB", fastslide::ImageFormat::kRGB)
      .value("RGBA", fastslide::ImageFormat::kRGBA)
      .value("SPECTRAL", fastslide::ImageFormat::kSpectral)
      .export_values();

  py::enum_<fastslide::PlanarConfig>(m, "PlanarConfig")
      .value("CONTIGUOUS", fastslide::PlanarConfig::kContiguous)
      .value("SEPARATE", fastslide::PlanarConfig::kSeparate)
      .export_values();

  // Image class binding
  py::class_<fastslide::Image, std::shared_ptr<fastslide::Image>>(m, "Image")
      .def_property_readonly("width", &fastslide::Image::GetWidth)
      .def_property_readonly("height", &fastslide::Image::GetHeight)
      .def_property_readonly("channels", &fastslide::Image::GetChannels)
      .def_property_readonly("format", &fastslide::Image::GetFormat)
      .def_property_readonly(
          "dtype",
          [](const fastslide::Image& self) {
            return fastslide::GetDataTypeName(self.GetDataType());
          })
      .def_property_readonly("planar_config",
                             &fastslide::Image::GetPlanarConfig)
      .def(
          "numpy",
          [](py::object self_py) -> py::object {
            const auto& self = self_py.cast<const fastslide::Image&>();
            py::object result;
            // Dispatch to the correct type for ArrayView
            fastslide::DispatchByDataType(
                self.GetDataType(), [&]<typename T>() {
                  // Get NDArrayView<T, 3>
                  auto view = self.ArrayView<T>();
                  // Cast to numpy array using the type caster we defined
                  // Pass self_py as parent to keep Image alive
                  result = py::cast(view,
                                    py::return_value_policy::reference_internal,
                                    self_py);
                });
            return result;
          },
          "Get a numpy array view of the image data (zero-copy)");

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

  py::class_<fastslide::runtime::ITileCache,
             std::shared_ptr<fastslide::runtime::ITileCache>>(m, "TileCache")
      .def("get_stats", &fastslide::runtime::ITileCache::GetStats,
           "Get cache statistics")
      .def("get_capacity", &fastslide::runtime::ITileCache::GetCapacity,
           "Get cache capacity")
      .def("get_size", &fastslide::runtime::ITileCache::GetSize,
           "Get cache size")
      .def("clear", &fastslide::runtime::ITileCache::Clear, "Clear cache");

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
  m.attr("CacheStats") = m.attr("RuntimeCacheStats");

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
          "    fastslide.Image: Image object. Call .numpy() to get array "
          "view.\n\n"
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
                             "Returns tuple with coordinates and size:\n"
                             "  (x, y) tuple: Top-left coordinates (level 0)\n"
                             "  (width, height) tuple: Bounding box size")
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
      .def("set_cache", &FastSlide::SetCache, "Set cache (accepts TileCache)",
           py::arg("cache"))
      // Overload for CacheManager for backward compatibility or convenience
      .def(
          "set_cache",
          [](FastSlide& self, std::shared_ptr<CacheManager> cm) {
            self.SetCache(cm->GetCache());
          },
          "Set cache using CacheManager", py::arg("cache_manager"))
      .def(
          "set_cache_manager",
          [](FastSlide& self, std::shared_ptr<CacheManager> cm) {
            self.SetCache(cm->GetCache());
          },
          "Set custom cache manager (deprecated, use set_cache)",
          py::arg("cache_manager"))
      .def("get_cache", &FastSlide::GetCache, "Get current cache")
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
  m.attr("__version__") = "0.2.0";
}
