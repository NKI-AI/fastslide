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

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <array>
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

namespace nb = nanobind;

using fastslide::RegionSpec;
using fastslide::SlideReader;
using fastslide::python::AssociatedData;
using fastslide::python::AssociatedImages;
using fastslide::python::FastSlide;
using fastslide::python::SlideImages;
using fastslide::python::SlideImageView;

namespace {

/// @brief Convert aifocore::Status to Python exception
void ThrowPyErrorFromStatus(const aifocore::Status& status) {
  if (status.code() == aifocore::StatusCode::kInvalidArgument) {
    throw nb::value_error(status.ToString().c_str());
  }
  // Raise generic runtime error for all other cases
  throw std::runtime_error(status.ToString());
}

/// @brief Resolve a Python cache argument into an ITileCache.
///
/// Accepts `None` (no cache), an `int` byte capacity (a new per-slide
/// `LRUTileCache`), a `CacheManager`, or a `TileCache`. Raises on an invalid
/// capacity or unsupported type.
std::shared_ptr<fastslide::runtime::ITileCache> ResolveCacheObject(
    const nb::object& cache) {
  if (cache.is_none()) {
    return nullptr;
  }
  if (nb::isinstance<nb::int_>(cache)) {
    const auto capacity_bytes = nb::cast<size_t>(cache);
    auto cache_or = fastslide::runtime::LRUTileCache::Create(capacity_bytes);
    if (!cache_or.ok()) {
      ThrowPyErrorFromStatus(cache_or.status());
    }
    return std::move(cache_or.value());
  }
  if (nb::isinstance<fastslide::python::CacheManager>(cache)) {
    auto manager =
        nb::cast<std::shared_ptr<fastslide::python::CacheManager>>(cache);
    return manager ? manager->GetCache() : nullptr;
  }
  return nb::cast<std::shared_ptr<fastslide::runtime::ITileCache>>(cache);
}

/// @brief Build a zero-copy numpy view of an Image's pixel buffer.
///
/// The returned `nb::ndarray` keeps `image_handle` alive via nanobind's
/// owner mechanism, so the underlying `fastslide::Image` storage is not
/// freed while the numpy array is in use. No data is copied.
nb::object MakeNumpyView(const fastslide::Image& image,
                         nb::handle image_handle) {
  nb::object result;
  fastslide::DispatchByDataType(image.GetDataType(), [&]<typename T>() {
    // Const Image::ArrayView<T>() returns NDArrayView<const T, 3>. We need a
    // mutable T* to construct nb::ndarray<numpy, T, ...>. Casting away const
    // matches the previous pybind11 behavior, where the returned numpy view
    // was writable.
    auto view = image.ArrayView<T>();
    const auto& src_shape = view.Shape();
    const auto& src_strides = view.Strides();

    using ArrayT =
        nb::ndarray<nb::numpy, T, nb::ndim<3>, nb::c_contig, nb::device::cpu>;
    ArrayT arr(
        /*data=*/const_cast<T*>(view.Data()),
        /*shape=*/{src_shape[0], src_shape[1], src_shape[2]},
        /*owner=*/image_handle,
        /*strides=*/
        {static_cast<int64_t>(src_strides[0]),
         static_cast<int64_t>(src_strides[1]),
         static_cast<int64_t>(src_strides[2])});
    // rv_policy::reference avoids any data copy; ownership of the returned
    // numpy object is transferred to the caller while the underlying buffer
    // remains owned by the Image (kept alive via the owner handle above).
    result = nb::cast(std::move(arr), nb::rv_policy::reference);
  });
  return result;
}

}  // namespace

NB_MODULE(_fastslide, m) {
  m.doc() =
      "FastSlide: High-performance, thread-safe digital pathology slide reader";

  using fastslide::python::CacheInspectionStats;
  using fastslide::python::CacheManager;
  using fastslide::runtime::ITileCache;

  // Enums
  nb::enum_<fastslide::ImageFormat>(m, "ImageFormat")
      .value("GRAY", fastslide::ImageFormat::kGray)
      .value("RGB", fastslide::ImageFormat::kRGB)
      .value("RGBA", fastslide::ImageFormat::kRGBA)
      .value("SPECTRAL", fastslide::ImageFormat::kSpectral)
      .export_values();

  nb::enum_<fastslide::PlanarConfig>(m, "PlanarConfig")
      .value("CONTIGUOUS", fastslide::PlanarConfig::kContiguous)
      .value("SEPARATE", fastslide::PlanarConfig::kSeparate)
      .export_values();

  // Image class binding
  nb::class_<fastslide::Image>(m, "Image")
      .def_prop_ro("width", &fastslide::Image::GetWidth)
      .def_prop_ro("height", &fastslide::Image::GetHeight)
      .def_prop_ro("channels", &fastslide::Image::GetChannels)
      .def_prop_ro("format", &fastslide::Image::GetFormat)
      .def_prop_ro("dtype",
                   [](const fastslide::Image& self) {
                     return fastslide::GetDataTypeName(self.GetDataType());
                   })
      .def_prop_ro("planar_config", &fastslide::Image::GetPlanarConfig)
      .def_prop_ro("is_interleaved", &fastslide::Image::IsInterleaved,
                   "True if the image is stored interleaved (HWC).")
      .def_prop_ro("is_separate", &fastslide::Image::IsSeparate,
                   "True if the image is stored band-separate / planar (CHW).")
      .def(
          "numpy",
          [](nb::handle self_py) -> nb::object {
            const auto& self = nb::cast<const fastslide::Image&>(self_py);
            return MakeNumpyView(self, self_py);
          },
          "Get a numpy array view of the image data (zero-copy)")
      .def(
          "to_interleaved",
          [](nb::handle self_py) -> nb::object {
            // No-op fast path: when already interleaved, return the *same*
            // Python object. No allocation, no data movement, no clone.
            const auto& self = nb::cast<const fastslide::Image&>(self_py);
            if (self.IsInterleaved()) {
              return nb::borrow(self_py);
            }
            return nb::cast(self.ToInterleaved());
          },
          "Return an interleaved (HWC) view of this image.\n\n"
          "If the image is already interleaved, returns ``self`` unchanged "
          "(no copy). Otherwise allocates a new Image with the same pixels "
          "in interleaved layout. Peak extra memory during conversion equals "
          "the image size.")
      .def(
          "to_separate",
          [](nb::handle self_py) -> nb::object {
            // Symmetric no-op fast path for the planar/separate direction.
            const auto& self = nb::cast<const fastslide::Image&>(self_py);
            if (self.IsSeparate()) {
              return nb::borrow(self_py);
            }
            return nb::cast(self.ToPlanar());
          },
          "Return a band-separate (CHW) view of this image.\n\n"
          "If the image is already band-separate, returns ``self`` unchanged "
          "(no copy). Otherwise allocates a new Image with the same pixels "
          "in planar layout.");

  // Cache-related classes
  nb::class_<CacheInspectionStats>(m, "CacheInspectionStats")
      .def_rw("capacity_bytes", &CacheInspectionStats::capacity_bytes)
      .def_rw("size", &CacheInspectionStats::size)
      .def_rw("hits", &CacheInspectionStats::hits)
      .def_rw("misses", &CacheInspectionStats::misses)
      .def_rw("hit_ratio", &CacheInspectionStats::hit_ratio)
      .def_rw("memory_usage_mb", &CacheInspectionStats::memory_usage_mb)
      .def_rw("recent_keys", &CacheInspectionStats::recent_keys)
      .def_rw("key_frequencies", &CacheInspectionStats::key_frequencies);

  nb::class_<fastslide::runtime::ITileCache>(m, "TileCache")
      .def("get_stats", &fastslide::runtime::ITileCache::GetStats,
           "Get cache statistics")
      .def("get_capacity_bytes",
           &fastslide::runtime::ITileCache::GetCapacityBytes,
           "Get cache capacity in bytes")
      .def("get_size", &fastslide::runtime::ITileCache::GetSize,
           "Get cache size (number of tiles)")
      .def("clear", &fastslide::runtime::ITileCache::Clear, "Clear cache");

  nb::class_<CacheManager>(m, "CacheManager")
      .def_static(
          "create",
          [](size_t capacity_bytes) -> std::shared_ptr<CacheManager> {
            auto result = CacheManager::Create(capacity_bytes);
            if (!result.ok()) {
              ThrowPyErrorFromStatus(result.status());
            }
            return std::move(result).value();
          },
          "Create cache manager with the given byte capacity",
          nb::arg("capacity_bytes") =
              fastslide::python::kDefaultCacheManagerCapacityBytes)
      .def("clear", &CacheManager::Clear, "Clear all cached tiles")
      .def("get_basic_stats", &CacheManager::GetBasicStats,
           "Get basic cache statistics")
      .def("get_detailed_stats", &CacheManager::GetDetailedStats,
           "Get detailed cache statistics")
      .def(
          "resize",
          [](CacheManager& self, size_t new_capacity_bytes) {
            auto status = self.Resize(new_capacity_bytes);
            if (!status.ok()) {
              ThrowPyErrorFromStatus(status);
            }
          },
          "Resize cache capacity (in bytes)", nb::arg("new_capacity_bytes"));

  // Expose ITileCache::Stats
  nb::class_<fastslide::runtime::ITileCache::Stats>(m, "RuntimeCacheStats")
      .def_rw("capacity_bytes",
              &fastslide::runtime::ITileCache::Stats::capacity_bytes)
      .def_rw("size", &fastslide::runtime::ITileCache::Stats::size)
      .def_rw("hits", &fastslide::runtime::ITileCache::Stats::hits)
      .def_rw("misses", &fastslide::runtime::ITileCache::Stats::misses)
      .def_rw("hit_ratio", &fastslide::runtime::ITileCache::Stats::hit_ratio)
      .def_rw("memory_usage_bytes",
              &fastslide::runtime::ITileCache::Stats::memory_usage_bytes);

  // Basic cache statistics (for compatibility)
  m.attr("CacheStats") = m.attr("RuntimeCacheStats");

  // Process-wide singleton cache manager. Mirrors the underlying C++ type
  // name ``fastslide::runtime::GlobalCacheManager``; distinct from the
  // per-instance ``CacheManager`` exposed above.
  nb::class_<fastslide::runtime::GlobalCacheManager>(m, "GlobalCacheManager")
      .def_static(
          "instance",
          []() -> fastslide::runtime::GlobalCacheManager* {
            return &fastslide::runtime::GlobalCacheManager::Instance();
          },
          nb::rv_policy::reference,
          "Return the process-wide singleton instance.")
      .def(
          "set_capacity_bytes",
          [](fastslide::runtime::GlobalCacheManager& self,
             size_t capacity_bytes) {
            auto status = self.SetCapacityBytes(capacity_bytes);
            if (!status.ok()) {
              ThrowPyErrorFromStatus(status);
            }
          },
          nb::arg("capacity_bytes"),
          "Replace the global cache with a new LRU cache of the given "
          "capacity (bytes). Drops all currently cached tiles.")
      .def("get_capacity_bytes",
           &fastslide::runtime::GlobalCacheManager::GetCapacityBytes,
           "Get the current global cache capacity in bytes.")
      .def("get_size", &fastslide::runtime::GlobalCacheManager::GetSize,
           "Get the current number of cached tiles.")
      .def("get_stats", &fastslide::runtime::GlobalCacheManager::GetStats,
           "Get the global cache statistics.")
      .def("clear", &fastslide::runtime::GlobalCacheManager::Clear,
           nb::call_guard<nb::gil_scoped_release>(),
           "Drop all entries from the global cache (capacity is preserved).");

  // Associated images accessor
  nb::class_<AssociatedImages>(m, "AssociatedImages")
      .def("__getitem__", &AssociatedImages::GetItem,
           "Get associated image by name (lazy loaded)", nb::arg("name"))
      .def("__contains__", &AssociatedImages::Contains,
           "Check if associated image exists", nb::arg("name"))
      .def("keys", &AssociatedImages::Keys,
           "Get list of associated image names")
      .def("get_dimensions", &AssociatedImages::GetDimensions,
           "Get dimensions of associated image", nb::arg("name"))
      .def("get_cache_size", &AssociatedImages::GetCacheSize,
           "Get number of cached images in memory")
      .def("clear_cache", &AssociatedImages::ClearCache,
           "Clear the associated image cache");

  // Associated data accessor (MRXS: XML, binary)
  nb::class_<AssociatedData>(m, "AssociatedData")
      .def("__getitem__", &AssociatedData::GetItem,
           "Get associated data by name (lazy loaded)\n\n"
           "Returns:\n"
           "    str for XML data\n"
           "    bytes for binary data",
           nb::arg("name"))
      .def("__contains__", &AssociatedData::Contains,
           "Check if associated data exists", nb::arg("name"))
      .def("keys", &AssociatedData::Keys,
           "Get list of associated data names (non-image)")
      .def("get_type", &AssociatedData::GetType,
           "Get data type without loading", nb::arg("name"))
      .def("get_cache_size", &AssociatedData::GetCacheSize,
           "Get number of cached data items in memory")
      .def("clear_cache", &AssociatedData::ClearCache,
           "Clear the associated data cache");

  // One navigable pyramid (image) inside a slide file. Returned by
  // ``slide.images[i]``. Stateless except for the (weak) reader handle, so
  // it can be safely passed across threads.
  nb::class_<SlideImageView>(m, "SlideImageView")
      .def_prop_ro("name", &SlideImageView::GetName,
                   "Image name (e.g. 'navigator', 'region 0').")
      .def_prop_ro("index", &SlideImageView::GetIndex,
                   "Image index inside the container.")
      .def_prop_ro("level_count", &SlideImageView::GetLevelCount,
                   "Number of pyramid levels in this image.")
      .def_prop_ro("dimensions", &SlideImageView::GetDimensions,
                   "(width, height) at level 0.")
      .def_prop_ro("level_dimensions", &SlideImageView::GetLevelDimensions,
                   "Tuple of (width, height) per level.")
      .def_prop_ro("level_downsamples", &SlideImageView::GetLevelDownsamples,
                   "Tuple of downsample factors per level.")
      .def_prop_ro("mpp", &SlideImageView::GetMpp,
                   "Microns-per-pixel as (mpp_x, mpp_y).")
      .def(
          "read_region",
          [](SlideImageView& self,
             const std::tuple<uint32_t, uint32_t>& location, int level,
             const std::tuple<uint32_t, uint32_t>& size, uint32_t z,
             uint32_t t) {
            auto [x, y] = location;
            auto [width, height] = size;
            return self.ReadRegion(x, y, width, height, level, z, t);
          },
          "Read a region from this image using level-native coordinates.\n\n"
          "Args:\n"
          "    location: Tuple (x, y) in level-native space.\n"
          "    level: Pyramid level (0=highest resolution).\n"
          "    size: Tuple (width, height) of region in pixels.\n"
          "    z: Focal-plane index (0=first plane).\n"
          "    t: Time-point index (0=first time point).\n\n"
          "The GIL is released during the read so concurrent Python threads\n"
          "can perform overlapping reads in parallel.",
          nb::arg("location"), nb::arg("level"), nb::arg("size"),
          nb::arg("z") = 0, nb::arg("t") = 0,
          nb::call_guard<nb::gil_scoped_release>())
      .def_prop_ro(
          "z_count",
          [](const SlideImageView& self) {
            return self.GetStackInfo().z_count;
          },
          "Number of focal planes (Z) in this image.")
      .def_prop_ro(
          "t_count",
          [](const SlideImageView& self) {
            return self.GetStackInfo().t_count;
          },
          "Number of time points (T) in this image.")
      .def_prop_ro(
          "z_spacing_um",
          [](const SlideImageView& self) {
            return self.GetStackInfo().z_spacing_um;
          },
          "Focal-plane spacing in microns, or None if unknown.")
      .def_prop_ro(
          "t_interval_s",
          [](const SlideImageView& self) {
            return self.GetStackInfo().t_interval_s;
          },
          "Time-point interval in seconds, or None if unknown.")
      .def(
          "get_stack_info",
          [](const SlideImageView& self) {
            const auto info = self.GetStackInfo();
            nb::dict d;
            d["z_count"] = info.z_count;
            d["t_count"] = info.t_count;
            d["z_spacing_um"] = info.z_spacing_um;
            d["t_interval_s"] = info.t_interval_s;
            return d;
          },
          "Z/T stack extent and spacing as a dict.")
      .def_prop_ro("channel_metadata", &SlideImageView::GetChannelMetadata,
                   "List of per-channel metadata dictionaries for this image.")
      .def_prop_ro("num_channels", &SlideImageView::GetNumChannels,
                   "Number of channels a read_region of this image returns.")
      .def("get_best_level_for_downsample",
           &SlideImageView::GetBestLevelForDownsample,
           "Best level for a given downsample factor.", nb::arg("downsample"));

  // Indexable container of ``SlideImageView``s, exposed as ``slide.images``.
  nb::class_<SlideImages>(m, "SlideImages")
      .def("__len__", &SlideImages::Len)
      .def("__getitem__", &SlideImages::GetItem, nb::arg("index"))
      .def(
          "__iter__",
          [](SlideImages& self) {
            const size_t n = self.Len();
            nb::list items;
            for (size_t i = 0; i < n; ++i) {
              items.append(self.GetItem(static_cast<int>(i)));
            }
            return nb::iter(items);
          },
          "Iterate over images in order.")
      .def_prop_ro("primary_index", &SlideImages::GetPrimaryIndex,
                   "Index of the primary (largest) image.")
      .def_prop_ro(
          "primary",
          [](SlideImages& self) {
            return self.GetItem(self.GetPrimaryIndex());
          },
          "The primary ``SlideImageView``.")
      .def("names", &SlideImages::Names, "Names of all images, in order.");

  // Main slide reader class with factory methods
  nb::class_<FastSlide>(m, "FastSlide")
      .def_static(
          "from_file_path",
          [](const nb::object& file_path, bool apply_icc,
             const nb::object& cache) {
            std::string path_str;
            if (nb::isinstance<nb::str>(file_path)) {
              path_str = nb::cast<std::string>(file_path);
            } else if (nb::hasattr(file_path, "__fspath__")) {
              // Handle pathlib.Path objects
              auto path_obj = file_path.attr("__fspath__")();
              path_str = nb::cast<std::string>(path_obj);
            } else {
              path_str = nb::cast<std::string>(file_path);
            }
            auto slide = FastSlide::FromFilePath(path_str, apply_icc);
            if (slide && !cache.is_none()) {
              slide->SetCache(ResolveCacheObject(cache));
            }
            return slide;
          },
          "Create FastSlide from file path (accepts str or pathlib.Path)\n\n"
          "Args:\n"
          "    file_path: Path to the slide (str or pathlib.Path).\n"
          "    apply_icc: When True and the slide has an embedded ICC\n"
          "        profile, read_region returns sRGB-corrected pixels\n"
          "        (perceptual intent). Slides without a profile are\n"
          "        returned unchanged.\n"
          "    cache: Optional tile cache to attach. Accepts an int byte\n"
          "        capacity (a new per-slide LRU cache), a CacheManager, a\n"
          "        TileCache, or None to disable caching.",
          nb::arg("file_path"), nb::arg("apply_icc") = false,
          nb::arg("cache").none() = nb::none())
      .def_static("from_uri", &FastSlide::FromUri,
                  "Create FastSlide from URI (future)", nb::arg("uri"))

      // Main reading method (level-native coordinates)
      .def(
          "read_region",
          [](FastSlide& self, const std::tuple<uint32_t, uint32_t>& location,
             int level, const std::tuple<uint32_t, uint32_t>& size, uint32_t z,
             uint32_t t) {
            auto [x, y] = location;
            auto [width, height] = size;
            return self.ReadRegion(x, y, width, height, level, z, t);
          },
          "Read a region from the slide using level-native coordinates\n\n"
          "Args:\n"
          "    location: Tuple (x, y) coordinates in level-native space\n"
          "    level: Pyramid level (0=highest resolution)\n"
          "    size: Tuple (width, height) of region\n"
          "    z: Focal-plane index (0=first plane)\n"
          "    t: Time-point index (0=first time point)\n\n"
          "Returns:\n"
          "    fastslide.Image: Image object. Call .numpy() to get array "
          "view.\n\n"
          "Note: Coordinates are in level-native space. To convert from "
          "level-0\n"
          "coordinates, use convert_level0_to_level_native().\n\n"
          "The GIL is released for the duration of the read so that "
          "concurrent\n"
          "Python threads can perform overlapping reads in parallel.",
          nb::arg("location"), nb::arg("level"), nb::arg("size"),
          nb::arg("z") = 0, nb::arg("t") = 0,
          nb::call_guard<nb::gil_scoped_release>())
      .def(
          "get_stack_info",
          [](const FastSlide& self) {
            const auto info = self.GetStackInfo();
            nb::dict d;
            d["z_count"] = info.z_count;
            d["t_count"] = info.t_count;
            d["z_spacing_um"] = info.z_spacing_um;
            d["t_interval_s"] = info.t_interval_s;
            return d;
          },
          "Z/T stack extent and spacing of the primary image as a dict.")
      .def_prop_ro(
          "z_count",
          [](const FastSlide& self) { return self.GetStackInfo().z_count; },
          "Number of focal planes (Z) in the primary image.")
      .def_prop_ro(
          "t_count",
          [](const FastSlide& self) { return self.GetStackInfo().t_count; },
          "Number of time points (T) in the primary image.")
      .def_prop_ro(
          "z_spacing_um",
          [](const FastSlide& self) {
            return self.GetStackInfo().z_spacing_um;
          },
          "Focal-plane spacing in microns, or None if unknown.")
      .def_prop_ro(
          "t_interval_s",
          [](const FastSlide& self) {
            return self.GetStackInfo().t_interval_s;
          },
          "Time-point interval in seconds, or None if unknown.")

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
           nb::arg("x"), nb::arg("y"), nb::arg("level"))

      .def("convert_level_native_to_level0",
           &FastSlide::ConvertLevelNativeToLevel0,
           "Convert level-native coordinates to level-0 coordinates\n\n"
           "Args:\n"
           "    x: X coordinate in level-native space\n"
           "    y: Y coordinate in level-native space\n"
           "    level: Source level\n\n"
           "Returns:\n"
           "    tuple: (level_0_x, level_0_y)",
           nb::arg("x"), nb::arg("y"), nb::arg("level"))

      // Properties
      .def_prop_ro("dimensions", &FastSlide::GetDimensions,
                   "Slide dimensions (width, height) at level 0")
      .def_prop_ro("level_dimensions", &FastSlide::GetLevelDimensions,
                   "Tuple of (width, height) for each level")
      .def_prop_ro("level_downsamples", &FastSlide::GetLevelDownsamples,
                   "Tuple of downsample factors for each level")
      .def_prop_ro("level_count", &FastSlide::GetLevelCount,
                   "Number of levels in the slide")
      .def_prop_ro("properties", &FastSlide::GetProperties,
                   "Dictionary of slide properties and metadata")
      .def_prop_ro("mpp", &FastSlide::GetMpp,
                   "Microns per pixel as (mpp_x, mpp_y) tuple")
      .def_prop_ro("bounds", &FastSlide::GetBounds,
                   "Bounding box of non-empty region\n\n"
                   "Returns tuple with coordinates and size:\n"
                   "  (x, y) tuple: Top-left coordinates (level 0)\n"
                   "  (width, height) tuple: Bounding box size")
      .def_prop_ro("format", &FastSlide::GetFormat, "File format name")
      .def_prop_ro("dtype", &FastSlide::GetDtype,
                   "Pixel data type (e.g. 'uint8', 'uint16')")
      .def_prop_ro("icc_profile", &FastSlide::GetIccProfile,
                   "Embedded ICC profile as bytes, or None if the slide has "
                   "no color profile.")
      .def_prop_ro("apply_icc", &FastSlide::GetApplyIcc,
                   "True if ICC color management is applied on read_region "
                   "(see from_file_path(apply_icc=True)).")
      .def_prop_ro(
          "quickhash", &FastSlide::GetQuickHash,
          "SHA-256 quickhash (unique identifier, OpenSlide-compatible)")
      .def_prop_ro("channel_metadata", &FastSlide::GetChannelMetadata,
                   "List of channel metadata dictionaries")
      .def_prop_ro("num_channels", &FastSlide::GetNumChannels,
                   "Number of channels a read_region returns")
      .def_prop_ro(
          "images", &FastSlide::GetImages,
          "Sequence of navigable images (pyramids) in this file.\n\n"
          "For most slides this has length 1. Olympus VSI files can\n"
          "expose a low-resolution 'navigator' image alongside one or\n"
          "more high-resolution 'region' images. The top-level\n"
          "``FastSlide`` properties (``dimensions``, ``level_count``,\n"
          "``read_region``, ...) forward to ``slide.images.primary``.",
          nb::rv_policy::reference_internal)
      .def_prop_ro(
          "num_images", [](FastSlide& self) { return self.GetImages().Len(); },
          "Number of navigable images (pyramids) in this file.\n\n"
          "Equivalent to ``len(slide.images)``.")
      .def_prop_ro("associated_images", &FastSlide::GetAssociatedImages,
                   "Dictionary-like access to associated images (lazy loaded)",
                   nb::rv_policy::reference_internal)
      .def_prop_ro("associated_data", &FastSlide::GetAssociatedData,
                   "Dictionary-like access to associated data: XML, "
                   "binary (lazy loaded, MRXS only)",
                   nb::rv_policy::reference_internal)
      .def_prop_ro("closed", &FastSlide::IsClosed,
                   "True if the slide reader is closed")
      .def_prop_ro("source_path", &FastSlide::GetSourcePath, "Source file path")

      // Utility methods
      .def("get_best_level_for_downsample",
           &FastSlide::GetBestLevelForDownsample,
           "Get the best level for a given downsample factor",
           nb::arg("downsample"))

      // Cache management.
      //
      // `set_cache` accepts an int byte capacity (a new per-slide LRU cache),
      // a `TileCache`, a `CacheManager`, or None. A single entrypoint
      // dispatches on argument type so callers do not need to unwrap the
      // manager themselves.
      .def(
          "set_cache",
          [](FastSlide& self, const nb::object& cache) {
            self.SetCache(ResolveCacheObject(cache));
          },
          "Set cache (accepts int bytes, TileCache, CacheManager, or None to "
          "disable).",
          nb::arg("cache").none())
      .def("get_cache", &FastSlide::GetCache, "Get current cache")
      .def_prop_ro("cache_enabled", &FastSlide::IsCacheEnabled,
                   "True if caching is enabled")
      .def(
          "use_global_cache",
          [](FastSlide& self) {
            self.SetCache(
                fastslide::runtime::GlobalCacheManager::Instance().GetCache());
          },
          "Attach the process-wide global tile cache to this slide.")
      .def(
          "clear_cache",
          [](FastSlide& self) {
            if (auto cache = self.GetCache()) {
              cache->Clear();
            }
          },
          "Clear all tiles from this slide's cache (no-op if none attached).")
      .def_prop_ro(
          "cache_stats",
          [](FastSlide& self) -> nb::object {
            auto cache = self.GetCache();
            if (!cache) {
              return nb::none();
            }
            return nb::cast(cache->GetStats());
          },
          "Cache statistics (RuntimeCacheStats), or None if no cache is "
          "attached.")

      // Resource management
      .def("close", &FastSlide::Close,
           "Close the slide reader and release resources")

      // Context manager support
      .def("__enter__", &FastSlide::__enter__,
           nb::rv_policy::reference_internal)
      // Python passes (None, None, None) on normal exit, so each parameter
      // must explicitly accept None - by default nanobind rejects it for
      // nb::object parameters.
      .def("__exit__", &FastSlide::__exit__, nb::arg("exc_type").none(),
           nb::arg("exc_value").none(), nb::arg("traceback").none());

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
      "Check if file format is supported", nb::arg("filename"));

  // Version and constants
  m.attr("__version__") = "0.8.0";
}
