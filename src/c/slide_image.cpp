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

#include "fastslide/c/slide_image.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "fastslide/c/image.h"
#include "fastslide/slide_image.h"
#include "fastslide/slide_reader.h"
#include "internal/debug.h"
#include "internal/error.h"

extern "C" FastSlideImage* fastslide_image_create_from_cpp(
    fastslide::Image image);

// Per-image handle: a stateless view onto the owning reader. It resolves the
// `const fastslide::SlideImage*` on each call via `SlideReader::GetImage`,
// mirroring the Python `SlideImageView`. The handle borrows the reader and
// must not outlive it.
struct FastSlideSlideImage {
  const fastslide::SlideReader* reader;
  int index;
};

// Internal factory used by `fastslide_slide_reader_get_image` (defined in
// slide_reader.cpp, where the FastSlideSlideReader wrapper lives). Not part
// of the public C header.
extern "C" FastSlideSlideImage* fastslide_slide_image_create_internal(
    const fastslide::SlideReader* reader, int index) {
  if (!reader) {
    return nullptr;
  }
  return new FastSlideSlideImage{reader, index};
}

namespace {

using ::fastslide::c::internal::SetLastError;

// Resolve the underlying C++ image, or return nullptr (and set last-error).
const fastslide::SlideImage* ResolveImage(const FastSlideSlideImage* image) {
  if (!image || !image->reader) {
    SetLastError("image is null or closed");
    return nullptr;
  }
  auto image_or = image->reader->GetImage(image->index);
  if (!image_or.ok()) {
    SetLastError(std::string(image_or.status().message()).c_str());
    return nullptr;
  }
  return image_or.value();
}

}  // namespace

void fastslide_slide_image_free(FastSlideSlideImage* image) {
  delete image;
}

int fastslide_slide_image_get_level_count(const FastSlideSlideImage* image) {
  const auto* img = ResolveImage(image);
  if (!img) {
    return -1;
  }
  return img->GetLevelCount();
}

int fastslide_slide_image_get_level_info(const FastSlideSlideImage* image,
                                         int level, FastSlideLevelInfo* info) {
  if (!info) {
    SetLastError("info cannot be null");
    return 0;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return 0;
  }

  auto level_info_or = img->GetLevelInfo(level);
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

int fastslide_slide_image_get_level_dimensions(
    const FastSlideSlideImage* image, int level,
    FastSlideImageDimensions* dimensions) {
  if (!dimensions) {
    SetLastError("dimensions cannot be null");
    return 0;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return 0;
  }

  auto level_info_or = img->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    SetLastError(std::string(level_info_or.status().message()).c_str());
    return 0;
  }

  const auto& level_info = level_info_or.value();
  dimensions->width = level_info.dimensions[0];
  dimensions->height = level_info.dimensions[1];
  return 1;
}

double fastslide_slide_image_get_level_downsample(
    const FastSlideSlideImage* image, int level) {
  const auto* img = ResolveImage(image);
  if (!img) {
    return -1.0;
  }

  auto level_info_or = img->GetLevelInfo(level);
  if (!level_info_or.ok()) {
    SetLastError(std::string(level_info_or.status().message()).c_str());
    return -1.0;
  }
  return level_info_or.value().downsample_factor;
}

int fastslide_slide_image_get_base_dimensions(
    const FastSlideSlideImage* image, FastSlideImageDimensions* dimensions) {
  return fastslide_slide_image_get_level_dimensions(image, 0, dimensions);
}

int fastslide_slide_image_get_tile_size(const FastSlideSlideImage* image,
                                        FastSlideImageDimensions* tile_size) {
  if (!tile_size) {
    SetLastError("tile_size cannot be null");
    return 0;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return 0;
  }

  auto tile_dims = img->GetTileSize();
  tile_size->width = tile_dims[0];
  tile_size->height = tile_dims[1];
  return 1;
}

FastSlideImageFormat fastslide_slide_image_get_image_format(
    const FastSlideSlideImage* image) {
  const auto* img = ResolveImage(image);
  if (!img) {
    return FASTSLIDE_IMAGE_FORMAT_RGB;
  }

  switch (img->GetImageFormat()) {
    case fastslide::ImageFormat::kGray:
      return FASTSLIDE_IMAGE_FORMAT_GRAY;
    case fastslide::ImageFormat::kRGB:
      return FASTSLIDE_IMAGE_FORMAT_RGB;
    case fastslide::ImageFormat::kRGBA:
      return FASTSLIDE_IMAGE_FORMAT_RGBA;
    case fastslide::ImageFormat::kSpectral:
      return FASTSLIDE_IMAGE_FORMAT_SPECTRAL;
    default:
      SetLastError("unknown image format");
      return FASTSLIDE_IMAGE_FORMAT_RGB;
  }
}

FastSlideDataType fastslide_slide_image_get_data_type(
    const FastSlideSlideImage* image) {
  const auto* img = ResolveImage(image);
  if (!img) {
    return FASTSLIDE_DATA_TYPE_UINT8;
  }

  switch (img->GetDataType()) {
    case fastslide::DataType::kUInt8:
      return FASTSLIDE_DATA_TYPE_UINT8;
    case fastslide::DataType::kUInt16:
      return FASTSLIDE_DATA_TYPE_UINT16;
    case fastslide::DataType::kInt16:
      return FASTSLIDE_DATA_TYPE_INT16;
    case fastslide::DataType::kUInt32:
      return FASTSLIDE_DATA_TYPE_UINT32;
    case fastslide::DataType::kInt32:
      return FASTSLIDE_DATA_TYPE_INT32;
    case fastslide::DataType::kFloat32:
      return FASTSLIDE_DATA_TYPE_FLOAT32;
    case fastslide::DataType::kFloat64:
      return FASTSLIDE_DATA_TYPE_FLOAT64;
    default:
      return FASTSLIDE_DATA_TYPE_UINT8;
  }
}

int fastslide_slide_image_get_channel_metadata(
    const FastSlideSlideImage* image, FastSlideChannelMetadata** metadata,
    int* num_channels) {
  if (!metadata || !num_channels) {
    SetLastError("metadata and num_channels cannot be null");
    return 0;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return 0;
  }

  auto channel_metadata = img->GetChannelMetadata();
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

    (*metadata)[i].name = static_cast<char*>(malloc(ch.name.length() + 1));
    if (!(*metadata)[i].name) {
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

    (*metadata)[i].biomarker =
        static_cast<char*>(malloc(ch.biomarker.length() + 1));
    if (!(*metadata)[i].biomarker) {
      for (int j = 0; j <= i; ++j) {
        free((*metadata)[j].name);
        if (j < i) {
          free((*metadata)[j].biomarker);
        }
      }
      free(*metadata);
      *metadata = nullptr;
      SetLastError("failed to allocate memory for biomarker");
      return 0;
    }
    snprintf((*metadata)[i].biomarker, ch.biomarker.length() + 1, "%s",
             ch.biomarker.c_str());

    (*metadata)[i].color.r = ch.color[0];
    (*metadata)[i].color.g = ch.color[1];
    (*metadata)[i].color.b = ch.color[2];
    (*metadata)[i].exposure_time = ch.exposure_time;
    (*metadata)[i].signal_units = ch.signal_units;
  }

  return 1;
}

int fastslide_slide_image_get_properties(const FastSlideSlideImage* image,
                                         FastSlideSlideProperties* properties) {
  if (!properties) {
    SetLastError("properties cannot be null");
    return 0;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return 0;
  }

  const auto& props = img->GetProperties();
  properties->mpp_x = props.mpp[0];
  properties->mpp_y = props.mpp[1];
  properties->objective_magnification = props.objective_magnification;

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

int fastslide_slide_image_get_bounds(const FastSlideSlideImage* image,
                                     FastSlideBounds* bounds) {
  if (!bounds) {
    SetLastError("bounds cannot be null");
    return 0;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return 0;
  }

  const auto& cpp_bounds = img->GetProperties().bounds;
  bounds->x = cpp_bounds.x;
  bounds->y = cpp_bounds.y;
  bounds->width = cpp_bounds.width;
  bounds->height = cpp_bounds.height;
  return 1;
}

FastSlideImage* fastslide_slide_image_read_region(
    const FastSlideSlideImage* image, const FastSlideRegionSpec* region) {
  if (!region) {
    SetLastError("region cannot be null");
    return nullptr;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return nullptr;
  }

  fastslide::RegionSpec cpp_region{
      .top_left = {region->top_left.x, region->top_left.y},
      .size = {region->size.width, region->size.height},
      .level = region->level,
      .plane = {region->z, region->t}};

  auto image_or = img->ReadRegion(cpp_region);
  if (!image_or.ok()) {
    SetLastError(std::string(image_or.status().message()).c_str());
    return nullptr;
  }

  const auto& cpp_image = image_or.value();
  if (cpp_image.GetDimensions()[0] == 0 || cpp_image.GetDimensions()[1] == 0) {
    SetLastError("ReadRegion returned invalid image with zero dimensions");
    return nullptr;
  }
  if (cpp_image.GetData() == nullptr) {
    SetLastError("ReadRegion returned image with null data");
    return nullptr;
  }

  return fastslide_image_create_from_cpp(std::move(image_or.value()));
}

FastSlideImage* fastslide_slide_image_read_region_coords(
    const FastSlideSlideImage* image, uint32_t x, uint32_t y, uint32_t width,
    uint32_t height, int level, uint32_t z, uint32_t t) {
  if (width == 0 || height == 0) {
    SetLastError("Invalid region dimensions: width and height must be > 0");
    return nullptr;
  }
  if (level < 0) {
    SetLastError("Invalid level: must be >= 0");
    return nullptr;
  }

  FastSlideRegionSpec region = {.top_left = {x, y},
                                .size = {width, height},
                                .level = level,
                                .z = z,
                                .t = t};
  return fastslide_slide_image_read_region(image, &region);
}

int fastslide_slide_image_get_stack_info(const FastSlideSlideImage* image,
                                         FastSlideStackInfo* info) {
  if (!info) {
    SetLastError("info cannot be null");
    return 0;
  }
  const auto* img = ResolveImage(image);
  if (!img) {
    return 0;
  }
  const fastslide::StackInfo stack = img->GetStackInfo();
  info->z_count = stack.z_count;
  info->t_count = stack.t_count;
  info->has_z_spacing = stack.z_spacing_um.has_value() ? 1 : 0;
  info->z_spacing_um = stack.z_spacing_um.value_or(0.0);
  info->has_t_interval = stack.t_interval_s.has_value() ? 1 : 0;
  info->t_interval_s = stack.t_interval_s.value_or(0.0);
  return 1;
}
