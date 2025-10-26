// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include "fastslide/c/slide_reader.h"

#include <cstdio>   // For printf
#include <cstdlib>  // For getenv
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>  // For std::vector

#include "fastslide/c/image.h"
#include "fastslide/slide_reader.h"

// Forward declarations for external functions
extern "C" void fastslide_set_last_error(const char* message);
extern "C" FastSlideImage* fastslide_image_create_from_cpp(
    fastslide::Image image);

// Check if debug logging is enabled via environment variable
static bool IsDebugEnabled() {
  static bool checked = false;
  static bool debug_enabled = false;

  if (!checked) {
    const char* debug_env = std::getenv("FASTSLIDE_DEBUG");
    debug_enabled = (debug_env != nullptr && std::strcmp(debug_env, "1") == 0);
    checked = true;
  }

  return debug_enabled;
}

#define FASTSLIDE_DEBUG_PRINT(...) \
  do {                             \
    if (IsDebugEnabled()) {        \
      printf(__VA_ARGS__);         \
    }                              \
  } while (0)

#define FASTSLIDE_ERROR_PRINT(...) \
  do {                             \
    printf(__VA_ARGS__);           \
  } while (0)

// Wrapper struct to hold the C++ SlideReader
struct FastSlideSlideReader {
  std::unique_ptr<fastslide::SlideReader> reader;
  std::string format_name;

  explicit FastSlideSlideReader(std::unique_ptr<fastslide::SlideReader> r)
      : reader(std::move(r)) {
    if (reader) {
      format_name = reader->GetFormatName();
    }
  }
};

// Helper function to create C wrapper from C++ reader
extern "C" FastSlideSlideReader* fastslide_slide_reader_create_from_cpp(
    std::unique_ptr<fastslide::SlideReader> reader) {
  if (!reader) {
    return nullptr;
  }
  return new FastSlideSlideReader(std::move(reader));
}

namespace {

void SetLastError(const char* message) {
  fastslide_set_last_error(message);
}

// Helper function to convert C++ property type to C enum
FastSlidePropertyType PropertyTypeToCEnum(
    const fastslide::Metadata::Value& value) {
  if (std::holds_alternative<std::string>(value)) {
    return FASTSLIDE_PROPERTY_TYPE_STRING;
  } else if (std::holds_alternative<size_t>(value)) {
    return FASTSLIDE_PROPERTY_TYPE_SIZE_T;
  } else if (std::holds_alternative<double>(value)) {
    return FASTSLIDE_PROPERTY_TYPE_DOUBLE;
  }
  return FASTSLIDE_PROPERTY_TYPE_STRING;  // Default fallback
}

}  // namespace

// Basic slide properties

int fastslide_slide_reader_get_level_count(const FastSlideSlideReader* reader) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return -1;
  }

  return reader->reader->GetLevelCount();
}

int fastslide_slide_reader_get_level_info(const FastSlideSlideReader* reader,
                                          int level, FastSlideLevelInfo* info) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!info) {
    SetLastError("info cannot be null");
    return 0;
  }

  auto level_info_or = reader->reader->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    SetLastError(std::string(level_info_or.status().message()).c_str());
    return 0;
  }

  const auto& level_info = level_info_or.value();
  info->dimensions.width = level_info.dimensions[0];
  info->dimensions.height = level_info.dimensions[1];
  info->downsample_factor = level_info.downsample_factor;

  return 1;
}

int fastslide_slide_reader_get_level_dimensions(
    const FastSlideSlideReader* reader, int level,
    FastSlideImageDimensions* dimensions) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!dimensions) {
    SetLastError("dimensions cannot be null");
    return 0;
  }

  auto level_info_or = reader->reader->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    SetLastError(std::string(level_info_or.status().message()).c_str());
    return 0;
  }

  const auto& level_info = level_info_or.value();
  dimensions->width = level_info.dimensions[0];
  dimensions->height = level_info.dimensions[1];

  return 1;
}

int fastslide_slide_reader_get_base_dimensions(
    const FastSlideSlideReader* reader, FastSlideImageDimensions* dimensions) {
  return fastslide_slide_reader_get_level_dimensions(reader, 0, dimensions);
}

double fastslide_slide_reader_get_level_downsample(
    const FastSlideSlideReader* reader, int level) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return -1.0;
  }

  auto level_info_or = reader->reader->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    SetLastError(std::string(level_info_or.status().message()).c_str());
    return -1.0;
  }

  return level_info_or.value().downsample_factor;
}

int fastslide_slide_reader_get_best_level_for_downsample(
    const FastSlideSlideReader* reader, double downsample) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return -1;
  }

  return reader->reader->GetBestLevelForDownsample(downsample);
}

// Slide properties

int fastslide_slide_reader_get_properties(
    const FastSlideSlideReader* reader, FastSlideSlideProperties* properties) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!properties) {
    SetLastError("properties cannot be null");
    return 0;
  }

  const auto& props = reader->reader->GetProperties();

  properties->mpp_x = props.mpp[0];
  properties->mpp_y = props.mpp[1];
  properties->objective_magnification = props.objective_magnification;

  // Allocate and copy strings
  properties->objective_name =
      static_cast<char*>(malloc(props.objective_name.length() + 1));
  if (properties->objective_name) {
    snprintf(properties->objective_name, props.objective_name.length() + 1,
             "%s", props.objective_name.c_str());
  }

  properties->scanner_model =
      static_cast<char*>(malloc(props.scanner_model.length() + 1));
  if (properties->scanner_model) {
    snprintf(properties->scanner_model, props.scanner_model.length() + 1, "%s",
             props.scanner_model.c_str());
  }

  if (props.scan_date) {
    properties->scan_date =
        static_cast<char*>(malloc(props.scan_date->length() + 1));
    if (properties->scan_date) {
      snprintf(properties->scan_date, props.scan_date->length() + 1, "%s",
               props.scan_date->c_str());
    }
  } else {
    properties->scan_date = nullptr;
  }

  return 1;
}

void fastslide_slide_reader_free_properties(
    FastSlideSlideProperties* properties) {
  if (!properties) {
    return;
  }

  free(properties->objective_name);
  free(properties->scanner_model);
  free(properties->scan_date);

  // Clear pointers to avoid double-free
  properties->objective_name = nullptr;
  properties->scanner_model = nullptr;
  properties->scan_date = nullptr;
}

int fastslide_slide_reader_get_bounds(const FastSlideSlideReader* reader,
                                      FastSlideBounds* bounds) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!bounds) {
    SetLastError("bounds cannot be null");
    return 0;
  }

  const auto& props = reader->reader->GetProperties();
  const auto& cpp_bounds = props.bounds;

  bounds->x = cpp_bounds.x;
  bounds->y = cpp_bounds.y;
  bounds->width = cpp_bounds.width;
  bounds->height = cpp_bounds.height;

  return 1;
}

const char* fastslide_slide_reader_get_format_name(
    const FastSlideSlideReader* reader) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return nullptr;
  }

  return reader->format_name.c_str();
}

FastSlideImageFormat fastslide_slide_reader_get_image_format(
    const FastSlideSlideReader* reader) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return FASTSLIDE_IMAGE_FORMAT_RGB;  // Default fallback
  }

  auto format = reader->reader->GetImageFormat();
  switch (format) {
    case fastslide::ImageFormat::kRGB:
      return FASTSLIDE_IMAGE_FORMAT_RGB;
    case fastslide::ImageFormat::kSpectral:
      return FASTSLIDE_IMAGE_FORMAT_SPECTRAL;
    default:
      SetLastError("unknown image format");
      return FASTSLIDE_IMAGE_FORMAT_RGB;  // Default fallback
  }
}

int fastslide_slide_reader_get_tile_size(const FastSlideSlideReader* reader,
                                         FastSlideImageDimensions* tile_size) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!tile_size) {
    SetLastError("tile_size cannot be null");
    return 0;
  }

  auto tile_dims = reader->reader->GetTileSize();
  tile_size->width = tile_dims[0];
  tile_size->height = tile_dims[1];

  return 1;
}

// Channel metadata

int fastslide_slide_reader_get_channel_metadata(
    const FastSlideSlideReader* reader, FastSlideChannelMetadata** metadata,
    int* num_channels) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!metadata || !num_channels) {
    SetLastError("metadata and num_channels cannot be null");
    return 0;
  }

  auto channel_metadata = reader->reader->GetChannelMetadata();
  *num_channels = static_cast<int>(channel_metadata.size());

  if (*num_channels == 0) {
    *metadata = nullptr;
    return 1;
  }

  *metadata = static_cast<FastSlideChannelMetadata*>(
      malloc(*num_channels * sizeof(FastSlideChannelMetadata)));
  if (!*metadata) {
    SetLastError("failed to allocate memory for channel metadata array");
    return 0;
  }

  for (int i = 0; i < *num_channels; ++i) {
    const auto& ch = channel_metadata[i];

    // Allocate and copy name
    (*metadata)[i].name = static_cast<char*>(malloc(ch.name.length() + 1));
    if (!(*metadata)[i].name) {
      // Cleanup on failure
      for (int j = 0; j < i; ++j) {
        free((*metadata)[j].name);
        free((*metadata)[j].biomarker);
      }
      free(*metadata);
      *metadata = nullptr;
      SetLastError("failed to allocate memory for channel name");
      return 0;
    }
    snprintf((*metadata)[i].name, ch.name.length() + 1, "%s", ch.name.c_str());

    // Allocate and copy biomarker
    (*metadata)[i].biomarker =
        static_cast<char*>(malloc(ch.biomarker.length() + 1));
    if (!(*metadata)[i].biomarker) {
      // Cleanup on failure
      for (int j = 0; j <= i; ++j) {
        free((*metadata)[j].name);
        if (j < i)
          free((*metadata)[j].biomarker);
      }
      free(*metadata);
      *metadata = nullptr;
      SetLastError("failed to allocate memory for biomarker");
      return 0;
    }
    snprintf((*metadata)[i].biomarker, ch.biomarker.length() + 1, "%s",
             ch.biomarker.c_str());

    // Copy color
    (*metadata)[i].color.r = ch.color[0];
    (*metadata)[i].color.g = ch.color[1];
    (*metadata)[i].color.b = ch.color[2];

    // Copy exposure time and signal units
    (*metadata)[i].exposure_time = ch.exposure_time;
    (*metadata)[i].signal_units = ch.signal_units;
  }

  return 1;
}

void fastslide_slide_reader_free_channel_metadata(
    FastSlideChannelMetadata* metadata, int num_channels) {
  if (!metadata || num_channels <= 0) {
    return;
  }

  for (int i = 0; i < num_channels; ++i) {
    free(metadata[i].name);
    free(metadata[i].biomarker);
  }
  free(metadata);
}

// Channel visibility controls

int fastslide_slide_reader_set_visible_channels(FastSlideSlideReader* reader,
                                                const size_t* channel_indices,
                                                int num_channels) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (num_channels < 0) {
    SetLastError("num_channels cannot be negative");
    return 0;
  }

  if (num_channels > 0 && !channel_indices) {
    SetLastError("channel_indices cannot be null when num_channels > 0");
    return 0;
  }

  std::vector<size_t> channels;
  if (num_channels > 0) {
    channels.assign(channel_indices, channel_indices + num_channels);
  }

  reader->reader->SetVisibleChannels(channels);
  return 1;
}

int fastslide_slide_reader_get_visible_channels(
    const FastSlideSlideReader* reader, size_t** channel_indices,
    int* num_channels) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!channel_indices || !num_channels) {
    SetLastError("channel_indices and num_channels cannot be null");
    return 0;
  }

  const auto& visible_channels = reader->reader->GetVisibleChannels();
  *num_channels = static_cast<int>(visible_channels.size());

  if (*num_channels == 0) {
    *channel_indices = nullptr;
    return 1;
  }

  *channel_indices =
      static_cast<size_t*>(malloc(*num_channels * sizeof(size_t)));
  if (!*channel_indices) {
    SetLastError("failed to allocate memory for channel indices");
    return 0;
  }

  for (int i = 0; i < *num_channels; ++i) {
    (*channel_indices)[i] = visible_channels[i];
  }

  return 1;
}

int fastslide_slide_reader_show_all_channels(FastSlideSlideReader* reader) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  reader->reader->ShowAllChannels();
  return 1;
}

void fastslide_slide_reader_free_visible_channels(size_t* channel_indices) {
  free(channel_indices);
}

// Region reading

FastSlideImage* fastslide_slide_reader_read_region(
    const FastSlideSlideReader* reader, const FastSlideRegionSpec* region) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    FASTSLIDE_ERROR_PRINT("[FastSlide C] ERROR: Reader is null or closed\n");
    return nullptr;
  }

  if (!region) {
    SetLastError("region cannot be null");
    FASTSLIDE_ERROR_PRINT("[FastSlide C] ERROR: Region is null\n");
    return nullptr;
  }

  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: Reading region at (%u,%u) size %ux%u level %d\n",
      region->top_left.x, region->top_left.y, region->size.width,
      region->size.height, region->level);

  fastslide::RegionSpec cpp_region{
      .top_left = {region->top_left.x, region->top_left.y},
      .size = {region->size.width, region->size.height},
      .level = region->level};

  FASTSLIDE_DEBUG_PRINT("[FastSlide C] DEBUG: Calling C++ ReadRegion\n");
  auto image_or = reader->reader->ReadRegion(cpp_region);

  if (!image_or.ok()) {
    std::string error_msg = std::string(image_or.status().message());
    FASTSLIDE_ERROR_PRINT("[FastSlide C] ERROR: C++ ReadRegion failed: %s\n",
                          error_msg.c_str());
    SetLastError(error_msg.c_str());
    return nullptr;
  }

  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: C++ ReadRegion succeeded, creating C image "
      "wrapper\n");

  // Check if the image is valid before creating wrapper
  const auto& cpp_image = image_or.value();
  if (cpp_image.GetDimensions()[0] == 0 || cpp_image.GetDimensions()[1] == 0) {
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: C++ ReadRegion returned invalid image with zero "
        "dimensions\n");
    SetLastError("ReadRegion returned invalid image with zero dimensions");
    return nullptr;
  }

  if (cpp_image.GetData() == nullptr) {
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: C++ ReadRegion returned image with null data\n");
    SetLastError("ReadRegion returned image with null data");
    return nullptr;
  }

  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: Image is valid, dimensions: %ux%u, data ptr: %p\n",
      cpp_image.GetDimensions()[0], cpp_image.GetDimensions()[1],
      cpp_image.GetData());

  FastSlideImage* result =
      fastslide_image_create_from_cpp(std::move(image_or.value()));
  if (!result) {
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: Failed to create C image wrapper\n");
    SetLastError("Failed to create C image wrapper");
    return nullptr;
  }

  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: Successfully created C image wrapper: %p\n",
      result);
  return result;
}

FastSlideImage* fastslide_slide_reader_read_region_coords(
    const FastSlideSlideReader* reader, uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, int level) {

  FASTSLIDE_DEBUG_PRINT(
      "[FastSlide C] DEBUG: read_region_coords called with x=%u, y=%u, w=%u, "
      "h=%u, level=%d\n",
      x, y, width, height, level);

  // Validate inputs
  if (width == 0 || height == 0) {
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: Invalid dimensions: width=%u, height=%u\n", width,
        height);
    SetLastError("Invalid region dimensions: width and height must be > 0");
    return nullptr;
  }

  if (level < 0) {
    FASTSLIDE_ERROR_PRINT(
        "[FastSlide C] ERROR: Invalid level: %d (must be >= 0)\n", level);
    SetLastError("Invalid level: must be >= 0");
    return nullptr;
  }

  FastSlideRegionSpec region = {
      .top_left = {x, y}, .size = {width, height}, .level = level};

  return fastslide_slide_reader_read_region(reader, &region);
}

// Associated images

int fastslide_slide_reader_get_associated_image_names(
    const FastSlideSlideReader* reader, char*** names, int* num_names) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!names || !num_names) {
    SetLastError("names and num_names cannot be null");
    return 0;
  }

  auto name_list = reader->reader->GetAssociatedImageNames();
  *num_names = static_cast<int>(name_list.size());

  if (*num_names == 0) {
    *names = nullptr;
    return 1;
  }

  *names = static_cast<char**>(malloc(*num_names * sizeof(char*)));
  if (!*names) {
    SetLastError("failed to allocate memory for names array");
    return 0;
  }

  for (int i = 0; i < *num_names; ++i) {
    (*names)[i] = static_cast<char*>(malloc(name_list[i].length() + 1));
    if (!(*names)[i]) {
      // Cleanup on failure
      for (int j = 0; j < i; ++j) {
        free((*names)[j]);
      }
      free(*names);
      *names = nullptr;
      SetLastError("failed to allocate memory for name string");
      return 0;
    }
    snprintf((*names)[i], name_list[i].length() + 1, "%s",
             name_list[i].c_str());
  }

  return 1;
}

void fastslide_slide_reader_free_associated_image_names(char** names,
                                                        int num_names) {
  if (!names || num_names <= 0) {
    return;
  }

  for (int i = 0; i < num_names; ++i) {
    free(names[i]);
  }
  free(names);
}

int fastslide_slide_reader_get_associated_image_dimensions(
    const FastSlideSlideReader* reader, const char* name,
    FastSlideImageDimensions* dimensions) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!name || !dimensions) {
    SetLastError("name and dimensions cannot be null");
    return 0;
  }

  auto dims_or = reader->reader->GetAssociatedImageDimensions(name);
  if (!dims_or.ok()) {
    SetLastError(std::string(dims_or.status().message()).c_str());
    return 0;
  }

  const auto& dims = dims_or.value();
  dimensions->width = dims[0];
  dimensions->height = dims[1];

  return 1;
}

FastSlideImage* fastslide_slide_reader_read_associated_image(
    const FastSlideSlideReader* reader, const char* name) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return nullptr;
  }

  if (!name) {
    SetLastError("name cannot be null");
    return nullptr;
  }

  auto image_or = reader->reader->ReadAssociatedImage(name);
  if (!image_or.ok()) {
    SetLastError(std::string(image_or.status().message()).c_str());
    return nullptr;
  }

  return fastslide_image_create_from_cpp(std::move(image_or.value()));
}

// Metadata access

int fastslide_slide_reader_get_metadata_keys(const FastSlideSlideReader* reader,
                                             char*** keys, int* num_keys) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!keys || !num_keys) {
    SetLastError("keys and num_keys cannot be null");
    return 0;
  }

  auto metadata = reader->reader->GetMetadata();
  *num_keys = static_cast<int>(metadata.size());

  if (*num_keys == 0) {
    *keys = nullptr;
    return 1;
  }

  *keys = static_cast<char**>(malloc(*num_keys * sizeof(char*)));
  if (!*keys) {
    SetLastError("failed to allocate memory for keys array");
    return 0;
  }

  int i = 0;
  for (const auto& [key, value] : metadata) {
    (*keys)[i] = static_cast<char*>(malloc(key.length() + 1));
    if (!(*keys)[i]) {
      // Cleanup on failure
      for (int j = 0; j < i; ++j) {
        free((*keys)[j]);
      }
      free(*keys);
      *keys = nullptr;
      SetLastError("failed to allocate memory for key string");
      return 0;
    }
    snprintf((*keys)[i], key.length() + 1, "%s", key.c_str());
    ++i;
  }

  return 1;
}

void fastslide_slide_reader_free_metadata_keys(char** keys, int num_keys) {
  if (!keys || num_keys <= 0) {
    return;
  }

  for (int i = 0; i < num_keys; ++i) {
    free(keys[i]);
  }
  free(keys);
}

int fastslide_slide_reader_get_metadata_value(
    const FastSlideSlideReader* reader, const char* key,
    FastSlidePropertyValue* value) {
  if (!reader || !reader->reader) {
    SetLastError("reader is null or closed");
    return 0;
  }

  if (!key || !value) {
    SetLastError("key and value cannot be null");
    return 0;
  }

  auto metadata = reader->reader->GetMetadata();
  auto it = metadata.find(key);
  if (it == metadata.end()) {
    SetLastError("property key not found");
    return 0;
  }

  static thread_local std::string cached_string_value;

  const auto& variant_value = it->second;

  if (std::holds_alternative<std::string>(variant_value)) {
    value->type = FASTSLIDE_PROPERTY_TYPE_STRING;
    cached_string_value = std::get<std::string>(variant_value);
    value->string_value = cached_string_value.c_str();
  } else if (std::holds_alternative<size_t>(variant_value)) {
    value->type = FASTSLIDE_PROPERTY_TYPE_SIZE_T;
    value->size_t_value = std::get<size_t>(variant_value);
  } else if (std::holds_alternative<double>(variant_value)) {
    value->type = FASTSLIDE_PROPERTY_TYPE_DOUBLE;
    value->double_value = std::get<double>(variant_value);
  } else {
    SetLastError("unknown property value type");
    return 0;
  }

  return 1;
}

FastSlidePropertyType fastslide_slide_reader_get_metadata_type(
    const FastSlideSlideReader* reader, const char* key) {
  if (!reader || !reader->reader || !key) {
    SetLastError("reader is null or closed, or key is null");
    return FASTSLIDE_PROPERTY_TYPE_STRING;  // Default fallback
  }

  auto metadata = reader->reader->GetMetadata();
  auto it = metadata.find(key);
  if (it == metadata.end()) {
    SetLastError("property key not found");
    return FASTSLIDE_PROPERTY_TYPE_STRING;  // Default fallback
  }

  return PropertyTypeToCEnum(it->second);
}

// Utility functions

int fastslide_slide_reader_is_region_valid(const FastSlideSlideReader* reader,
                                           const FastSlideRegionSpec* region) {
  if (!reader || !reader->reader || !region) {
    return 0;
  }

  return (region->size.width > 0 && region->size.height > 0 &&
          region->level >= 0)
             ? 1
             : 0;
}

int fastslide_slide_reader_clamp_region(const FastSlideSlideReader* reader,
                                        const FastSlideRegionSpec* region,
                                        FastSlideRegionSpec* clamped_region) {
  if (!reader || !reader->reader || !region || !clamped_region) {
    SetLastError("reader, region, or clamped_region cannot be null");
    return 0;
  }

  // Get level dimensions
  FastSlideImageDimensions level_dims;
  if (!fastslide_slide_reader_get_level_dimensions(reader, region->level,
                                                   &level_dims)) {
    return 0;
  }

  // Clamp coordinates and size
  uint32_t max_x = (region->top_left.x < level_dims.width)
                       ? region->top_left.x
                       : level_dims.width - 1;
  uint32_t max_y = (region->top_left.y < level_dims.height)
                       ? region->top_left.y
                       : level_dims.height - 1;

  uint32_t max_width = level_dims.width - max_x;
  uint32_t max_height = level_dims.height - max_y;

  clamped_region->top_left.x = max_x;
  clamped_region->top_left.y = max_y;
  clamped_region->size.width =
      (region->size.width < max_width) ? region->size.width : max_width;
  clamped_region->size.height =
      (region->size.height < max_height) ? region->size.height : max_height;
  clamped_region->level = region->level;

  return 1;
}

// Memory management

void fastslide_slide_reader_free(FastSlideSlideReader* reader) {
  delete reader;
}
