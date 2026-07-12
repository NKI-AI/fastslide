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

#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
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

namespace {

/// @brief Convert C++ channel metadata into a list of Python dicts.
///
/// Shared by the slide-level (`FastSlide.channel_metadata`) and image-level
/// (`SlideImageView.channel_metadata`) accessors so both expose the same
/// shape, including any format-specific `additional` fields.
nb::list BuildChannelMetadataList(
    const std::vector<fastslide::ChannelMetadata>& channels) {
  nb::list result;
  for (const auto& channel : channels) {
    nb::dict channel_dict;
    channel_dict["name"] = channel.name;
    channel_dict["biomarker"] = channel.biomarker;
    channel_dict["color"] =
        nb::make_tuple(channel.color[0], channel.color[1], channel.color[2]);
    channel_dict["exposure_time"] = channel.exposure_time;
    channel_dict["signal_units"] = channel.signal_units;
    for (const auto& [key, value] : channel.additional) {
      channel_dict[key.c_str()] = value;
    }
    result.append(channel_dict);
  }
  return result;
}

/// @brief Number of channels a `read_region` returns for a given format.
///
/// Standard formats map directly to their channel count (Gray=1, RGB=3,
/// RGBA=4); spectral images carry one plane per channel-metadata entry.
int NumChannelsFromFormat(
    fastslide::ImageFormat format,
    const std::vector<fastslide::ChannelMetadata>& channels) {
  if (format == fastslide::ImageFormat::kSpectral) {
    return static_cast<int>(channels.size());
  }
  return static_cast<int>(fastslide::GetFormatChannels(format));
}

}  // namespace

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
    names_loaded_ = true;
  }
}

std::shared_ptr<fastslide::Image> AssociatedImages::GetItem(
    const std::string& name) const {
  EnsureNamesLoaded();

  if (std::find(available_names_.begin(), available_names_.end(), name) ==
      available_names_.end()) {
    throw nb::key_error(("Associated image '" + name + "' not found").c_str());
  }

  auto cache_it = cache_.find(name);
  if (cache_it != cache_.end() && cache_it->second) {
    return cache_it->second;
  }

  auto reader = GetReader();
  auto result = reader->ReadAssociatedImage(name);
  if (!result.ok()) {
    throw std::runtime_error("Failed to read associated image '" + name +
                             "': " + std::string(result.status().message()));
  }

  auto result_image =
      std::make_shared<fastslide::Image>(std::move(result.value()));
  cache_[name] = result_image;
  return result_image;
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

nb::tuple AssociatedImages::GetDimensions(const std::string& name) const {
  EnsureNamesLoaded();

  if (!Contains(name)) {
    throw nb::key_error(("Associated image '" + name + "' not found").c_str());
  }

  auto reader = GetReader();
  auto dims_or = reader->GetAssociatedImageDimensions(name);
  if (!dims_or.ok()) {
    throw std::runtime_error("Failed to get dimensions for '" + name +
                             "': " + std::string(dims_or.status().message()));
  }
  const auto& dims = dims_or.value();
  return nb::make_tuple(dims[0], dims[1]);
}

size_t AssociatedImages::GetCacheSize() const {
  return std::count_if(cache_.begin(), cache_.end(),
                       [](const auto& pair) { return pair.second != nullptr; });
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
    if (auto* mrxs_reader = dynamic_cast<MrxsReader*>(reader.get())) {
      available_names_ = mrxs_reader->GetNonImageAssociatedDataNames();
    }
    names_loaded_ = true;
  }
}

nb::object AssociatedData::GetItem(const std::string& name) const {
  EnsureNamesLoaded();

  // Check if data exists
  if (std::find(available_names_.begin(), available_names_.end(), name) ==
      available_names_.end()) {
    throw nb::key_error(("Associated data '" + name + "' not found").c_str());
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
  nb::object result;
  if (data.IsXml()) {
    const auto* xml = data.GetXml();
    if (!xml) {
      throw std::runtime_error("XML data is null");
    }
    result = nb::str(xml->c_str(), xml->size());
  } else {
    const auto* binary = data.GetBinary();
    if (!binary) {
      throw std::runtime_error("Binary data is null");
    }
    result = nb::bytes(reinterpret_cast<const char*>(binary->data()),
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
    throw nb::key_error(("Associated data '" + name + "' not found").c_str());
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

// ---------------------------------------------------------------------
// SlideImageView (one image inside a slide)
// ---------------------------------------------------------------------

SlideImageView::SlideImageView(std::weak_ptr<SlideReader> reader, int index)
    : reader_(std::move(reader)), index_(index) {}

std::shared_ptr<SlideReader> SlideImageView::GetReader() const {
  auto reader = reader_.lock();
  if (!reader) {
    throw std::runtime_error("Cannot access image: slide reader is closed");
  }
  return reader;
}

const SlideImage* SlideImageView::GetImage() const {
  auto reader = GetReader();
  auto image_or = reader->GetImage(index_);
  if (!image_or.ok()) {
    throw std::runtime_error("Failed to access image " +
                             std::to_string(index_) + ": " +
                             std::string(image_or.status().message()));
  }
  return image_or.value();
}

std::string SlideImageView::GetName() const {
  return GetImage()->GetName();
}

int SlideImageView::GetLevelCount() const {
  return GetImage()->GetLevelCount();
}

nb::tuple SlideImageView::GetDimensions() const {
  const auto* image = GetImage();
  auto level_info_or = image->GetLevelInfo(0);
  if (!level_info_or.ok()) {
    throw std::runtime_error("Failed to get level 0 info for image " +
                             std::to_string(index_));
  }
  const auto& info = level_info_or.value();
  return nb::make_tuple(info.dimensions[0], info.dimensions[1]);
}

nb::tuple SlideImageView::GetLevelDimensions() const {
  const auto* image = GetImage();
  const int level_count = image->GetLevelCount();
  nb::list result;
  for (int i = 0; i < level_count; ++i) {
    auto level_info_or = image->GetLevelInfo(i);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level " + std::to_string(i) +
                               " info for image " + std::to_string(index_));
    }
    const auto& info = level_info_or.value();
    result.append(nb::make_tuple(info.dimensions[0], info.dimensions[1]));
  }
  return nb::tuple(result);
}

nb::tuple SlideImageView::GetLevelDownsamples() const {
  const auto* image = GetImage();
  const int level_count = image->GetLevelCount();
  nb::list result;
  for (int i = 0; i < level_count; ++i) {
    auto level_info_or = image->GetLevelInfo(i);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level " + std::to_string(i) +
                               " info for image " + std::to_string(index_));
    }
    result.append(level_info_or.value().downsample_factor);
  }
  return nb::tuple(result);
}

nb::tuple SlideImageView::GetMpp() const {
  const auto& props = GetImage()->GetProperties();
  return nb::make_tuple(props.mpp[0], props.mpp[1]);
}

std::shared_ptr<fastslide::Image> SlideImageView::ReadRegion(
    uint32_t x, uint32_t y, uint32_t width, uint32_t height, int level,
    uint32_t z, uint32_t t) {
  const auto* image = GetImage();
  RegionSpec region{.top_left = {x, y},
                    .size = {width, height},
                    .level = level,
                    .plane = {z, t}};
  auto result = image->ReadRegion(region);
  if (!result.ok()) {
    throw std::runtime_error("Failed to read region from image " +
                             std::to_string(index_) + ": " +
                             std::string(result.status().message()));
  }
  return std::make_shared<fastslide::Image>(std::move(result.value()));
}

StackInfo SlideImageView::GetStackInfo() const {
  return GetImage()->GetStackInfo();
}

nb::list SlideImageView::GetChannelMetadata() const {
  return BuildChannelMetadataList(GetImage()->GetChannelMetadata());
}

int SlideImageView::GetNumChannels() const {
  const auto* image = GetImage();
  return NumChannelsFromFormat(image->GetImageFormat(),
                               image->GetChannelMetadata());
}

int SlideImageView::GetBestLevelForDownsample(double downsample) const {
  return GetImage()->GetBestLevelForDownsample(downsample);
}

// ---------------------------------------------------------------------
// SlideImages (`slide.images`)
// ---------------------------------------------------------------------

SlideImages::SlideImages(std::weak_ptr<SlideReader> reader)
    : reader_(std::move(reader)) {}

std::shared_ptr<SlideReader> SlideImages::GetReader() const {
  auto reader = reader_.lock();
  if (!reader) {
    throw std::runtime_error("Cannot access images: slide reader is closed");
  }
  return reader;
}

size_t SlideImages::Len() const {
  return static_cast<size_t>(GetReader()->GetImageCount());
}

SlideImageView SlideImages::GetItem(int index) const {
  auto reader = GetReader();
  const int count = reader->GetImageCount();
  // Pythonic negative indexing.
  if (index < 0) {
    index += count;
  }
  if (index < 0 || index >= count) {
    throw nb::index_error(
        ("Image index out of range: " + std::to_string(index)).c_str());
  }
  return SlideImageView(reader_, index);
}

int SlideImages::GetPrimaryIndex() const {
  return GetReader()->GetPrimaryImageIndex();
}

std::vector<std::string> SlideImages::Names() const {
  return GetReader()->GetImageNames();
}

// FastSlide implementation
FastSlide::FastSlide(std::shared_ptr<SlideReader> reader,
                     const std::string& source_path)
    : reader_(std::move(reader)), is_closed_(false), source_path_(source_path) {
  associated_images_ = std::make_unique<AssociatedImages>(reader_);
  associated_data_ = std::make_unique<AssociatedData>(reader_);
  images_ = std::make_unique<SlideImages>(reader_);
}

std::unique_ptr<FastSlide> FastSlide::FromFilePath(const std::string& file_path,
                                                   bool apply_icc) {
  // Validate file existence up front so callers get a clean, predictable
  // error instead of a format-specific factory writing to stderr and
  // returning a half-initialised reader (e.g. the Aperio/TIFF factory does
  // this for missing paths).
  std::error_code ec;
  if (!std::filesystem::exists(file_path, ec) || ec) {
    throw std::runtime_error("Failed to open slide '" + file_path +
                             "': file does not exist");
  }

  auto reader_or =
      fastslide::runtime::GetGlobalRegistry().CreateReader(file_path);
  if (!reader_or.ok()) {
    throw std::runtime_error("Failed to open slide '" + file_path +
                             "': " + std::string(reader_or.status().message()));
  }

  auto reader = std::move(reader_or.value());
  if (apply_icc) {
    // Enabling on a slide with no embedded profile is a successful no-op; a
    // non-ok status means the profile was present but could not be used.
    const auto status = reader->SetColorTransform();
    if (!status.ok()) {
      throw std::runtime_error("Failed to enable ICC color management for '" +
                               file_path +
                               "': " + std::string(status.message()));
    }
  }

  return std::make_unique<FastSlide>(std::move(reader), file_path);
}

std::unique_ptr<FastSlide> FastSlide::FromUri(const std::string& uri) {
  // TODO(jonasteuwen): Implement URI-based loading
  throw std::runtime_error("URI-based loading not yet implemented");
}

void FastSlide::Close() {
  if (!is_closed_) {
    reader_.reset();
    associated_images_.reset();
    associated_data_.reset();
    images_.reset();
    cache_.reset();
    is_closed_ = true;
  }
}

bool FastSlide::IsClosed() const {
  return is_closed_;
}

FastSlide& FastSlide::__enter__() {
  return *this;
}

bool FastSlide::__exit__(nb::object exc_type, nb::object exc_value,
                         nb::object traceback) {
  Close();
  return false;
}

std::shared_ptr<fastslide::Image> FastSlide::ReadRegion(uint32_t x, uint32_t y,
                                                        uint32_t width,
                                                        uint32_t height,
                                                        int level, uint32_t z,
                                                        uint32_t t) {
  if (is_closed_) {
    throw std::runtime_error("Cannot read region: slide reader is closed");
  }

  RegionSpec region{.top_left = {x, y},
                    .size = {width, height},
                    .level = level,
                    .plane = {z, t}};

  auto result = reader_->ReadRegion(region);
  if (!result.ok()) {
    throw std::runtime_error("Failed to read region: " +
                             std::string(result.status().message()));
  }

  return std::make_shared<fastslide::Image>(std::move(result.value()));
}

StackInfo FastSlide::GetStackInfo() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get stack info: slide reader is closed");
  }
  return reader_->GetStackInfo();
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

SlideImages& FastSlide::GetImages() {
  if (is_closed_) {
    throw std::runtime_error("Cannot access images: slide reader is closed");
  }
  return *images_;
}

nb::tuple FastSlide::GetDimensions() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get dimensions: slide reader is closed");
  }
  auto level_info_or = reader_->GetLevelInfo(0);
  if (!level_info_or.ok()) {
    throw std::runtime_error("Failed to get level 0 info");
  }
  const auto& info = level_info_or.value();
  return nb::make_tuple(info.dimensions[0], info.dimensions[1]);
}

nb::tuple FastSlide::GetLevelDimensions() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get level dimensions: slide reader is closed");
  }

  int level_count = reader_->GetLevelCount();
  nb::list result;

  for (int i = 0; i < level_count; ++i) {
    auto level_info_or = reader_->GetLevelInfo(i);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level " + std::to_string(i) +
                               " info");
    }
    const auto& info = level_info_or.value();
    result.append(nb::make_tuple(info.dimensions[0], info.dimensions[1]));
  }
  return nb::tuple(result);
}

nb::tuple FastSlide::GetLevelDownsamples() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get level downsamples: slide reader is closed");
  }

  int level_count = reader_->GetLevelCount();
  nb::list result;

  for (int i = 0; i < level_count; ++i) {
    auto level_info_or = reader_->GetLevelInfo(i);
    if (!level_info_or.ok()) {
      throw std::runtime_error("Failed to get level " + std::to_string(i) +
                               " info");
    }
    result.append(level_info_or.value().downsample_factor);
  }
  return nb::tuple(result);
}

int FastSlide::GetLevelCount() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get level count: slide reader is closed");
  }
  return reader_->GetLevelCount();
}

nb::dict FastSlide::GetProperties() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get properties: slide reader is closed");
  }

  const auto& props = reader_->GetProperties();
  nb::dict result;
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

std::string FastSlide::GetDtype() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get dtype: slide reader is closed");
  }
  return fastslide::GetDataTypeName(reader_->GetDataType());
}

nb::object FastSlide::GetIccProfile() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get ICC profile: slide reader is closed");
  }
  auto profile_or = reader_->GetIccProfile();
  if (!profile_or.ok()) {
    return nb::none();
  }
  const std::vector<uint8_t>& profile = profile_or.value();
  return nb::bytes(reinterpret_cast<const char*>(profile.data()),
                   profile.size());
}

bool FastSlide::GetApplyIcc() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot query ICC state: slide reader is closed");
  }
  return reader_->IsColorTransformEnabled();
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

nb::tuple FastSlide::GetMpp() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get MPP: slide reader is closed");
  }
  const auto& props = reader_->GetProperties();
  return nb::make_tuple(props.mpp[0], props.mpp[1]);
}

nb::tuple FastSlide::GetBounds() const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get bounds: slide reader is closed");
  }
  const auto& props = reader_->GetProperties();
  const auto& bounds = props.bounds;

  auto coordinates = nb::make_tuple(bounds.x, bounds.y);
  auto size = nb::make_tuple(bounds.width, bounds.height);

  return nb::make_tuple(coordinates, size);
}

nb::list FastSlide::GetChannelMetadata() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get channel metadata: slide reader is closed");
  }
  return BuildChannelMetadataList(reader_->GetChannelMetadata());
}

int FastSlide::GetNumChannels() const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot get channel count: slide reader is closed");
  }
  return NumChannelsFromFormat(reader_->GetImageFormat(),
                               reader_->GetChannelMetadata());
}

int FastSlide::GetBestLevelForDownsample(double downsample) const {
  if (is_closed_) {
    throw std::runtime_error("Cannot get best level: slide reader is closed");
  }
  return reader_->GetBestLevelForDownsample(downsample);
}

void FastSlide::SetCache(
    std::shared_ptr<fastslide::runtime::ITileCache> cache) {
  if (is_closed_) {
    throw std::runtime_error("Cannot set cache: slide reader is closed");
  }
  cache_ = cache;
  reader_->SetCache(cache);
}

std::shared_ptr<fastslide::runtime::ITileCache> FastSlide::GetCache() const {
  return cache_;
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

nb::tuple FastSlide::ConvertLevel0ToLevelNative(int64_t x, int64_t y,
                                                int level) const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot convert coordinates: slide reader is closed");
  }

  if (level == 0) {
    return nb::make_tuple(x, y);
  }

  auto level_info_or = reader_->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    throw std::runtime_error("Invalid level " + std::to_string(level) + ": " +
                             std::string(level_info_or.status().message()));
  }

  double downsample = level_info_or.value().downsample_factor;
  uint32_t native_x = static_cast<uint32_t>(x / downsample);
  uint32_t native_y = static_cast<uint32_t>(y / downsample);

  return nb::make_tuple(native_x, native_y);
}

nb::tuple FastSlide::ConvertLevelNativeToLevel0(uint32_t x, uint32_t y,
                                                int level) const {
  if (is_closed_) {
    throw std::runtime_error(
        "Cannot convert coordinates: slide reader is closed");
  }

  if (level == 0) {
    return nb::make_tuple(x, y);
  }

  auto level_info_or = reader_->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    throw std::runtime_error("Invalid level " + std::to_string(level) + ": " +
                             std::string(level_info_or.status().message()));
  }

  double downsample = level_info_or.value().downsample_factor;
  int64_t level0_x = static_cast<int64_t>(x * downsample);
  int64_t level0_y = static_cast<int64_t>(y * downsample);

  return nb::make_tuple(level0_x, level0_y);
}

}  // namespace fastslide::python
