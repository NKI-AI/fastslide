// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include "fastslide/c/image.h"

#include <algorithm>
#include <cstdio>   // For printf
#include <cstdlib>  // For getenv
#include <cstring>  // For strcmp, strncpy, memcpy
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fastslide/image.h"

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

// Forward declaration
extern "C" void fastslide_set_last_error(const char* message);

void SetLastError(const char* message) {
  fastslide_set_last_error(message);
}

}  // namespace

// Factory functions

FastSlideImage* fastslide_image_create_rgb(FastSlideImageDimensions dimensions,
                                           FastSlideDataType data_type) {
  try {
    fastslide::ImageDimensions cpp_dims = {dimensions.width, dimensions.height};
    fastslide::DataType cpp_dtype = CEnumToDataType(data_type);

    auto cpp_image =
        fastslide::Image(cpp_dims, fastslide::ImageFormat::kRGB, cpp_dtype);
    return new FastSlideImage(std::move(cpp_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_create_rgba(FastSlideImageDimensions dimensions,
                                            FastSlideDataType data_type) {
  try {
    fastslide::ImageDimensions cpp_dims = {dimensions.width, dimensions.height};
    fastslide::DataType cpp_dtype = CEnumToDataType(data_type);

    auto cpp_image =
        fastslide::Image(cpp_dims, fastslide::ImageFormat::kRGBA, cpp_dtype);
    return new FastSlideImage(std::move(cpp_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_create_grayscale(
    FastSlideImageDimensions dimensions, FastSlideDataType data_type) {
  try {
    fastslide::ImageDimensions cpp_dims = {dimensions.width, dimensions.height};
    fastslide::DataType cpp_dtype = CEnumToDataType(data_type);

    auto cpp_image =
        fastslide::Image(cpp_dims, fastslide::ImageFormat::kGray, cpp_dtype);
    return new FastSlideImage(std::move(cpp_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_create_spectral(
    FastSlideImageDimensions dimensions, uint32_t channels,
    FastSlideDataType data_type) {
  try {
    fastslide::ImageDimensions cpp_dims = {dimensions.width, dimensions.height};
    fastslide::DataType cpp_dtype = CEnumToDataType(data_type);

    auto cpp_image = fastslide::Image(cpp_dims, channels, cpp_dtype);
    return new FastSlideImage(std::move(cpp_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
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

FastSlideImage* fastslide_image_to_rgb(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    auto rgb_image = image->image.ToRGB();
    if (!rgb_image) {
      SetLastError("failed to convert image to RGB");
      return nullptr;
    }
    return new FastSlideImage(std::move(*rgb_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_to_grayscale(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    auto gray_image = image->image.ToGrayscale();
    if (!gray_image) {
      SetLastError("failed to convert image to grayscale");
      return nullptr;
    }
    return new FastSlideImage(std::move(*gray_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_to_planar(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    auto planar_image = image->image.ToPlanar();
    if (!planar_image) {
      SetLastError("failed to convert image to planar layout");
      return nullptr;
    }
    return new FastSlideImage(std::move(*planar_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_to_interleaved(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    auto interleaved_image = image->image.ToInterleaved();
    if (!interleaved_image) {
      SetLastError("failed to convert image to interleaved layout");
      return nullptr;
    }
    return new FastSlideImage(std::move(*interleaved_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_extract_channels(
    const FastSlideImage* image, const uint32_t* channel_indices,
    uint32_t num_channels) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  if (num_channels > 0 && !channel_indices) {
    SetLastError("channel_indices cannot be null when num_channels > 0");
    return nullptr;
  }

  try {
    std::vector<uint32_t> indices(channel_indices,
                                  channel_indices + num_channels);
    auto extracted_image = image->image.ExtractChannels(indices);
    if (!extracted_image) {
      SetLastError("failed to extract channels");
      return nullptr;
    }
    return new FastSlideImage(std::move(*extracted_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_image_clone(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    auto cloned_image = image->image.Clone();
    return new FastSlideImage(std::move(*cloned_image));
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

int fastslide_image_get_description(const FastSlideImage* image, char* buffer,
                                    size_t buffer_size) {
  if (!image || !buffer) {
    SetLastError("image and buffer cannot be null");
    return -1;
  }

  try {
    std::string description = image->image.GetDescription();
    size_t desc_len = description.length();

    if (buffer_size <= desc_len) {
      SetLastError("buffer size is too small");
      return -1;
    }

    std::strncpy(buffer, description.c_str(), buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return static_cast<int>(desc_len);
  } catch (const std::exception& e) {
    SetLastError(e.what());
    return -1;
  }
}

// Helper functions for other modules

extern "C" FastSlideImage* fastslide_image_create_from_cpp(
    fastslide::Image image) {
  try {
    // Check if the image is valid
    if (image.GetDimensions()[0] == 0 || image.GetDimensions()[1] == 0) {
      printf(
          "[FastSlide C] ERROR: Cannot create wrapper for image with zero "
          "dimensions\n");
      SetLastError("Cannot create wrapper for image with zero dimensions");
      return nullptr;
    }

    // Allow uninitialized images (they may have null data until first paste)
    if (image.IsInitialized() && image.GetData() == nullptr) {
      printf(
          "[FastSlide C] ERROR: Cannot create wrapper for initialized image "
          "with null "
          "data\n");
      SetLastError(
          "Cannot create wrapper for initialized image with null data");
      return nullptr;
    }

    FastSlideImage* result = new FastSlideImage(std::move(image));
    return result;
  } catch (const std::exception& e) {
    printf("[FastSlide C] ERROR: Exception creating image wrapper: %s\n",
           e.what());
    SetLastError(e.what());
    return nullptr;
  }
}

const fastslide::Image& fastslide_image_get_cpp_image(
    const FastSlideImage* image) {
  if (!image) {
    throw std::invalid_argument("image cannot be null");
  }
  return image->image;
}

fastslide::Image& fastslide_image_get_cpp_image_mutable(FastSlideImage* image) {
  if (!image) {
    throw std::invalid_argument("image cannot be null");
  }
  return image->image;
}

// Extended factory functions

extern "C" FastSlideImage* fastslide_image_create_blank(
    FastSlideImageDimensions dimensions) {
  try {
    fastslide::ImageDimensions cpp_dims = {dimensions.width, dimensions.height};
    auto cpp_image = fastslide::CreateBlankImage(cpp_dims);
    return fastslide_image_create_from_cpp(std::move(*cpp_image));
  } catch (const std::exception& e) {
    printf("[FastSlide C] ERROR: Exception creating blank image: %s\n",
           e.what());
    SetLastError(e.what());
    return nullptr;
  }
}

extern "C" FastSlideImage* fastslide_image_create_solid_color(
    FastSlideImageDimensions dimensions, FastSlideDataType data_type,
    uint32_t red, uint32_t green, uint32_t blue) {
  try {
    fastslide::ImageDimensions cpp_dims = {dimensions.width, dimensions.height};
    fastslide::DataType cpp_dtype = CEnumToDataType(data_type);

    auto rgb_image = fastslide::CreateRGBImage(cpp_dims, cpp_dtype);

    // Fill with color based on data type
    switch (cpp_dtype) {
      case fastslide::DataType::kUInt8:
        rgb_image->FillWithColor(static_cast<uint8_t>(red),
                                 static_cast<uint8_t>(green),
                                 static_cast<uint8_t>(blue));
        break;
      case fastslide::DataType::kUInt16:
        rgb_image->FillWithColor(static_cast<uint16_t>(red),
                                 static_cast<uint16_t>(green),
                                 static_cast<uint16_t>(blue));
        break;
      case fastslide::DataType::kInt16:
        rgb_image->FillWithColor(static_cast<int16_t>(red),
                                 static_cast<int16_t>(green),
                                 static_cast<int16_t>(blue));
        break;
      case fastslide::DataType::kUInt32:
        rgb_image->FillWithColor(red, green, blue);
        break;
      case fastslide::DataType::kInt32:
        rgb_image->FillWithColor(static_cast<int32_t>(red),
                                 static_cast<int32_t>(green),
                                 static_cast<int32_t>(blue));
        break;
      case fastslide::DataType::kFloat32:
        rgb_image->FillWithColor(static_cast<float>(red),
                                 static_cast<float>(green),
                                 static_cast<float>(blue));
        break;
      case fastslide::DataType::kFloat64:
        rgb_image->FillWithColor(static_cast<double>(red),
                                 static_cast<double>(green),
                                 static_cast<double>(blue));
        break;
    }

    return fastslide_image_create_from_cpp(std::move(*rgb_image));
  } catch (const std::exception& e) {
    printf("[FastSlide C] ERROR: Exception creating solid color image: %s\n",
           e.what());
    SetLastError(e.what());
    return nullptr;
  }
}

// Extended property accessors

extern "C" int fastslide_image_is_initialized(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return 0;
  }

  try {
    return image->image.IsInitialized() ? 1 : 0;
  } catch (const std::exception& e) {
    printf("[FastSlide C] ERROR: Exception checking initialization: %s\n",
           e.what());
    SetLastError(e.what());
    return 0;
  }
}

// Image operations

extern "C" int fastslide_image_paste(FastSlideImage* dest_image,
                                     const FastSlideImage* source_image,
                                     uint32_t dest_x, uint32_t dest_y,
                                     uint32_t source_x, uint32_t source_y,
                                     uint32_t source_width,
                                     uint32_t source_height) {
  if (!dest_image || !source_image) {
    SetLastError("dest_image and source_image cannot be null");
    return 0;
  }

  try {
    auto& dest_cpp = fastslide_image_get_cpp_image_mutable(dest_image);
    const auto& source_cpp = fastslide_image_get_cpp_image(source_image);

    dest_cpp.Paste(source_cpp, dest_x, dest_y, source_x, source_y, source_width,
                   source_height);
    return 1;
  } catch (const std::exception& e) {
    printf("[FastSlide C] ERROR: Exception during paste: %s\n", e.what());
    SetLastError(e.what());
    return 0;
  }
}

// Memory management

void fastslide_image_free(FastSlideImage* image) {
  delete image;
}
