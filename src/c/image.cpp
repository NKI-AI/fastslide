// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include "fastslide/c/image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fastslide/image.h"
#include "internal/error.h"

// Wrapper struct to hold the C++ Image
struct FastSlideImage {
  fastslide::Image image;

  explicit FastSlideImage(fastslide::Image img) : image(std::move(img)) {}
};

namespace {

// Helper function to convert C++ ImageFormat to C enum
FastSlideImageFormat ImageFormatToCEnum(fastslide::ImageFormat format) {
  switch (format) {
    case fastslide::ImageFormat::kGray:
      return FASTSLIDE_IMAGE_FORMAT_GRAY;
    case fastslide::ImageFormat::kRGB:
      return FASTSLIDE_IMAGE_FORMAT_RGB;
    case fastslide::ImageFormat::kRGBA:
      return FASTSLIDE_IMAGE_FORMAT_RGBA;
    case fastslide::ImageFormat::kSpectral:
      return FASTSLIDE_IMAGE_FORMAT_SPECTRAL;
  }
  return FASTSLIDE_IMAGE_FORMAT_RGB;  // Default fallback
}

// Helper function to convert C++ DataType to C enum
FastSlideDataType DataTypeToCEnum(fastslide::DataType data_type) {
  switch (data_type) {
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
  }
  return FASTSLIDE_DATA_TYPE_UINT8;  // Default fallback
}

// Helper function to convert C enum to C++ DataType
fastslide::DataType CEnumToDataType(FastSlideDataType data_type) {
  switch (data_type) {
    case FASTSLIDE_DATA_TYPE_UINT8:
      return fastslide::DataType::kUInt8;
    case FASTSLIDE_DATA_TYPE_UINT16:
      return fastslide::DataType::kUInt16;
    case FASTSLIDE_DATA_TYPE_INT16:
      return fastslide::DataType::kInt16;
    case FASTSLIDE_DATA_TYPE_UINT32:
      return fastslide::DataType::kUInt32;
    case FASTSLIDE_DATA_TYPE_INT32:
      return fastslide::DataType::kInt32;
    case FASTSLIDE_DATA_TYPE_FLOAT32:
      return fastslide::DataType::kFloat32;
    case FASTSLIDE_DATA_TYPE_FLOAT64:
      return fastslide::DataType::kFloat64;
  }
  return fastslide::DataType::kUInt8;  // Default fallback
}

// Helper function to convert C++ PlanarConfig to C enum
FastSlidePlanarConfig PlanarConfigToCEnum(fastslide::PlanarConfig config) {
  switch (config) {
    case fastslide::PlanarConfig::kContiguous:
      return FASTSLIDE_PLANAR_CONFIG_CONTIG;
    case fastslide::PlanarConfig::kSeparate:
      return FASTSLIDE_PLANAR_CONFIG_SEPARATE;
  }
  return FASTSLIDE_PLANAR_CONFIG_CONTIG;  // Default fallback
}

// Helper function to populate C image info from C++ image
void PopulateImageInfo(const fastslide::Image& image,
                       FastSlideImageInfo* info) {
  info->format = ImageFormatToCEnum(image.GetFormat());
  info->data_type = DataTypeToCEnum(image.GetDataType());
  info->planar_config = PlanarConfigToCEnum(image.GetPlanarConfig());
  info->width = image.GetWidth();
  info->height = image.GetHeight();
  info->channels = image.GetChannels();
  info->bytes_per_sample = image.GetBytesPerSample();
  info->data_size = image.SizeBytes();
}

using ::fastslide::c::internal::SetLastError;

// Wrap a freshly-constructed C++ Image in a heap-allocated FastSlideImage.
// All `fastslide_image_create_*` factories funnel through here. The C++
// `fastslide::Image` API is exception-free (precondition violations abort
// via AIFOCORE_CHECK), so no try/catch boundary is required.
template <typename Construct>
FastSlideImage* MakeImage(Construct construct) {
  return new FastSlideImage(construct());
}

inline fastslide::ImageDimensions ToCppDims(FastSlideImageDimensions dims) {
  return {dims.width, dims.height};
}

}  // namespace

// Factory functions

FastSlideImage* fastslide_image_create_rgb(FastSlideImageDimensions dimensions,
                                           FastSlideDataType data_type) {
  return MakeImage([&] {
    return fastslide::Image(ToCppDims(dimensions), fastslide::ImageFormat::kRGB,
                            CEnumToDataType(data_type));
  });
}

FastSlideImage* fastslide_image_create_rgba(FastSlideImageDimensions dimensions,
                                            FastSlideDataType data_type) {
  return MakeImage([&] {
    return fastslide::Image(ToCppDims(dimensions),
                            fastslide::ImageFormat::kRGBA,
                            CEnumToDataType(data_type));
  });
}

FastSlideImage* fastslide_image_create_grayscale(
    FastSlideImageDimensions dimensions, FastSlideDataType data_type) {
  return MakeImage([&] {
    return fastslide::Image(ToCppDims(dimensions),
                            fastslide::ImageFormat::kGray,
                            CEnumToDataType(data_type));
  });
}

FastSlideImage* fastslide_image_create_spectral(
    FastSlideImageDimensions dimensions, uint32_t channels,
    FastSlideDataType data_type) {
  return MakeImage([&] {
    return fastslide::Image(ToCppDims(dimensions), channels,
                            CEnumToDataType(data_type));
  });
}

// Property accessors

int fastslide_image_get_info(const FastSlideImage* image,
                             FastSlideImageInfo* info) {
  if (!image || !info) {
    SetLastError("image and info cannot be null");
    return 0;
  }

  PopulateImageInfo(image->image, info);
  return 1;
}

int fastslide_image_get_dimensions(const FastSlideImage* image,
                                   FastSlideImageDimensions* dimensions) {
  if (!image || !dimensions) {
    SetLastError("image and dimensions cannot be null");
    return 0;
  }

  dimensions->width = image->image.GetWidth();
  dimensions->height = image->image.GetHeight();
  return 1;
}

uint32_t fastslide_image_get_width(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 0;
  }
  return image->image.GetWidth();
}

uint32_t fastslide_image_get_height(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 0;
  }
  return image->image.GetHeight();
}

uint32_t fastslide_image_get_channels(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 0;
  }
  return image->image.GetChannels();
}

FastSlideImageFormat fastslide_image_get_format(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return FASTSLIDE_IMAGE_FORMAT_RGB;  // Default fallback
  }
  return ImageFormatToCEnum(image->image.GetFormat());
}

FastSlideDataType fastslide_image_get_data_type(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return FASTSLIDE_DATA_TYPE_UINT8;  // Default fallback
  }
  return DataTypeToCEnum(image->image.GetDataType());
}

FastSlidePlanarConfig fastslide_image_get_planar_config(
    const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return FASTSLIDE_PLANAR_CONFIG_CONTIG;  // Default fallback
  }
  return PlanarConfigToCEnum(image->image.GetPlanarConfig());
}

size_t fastslide_image_get_bytes_per_sample(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 0;
  }
  return image->image.GetBytesPerSample();
}

int fastslide_image_is_empty(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 1;  // Consider null image as empty
  }
  return image->image.Empty() ? 1 : 0;
}

size_t fastslide_image_get_size_bytes(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 0;
  }
  return image->image.SizeBytes();
}

size_t fastslide_image_get_pixel_count(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 0;
  }
  return image->image.GetPixelCount();
}

// Data access

const uint8_t* fastslide_image_get_data(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  return image->image.GetData();
}

uint8_t* fastslide_image_get_data_mutable(FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  return image->image.GetData();
}

int fastslide_image_copy_data(const FastSlideImage* image, uint8_t* buffer,
                              size_t buffer_size) {
  if (!image || !buffer) {
    SetLastError("image and buffer cannot be null");
    return 0;
  }

  size_t data_size = image->image.SizeBytes();
  if (buffer_size < data_size) {
    SetLastError("buffer size is too small");
    return 0;
  }

  std::memcpy(buffer, image->image.GetData(), data_size);
  return 1;
}

// Conversion methods

namespace {

// Wrap a `unique_ptr<Image>`-returning conversion with consistent C-API
// semantics: validate the input handle, run the conversion, surface a typed
// error message if the C++ side returned nullptr.
template <typename Convert>
FastSlideImage* WrapConversion(const FastSlideImage* image,
                               const char* failure_message, Convert convert) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  auto result = convert(image->image);
  if (!result) {
    SetLastError(failure_message);
    return nullptr;
  }
  return new FastSlideImage(std::move(*result));
}

}  // namespace

FastSlideImage* fastslide_image_to_rgb(const FastSlideImage* image) {
  return WrapConversion(
      image, "failed to convert image to RGB",
      [](const fastslide::Image& img) { return img.ToRGB(); });
}

FastSlideImage* fastslide_image_to_grayscale(const FastSlideImage* image) {
  return WrapConversion(
      image, "failed to convert image to grayscale",
      [](const fastslide::Image& img) { return img.ToGrayscale(); });
}

FastSlideImage* fastslide_image_to_planar(const FastSlideImage* image) {
  return WrapConversion(
      image, "failed to convert image to planar layout",
      [](const fastslide::Image& img) { return img.ToPlanar(); });
}

FastSlideImage* fastslide_image_to_interleaved(const FastSlideImage* image) {
  return WrapConversion(
      image, "failed to convert image to interleaved layout",
      [](const fastslide::Image& img) { return img.ToInterleaved(); });
}

FastSlideImage* fastslide_image_extract_channels(
    const FastSlideImage* image, const uint32_t* channel_indices,
    uint32_t num_channels) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  if (num_channels > 0 && channel_indices == nullptr) {
    SetLastError("channel_indices cannot be null when num_channels > 0");
    return nullptr;
  }

  const std::vector<uint32_t> indices(channel_indices,
                                      channel_indices + num_channels);
  auto extracted_image = image->image.ExtractChannels(indices);
  if (!extracted_image) {
    SetLastError("failed to extract channels");
    return nullptr;
  }
  return new FastSlideImage(std::move(*extracted_image));
}

FastSlideImage* fastslide_image_clone(const FastSlideImage* image) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  return new FastSlideImage(std::move(*image->image.Clone()));
}

int fastslide_image_get_description(const FastSlideImage* image, char* buffer,
                                    size_t buffer_size) {
  if (image == nullptr || buffer == nullptr) {
    SetLastError("image and buffer cannot be null");
    return -1;
  }

  const std::string description = image->image.GetDescription();
  const size_t desc_len = description.length();
  if (buffer_size <= desc_len) {
    SetLastError("buffer size is too small");
    return -1;
  }

  std::strncpy(buffer, description.c_str(), buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
  return static_cast<int>(desc_len);
}

// Helper functions for other modules

extern "C" FastSlideImage* fastslide_image_create_from_cpp(
    fastslide::Image image) {
  if (image.GetDimensions()[0] == 0 || image.GetDimensions()[1] == 0) {
    printf(
        "[FastSlide C] ERROR: Cannot create wrapper for image with zero "
        "dimensions\n");
    SetLastError("Cannot create wrapper for image with zero dimensions");
    return nullptr;
  }

  // Uninitialized images are allowed (data is null until first paste).
  if (image.IsInitialized() && image.GetData() == nullptr) {
    printf(
        "[FastSlide C] ERROR: Cannot create wrapper for initialized image "
        "with null data\n");
    SetLastError("Cannot create wrapper for initialized image with null data");
    return nullptr;
  }

  return new FastSlideImage(std::move(image));
}

const fastslide::Image& fastslide_image_get_cpp_image(
    const FastSlideImage* image) {
  AIFOCORE_CHECK(image != nullptr, "image cannot be null");
  return image->image;
}

fastslide::Image& fastslide_image_get_cpp_image_mutable(FastSlideImage* image) {
  AIFOCORE_CHECK(image != nullptr, "image cannot be null");
  return image->image;
}

// Extended factory functions

extern "C" FastSlideImage* fastslide_image_create_blank(
    FastSlideImageDimensions dimensions) {
  return new FastSlideImage(fastslide::Image(ToCppDims(dimensions)));
}

extern "C" FastSlideImage* fastslide_image_create_solid_color(
    FastSlideImageDimensions dimensions, FastSlideDataType data_type,
    uint32_t red, uint32_t green, uint32_t blue) {
  const fastslide::DataType cpp_dtype = CEnumToDataType(data_type);
  fastslide::Image rgb_image(ToCppDims(dimensions),
                             fastslide::ImageFormat::kRGB, cpp_dtype);

  switch (cpp_dtype) {
    case fastslide::DataType::kUInt8:
      rgb_image.FillWithColor(static_cast<uint8_t>(red),
                              static_cast<uint8_t>(green),
                              static_cast<uint8_t>(blue));
      break;
    case fastslide::DataType::kUInt16:
      rgb_image.FillWithColor(static_cast<uint16_t>(red),
                              static_cast<uint16_t>(green),
                              static_cast<uint16_t>(blue));
      break;
    case fastslide::DataType::kInt16:
      rgb_image.FillWithColor(static_cast<int16_t>(red),
                              static_cast<int16_t>(green),
                              static_cast<int16_t>(blue));
      break;
    case fastslide::DataType::kUInt32:
      rgb_image.FillWithColor(static_cast<uint32_t>(red),
                              static_cast<uint32_t>(green),
                              static_cast<uint32_t>(blue));
      break;
    case fastslide::DataType::kInt32:
      rgb_image.FillWithColor(static_cast<int32_t>(red),
                              static_cast<int32_t>(green),
                              static_cast<int32_t>(blue));
      break;
    case fastslide::DataType::kFloat32:
      rgb_image.FillWithColor(static_cast<float>(red),
                              static_cast<float>(green),
                              static_cast<float>(blue));
      break;
    case fastslide::DataType::kFloat64:
      rgb_image.FillWithColor(static_cast<double>(red),
                              static_cast<double>(green),
                              static_cast<double>(blue));
      break;
  }

  return fastslide_image_create_from_cpp(std::move(rgb_image));
}

// Extended property accessors

extern "C" int fastslide_image_is_initialized(const FastSlideImage* image) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return 0;
  }
  return image->image.IsInitialized() ? 1 : 0;
}

// Image operations

extern "C" int fastslide_image_paste(FastSlideImage* dest_image,
                                     const FastSlideImage* source_image,
                                     uint32_t dest_x, uint32_t dest_y,
                                     uint32_t source_x, uint32_t source_y,
                                     uint32_t source_width,
                                     uint32_t source_height) {
  if (dest_image == nullptr || source_image == nullptr) {
    SetLastError("dest_image and source_image cannot be null");
    return 0;
  }

  dest_image->image.Paste(source_image->image, dest_x, dest_y, source_x,
                          source_y, source_width, source_height);
  return 1;
}

// Memory management

void fastslide_image_free(FastSlideImage* image) {
  delete image;
}
