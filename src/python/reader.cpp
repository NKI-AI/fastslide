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

#include "fastslide/python/reader.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fastslide/readers/mrxs/mrxs.h"
#include "fastslide/readers/tiff_based_reader.h"
#include "fastslide/runtime/global_cache_manager.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/slide_reader.h"

namespace fastslide::python {

using fastslide::RegionSpec;

// AssociatedImages implementation
AssociatedImages::AssociatedImages(std::shared_ptr<SlideReader> reader)
    : reader_(reader) {}

std::shared_ptr<SlideReader> AssociatedImages::GetReader() const {
  auto reader = reader_.lock();
  if (!reader) {
    throw std::runtime_error(
        "Cannot access associated images: slide reader is closed");
  }
  return reader;
}

void AssociatedImages::EnsureNamesLoaded() const {
  if (!names_loaded_) {
    auto reader = GetReader();
    available_names_ = reader->GetAssociatedImageNames();

    // Also add image-type data from MRXS nonhier layers
    auto* mrxs_reader = dynamic_cast<MrxsReader*>(reader.get());
    if (mrxs_reader) {
      auto all_data_names = mrxs_reader->GetAssociatedDataNames();
      for (const auto& name : all_data_names) {
        // Move items starting with "ScanDataLayer_Slide" to associated_images
        const std::string prefix = "ScanDataLayer_Slide";
        if (name.find(prefix) == 0) {
          // Strip prefix and add to images
          std::string stripped_name = name.substr(prefix.length());
          available_names_.push_back(stripped_name);
        }
      }
    }

    names_loaded_ = true;
  }
}

py::array_t<uint8_t> AssociatedImages::GetItem(const std::string& name) const {
  EnsureNamesLoaded();

  // Check if image exists
  if (std::find(available_names_.begin(), available_names_.end(), name) ==
      available_names_.end()) {
    throw py::key_error("Associated image '" + name + "' not found");
  }

  // Check cache first
  auto cache_it = cache_.find(name);
  if (cache_it != cache_.end() && cache_it->second.has_value()) {
    return cache_it->second.value();
  }

  // Try to load from standard associated images first
  auto reader = GetReader();
  auto result = reader->ReadAssociatedImage(name);

  if (!result.ok()) {
    // Try loading from MRXS nonhier layers
    // Need to add back the prefix we stripped
    auto* mrxs_reader = dynamic_cast<MrxsReader*>(reader.get());
    if (mrxs_reader) {
      std::string full_name = "ScanDataLayer_Slide" + name;
      auto data_or = mrxs_reader->LoadAssociatedData(full_name);
      if (data_or.ok() && data_or->IsImage()) {
        const auto* image = data_or->GetImage();
        if (image) {
          const auto& dims = image->GetDimensions();
          std::vector<ssize_t> shape = {static_cast<ssize_t>(dims[1]),
                                        static_cast<ssize_t>(dims[0]), 3};
          py::array_t<uint8_t> result_array(shape);
          std::memcpy(result_array.mutable_data(), image->GetData(),
                      dims[0] * dims[1] * 3);
          cache_[name] = result_array;
          return result_array;
        }
      }
    }

    throw std::runtime_error("Failed to read associated image '" + name +
                             "': " + std::string(result.status().message()));
  }

  const auto& image = result.value();
  std::vector<ssize_t> shape = {static_cast<ssize_t>(image.GetDimensions()[1]),
                                static_cast<ssize_t>(image.GetDimensions()[0]),
                                3};

  py::array_t<uint8_t> result_array(shape);
  auto buf = result_array.request();
  std::memcpy(buf.ptr, image.GetData(), image.SizeBytes());

  // Cache the result
  cache_[name] = result_array;

  return result_array;
}

bool AssociatedImages::Contains(const std::string& name) const {
  EnsureNamesLoaded();
  return std::find(available_names_.begin(), available_names_.end(), name) !=
         available_names_.end();
}

std::vector<std::string> AssociatedImages::Keys() const {
  EnsureNamesLoaded();
  return available_names_;
}

py::tuple AssociatedImages::GetDimensions(const std::string& name) const {
  EnsureNamesLoaded();

  if (!Contains(name)) {
    throw py::key_error("Associated image '" + name + "' not found");
  }

  auto reader = GetReader();
  auto dims_or = reader->GetAssociatedImageDimensions(name);

  if (!dims_or.ok()) {
    // Try MRXS nonhier with prefix added back
    auto* mrxs_reader = dynamic_cast<MrxsReader*>(reader.get());
    if (mrxs_reader) {
      std::string full_name = "ScanDataLayer_Slide" + name;
      auto data_or = mrxs_reader->LoadAssociatedData(full_name);
      if (data_or.ok() && data_or->IsImage()) {
        const auto* image = data_or->GetImage();
        if (image) {
          const auto& dims = image->GetDimensions();
          return py::make_tuple(dims[0], dims[1]);
        }
      }
    }

    throw std::runtime_error("Failed to get dimensions for '" + name +
                             "': " + std::string(dims_or.status().message()));
  }

  const auto& dims = dims_or.value();
  return py::make_tuple(dims[0], dims[1]);
}

size_t AssociatedImages::GetCacheSize() const {
  return std::count_if(cache_.begin(), cache_.end(), [](const auto& pair) {
    return pair.second.has_value();
  });
}

void AssociatedImages::ClearCache() const {
  cache_.clear();
}

// AssociatedData implementation
AssociatedData::AssociatedData(std::shared_ptr<SlideReader> reader)
    : reader_(reader) {}

std::shared_ptr<SlideReader> AssociatedData::GetReader() const {
  auto reader = reader_.lock();
  if (!reader) {
    throw std::runtime_error(
        "Cannot access associated data: slide reader is closed");
  }
  return reader;
}

void AssociatedData::EnsureNamesLoaded() const {
  if (!names_loaded_) {
    auto reader = GetReader();

    // Only MRXS supports associated data currently
    auto* mrxs_reader = dynamic_cast<MrxsReader*>(reader.get());
    if (mrxs_reader) {
      auto all_names = mrxs_reader->GetAssociatedDataNames();
      // Filter out images (items starting with "ScanDataLayer_Slide")
      // Those go in AssociatedImages with the prefix stripped
      const std::string image_prefix = "ScanDataLayer_Slide";
      for (const auto& name : all_names) {
        if (name.find(image_prefix) != 0) {
          // Not an image, keep in associated_data
          available_names_.push_back(name);
        }
      }
    }

    names_loaded_ = true;
  }
}

py::object AssociatedData::GetItem(const std::string& name) const {
  EnsureNamesLoaded();

  // Check if data exists
  if (std::find(available_names_.begin(), available_names_.end(), name) ==
      available_names_.end()) {
    throw py::key_error("Associated data '" + name + "' not found");
  }

  // Check cache first
  auto cache_it = cache_.find(name);
  if (cache_it != cache_.end() && cache_it->second.has_value()) {
    return cache_it->second.value();
  }

  // Load data from MRXS nonhier layers
  auto reader = GetReader();
  auto* mrxs_reader = dynamic_cast<MrxsReader*>(reader.get());
  if (!mrxs_reader) {
    throw std::runtime_error("Associated data only supported for MRXS files");
  }

  auto data_or = mrxs_reader->LoadAssociatedData(name);
  if (!data_or.ok()) {
    throw std::runtime_error("Failed to load associated data '" + name +
                             "': " + std::string(data_or.status().message()));
  }

  const auto& data = *data_or;

  // Return appropriate Python type
  py::object result;
  if (data.IsXml()) {
    const auto* xml = data.GetXml();
    if (!xml) {
      throw std::runtime_error("XML data is null");
    }
    result = py::str(*xml);
  } else {
    const auto* binary = data.GetBinary();
    if (!binary) {
      throw std::runtime_error("Binary data is null");
    }
    result = py::bytes(reinterpret_cast<const char*>(binary->data()),
                       binary->size());
  }

  // Cache the result
  cache_[name] = result;
  return result;
}

bool AssociatedData::Contains(const std::string& name) const {
  EnsureNamesLoaded();
  return std::find(available_names_.begin(), available_names_.end(), name) !=
         available_names_.end();
}

std::vector<std::string> AssociatedData::Keys() const {
  EnsureNamesLoaded();
  return available_names_;
}

std::string AssociatedData::GetType(const std::string& name) const {
  EnsureNamesLoaded();

  if (!Contains(name)) {
    throw py::key_error("Associated data '" + name + "' not found");
  }

  auto reader = GetReader();
  auto* mrxs_reader = dynamic_cast<MrxsReader*>(reader.get());
  if (!mrxs_reader) {
    return "unknown";
  }

  auto info_or = mrxs_reader->GetAssociatedDataInfo(name);
  if (!info_or.ok()) {
    return "unknown";
  }

  return GetTypeName(info_or->type);
}

size_t AssociatedData::GetCacheSize() const {
  return std::count_if(cache_.begin(), cache_.end(), [](const auto& pair) {
    return pair.second.has_value();
  });
}

void AssociatedData::ClearCache() const {
  cache_.clear();
}

// FastSlide implementation
FastSlide::FastSlide(std::shared_ptr<SlideReader> reader,
                     const std::string& source_path)
    : reader_(std::move(reader)), is_closed_(false), source_path_(source_path) {
  associated_images_ = std::make_unique<AssociatedImages>(reader_);
  associated_data_ = std::make_unique<AssociatedData>(reader_);
}

std::unique_ptr<FastSlide> FastSlide::FromFilePath(
    const std::string& file_path) {
  auto reader_or =
      fastslide::runtime::GetGlobalRegistry().CreateReader(file_path);
  if (!reader_or.ok()) {
    throw std::runtime_error("Failed to open slide '" + file_path +
                             "': " + std::string(reader_or.status().message()));
  }

  return std::make_unique<FastSlide>(std::move(reader_or.value()), file_path);
}

std::unique_ptr<FastSlide> FastSlide::FromUri(const std::string& uri) {
  // TODO(jonasteuwen): Implement URI-based loading
  throw std::runtime_error("URI-based loading not yet implemented");
}

void FastSlide::Close() {
  if (!is_closed_) {
    reader_.reset();
    associated_images_.reset();
    cache_manager_.reset();
    is_closed_ = true;
  }
}

bool FastSlide::IsClosed() const {
  return is_closed_;
}

FastSlide& FastSlide::__enter__() {
  return *this;
}

bool FastSlide::__exit__(py::object exc_type, py::object exc_value,
                         py::object traceback) {
  Close();
  return false;
}

py::array_t<uint8_t> FastSlide::ReadRegion(uint32_t x, uint32_t y,
                                           uint32_t width, uint32_t height,
                                           int level) {
  if (is_closed_) {
    throw std::runtime_error("Cannot read region: slide reader is closed");
  }

  RegionSpec region{
      .top_left = {x, y}, .size = {width, height}, .level = level};

  auto result = reader_->ReadRegion(region);
  if (!result.ok()) {
    throw std::runtime_error("Failed to read region: " +
                             std::string(result.status().message()));
  }

  const auto& image = result.value();
  std::vector<ssize_t> shape = {static_cast<ssize_t>(image.GetDimensions()[1]),
                                static_cast<ssize_t>(image.GetDimensions()[0]),
                                3};

  py::array_t<uint8_t> result_array(shape);
  auto buf = result_array.request();
  std::memcpy(buf.ptr, image.GetData(), image.SizeBytes());
  return result_array;
}

AssociatedImages& FastSlide::GetAssociatedImages() {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot access associated images: slide reader is closed");
  }
  return *associated_images_;
}

AssociatedData& FastSlide::GetAssociatedData() {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot access associated data: slide reader is closed");
  }
  return *associated_data_;
}

py::tuple FastSlide::GetDimensions() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get dimensions: slide reader is closed");
  }
  auto level_info_or = reader_->GetLevelInfo(0);
  if (!level_info_or.ok()) {
    throw std::runtime_error("Failed to get level 0 info");
  }
  const auto& info = level_info_or.value();
  return py::make_tuple(info.dimensions[0], info.dimensions[1]);
}

py::tuple FastSlide::GetLevelDimensions() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get level dimensions: slide reader is closed");
  }

  int level_count = reader_->GetLevelCount();
  py::tuple result(level_count);

  for (int i = 0; i < level_count; ++i) {
    auto level_info_or = reader_->GetLevelInfo(i);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level " + std::to_string(i) +
                               " info");
    }
    const auto& info = level_info_or.value();
    result[i] = py::make_tuple(info.dimensions[0], info.dimensions[1]);
  }
  return result;
}

py::tuple FastSlide::GetLevelDownsamples() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get level downsamples: slide reader is closed");
  }

  int level_count = reader_->GetLevelCount();
  py::tuple result(level_count);

  for (int i = 0; i < level_count; ++i) {
    auto level_info_or = reader_->GetLevelInfo(i);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level " + std::to_string(i) +
                               " info");
    }
    result[i] = level_info_or.value().downsample_factor;
  }
  return result;
}

int FastSlide::GetLevelCount() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get level count: slide reader is closed");
  }
  return reader_->GetLevelCount();
}

py::dict FastSlide::GetProperties() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get properties: slide reader is closed");
  }

  const auto& props = reader_->GetProperties();
  py::dict result;
  result["mpp_x"] = props.mpp[0];
  result["mpp_y"] = props.mpp[1];
  result["objective_magnification"] = props.objective_magnification;
  result["objective_name"] = props.objective_name;
  result["scanner_model"] = props.scanner_model;
  if (props.scan_date) {
    result["scan_date"] = *props.scan_date;
  }

  // Add format-specific metadata
  auto metadata = reader_->GetMetadata();
  for (const auto& [key, value] : metadata) {
    result[key.c_str()] = value;
  }

  // Add source information
  result["source_path"] = source_path_;

  return result;
}

std::string FastSlide::GetFormat() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get format: slide reader is closed");
  }
  return reader_->GetFormatName();
}

std::string FastSlide::GetQuickHash() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get quickhash: slide reader is closed");
  }
  auto hash_or = reader_->GetQuickHash();
  if (!hash_or.ok()) {
    throw std::runtime_error("Failed to get quickhash: " +
                             std::string(hash_or.status().message()));
  }
  return hash_or.value();
}

py::tuple FastSlide::GetMpp() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get MPP: slide reader is closed");
  }
  const auto& props = reader_->GetProperties();
  return py::make_tuple(props.mpp[0], props.mpp[1]);
}

py::dict FastSlide::GetBounds() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get bounds: slide reader is closed");
  }
  const auto& props = reader_->GetProperties();
  const auto& bounds = props.bounds;

  py::dict result;
  result["x"] = bounds.x;
  result["y"] = bounds.y;
  result["width"] = bounds.width;
  result["height"] = bounds.height;
  result["coordinates"] = py::make_tuple(bounds.x, bounds.y);
  result["size"] = py::make_tuple(bounds.width, bounds.height);

  return result;
}

py::list FastSlide::GetChannelMetadata() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get channel metadata: slide reader is closed");
  }

  auto channel_metadata = reader_->GetChannelMetadata();
  py::list result;

  for (const auto& channel : channel_metadata) {
    py::dict channel_dict;
    channel_dict["name"] = channel.name;
    channel_dict["biomarker"] = channel.biomarker;
    channel_dict["color"] =
        py::make_tuple(channel.color[0], channel.color[1], channel.color[2]);
    channel_dict["exposure_time"] = channel.exposure_time;
    channel_dict["signal_units"] = channel.signal_units;

    // Add additional metadata
    for (const auto& [key, value] : channel.additional) {
      channel_dict[key.c_str()] = value;
    }

    result.append(channel_dict);
  }

  return result;
}

int FastSlide::GetBestLevelForDownsample(double downsample) const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get best level: slide reader is closed");
  }
  return reader_->GetBestLevelForDownsample(downsample);
}

void FastSlide::SetCacheManager(std::shared_ptr<CacheManager> cache_manager) {
  if (is_closed_) {
    throw std::runtime_error("Cannot set cache: slide reader is closed");
  }
  cache_manager_ = cache_manager;
  reader_->SetCache(cache_manager->GetCache());
}

std::shared_ptr<CacheManager> FastSlide::GetCacheManager() const {
  return cache_manager_;
}

void FastSlide::UseGlobalCache() {
  if (is_closed_) {
    throw std::runtime_error("Cannot set cache: slide reader is closed");
  }
  // Use the runtime global cache manager with the modern ITileCache interface
  auto* tiff_reader = dynamic_cast<TiffBasedReader*>(reader_.get());
  if (tiff_reader) {
    tiff_reader->SetCache(
        fastslide::runtime::GlobalCacheManager::Instance().GetCache());
  } else {
    throw std::runtime_error(
        "Global cache requires TiffBasedReader implementation");
  }
}

bool FastSlide::IsCacheEnabled() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot check cache status: slide reader is closed");
  }
  return reader_->IsCacheEnabled();
}

std::string FastSlide::GetSourcePath() const {
  return source_path_;
}

py::tuple FastSlide::ConvertLevel0ToLevelNative(int64_t x, int64_t y,
                                                int level) const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot convert coordinates: slide reader is closed");
  }

  if (level == 0) {
    return py::make_tuple(x, y);
  }

  auto level_info_or = reader_->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    throw std::runtime_error("Invalid level " + std::to_string(level) + ": " +
                             std::string(level_info_or.status().message()));
  }

  double downsample = level_info_or.value().downsample_factor;
  uint32_t native_x = static_cast<uint32_t>(x / downsample);
  uint32_t native_y = static_cast<uint32_t>(y / downsample);

  return py::make_tuple(native_x, native_y);
}

py::tuple FastSlide::ConvertLevelNativeToLevel0(uint32_t x, uint32_t y,
                                                int level) const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot convert coordinates: slide reader is closed");
  }

  if (level == 0) {
    return py::make_tuple(x, y);
  }

  auto level_info_or = reader_->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    throw std::runtime_error("Invalid level " + std::to_string(level) + ": " +
                             std::string(level_info_or.status().message()));
  }

  double downsample = level_info_or.value().downsample_factor;
  int64_t level0_x = static_cast<int64_t>(x * downsample);
  int64_t level0_y = static_cast<int64_t>(y * downsample);

  return py::make_tuple(level0_x, level0_y);
}

// Channel visibility controls
void FastSlide::SetVisibleChannels(const std::vector<size_t>& channel_indices) {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot set visible channels: slide reader is closed");
  }
  reader_->SetVisibleChannels(channel_indices);
}

std::vector<size_t> FastSlide::GetVisibleChannels() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get visible channels: slide reader is closed");
  }
  return reader_->GetVisibleChannels();
}

void FastSlide::ShowAllChannels() {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot show all channels: slide reader is closed");
  }
  reader_->ShowAllChannels();
}

}  // namespace fastslide::python
