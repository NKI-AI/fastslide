// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include "fastslide/c/utilities.h"

#include <lodepng/lodepng.h>
#include <cstring>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fastslide/c/image.h"
#include "fastslide/c/slide_reader.h"
#include "fastslide/image.h"
#include "fastslide/resample/average.h"
#include "fastslide/resample/lanczos.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/colors.h"

// Forward declarations for external functions
extern "C" void fastslide_set_last_error(const char* message);
extern "C" FastSlideImage* fastslide_image_create_from_cpp(
    fastslide::Image image);

// Forward declaration for function in image.cpp
const fastslide::Image& fastslide_image_get_cpp_image(
    const FastSlideImage* image);

namespace {

void SetLastError(const char* message) {
  fastslide_set_last_error(message);
}

// Helper to get FastSlideImage internal image
const fastslide::Image& GetCppImage(const FastSlideImage* image) {
  return fastslide_image_get_cpp_image(image);
}

}  // namespace

FastSlideImage* fastslide_lanczos_resample(const FastSlideImage* image,
                                           uint32_t output_width,
                                           uint32_t output_height) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  if (output_width == 0 || output_height == 0) {
    SetLastError("output dimensions must be positive");
    return nullptr;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    // Check if image has separate planar configuration
    if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
      SetLastError("image must have separate planar configuration");
      return nullptr;
    }

    // Call the C++ LanczosResample function
    auto result = fastslide::resample::LanczosResample(cpp_image, output_width,
                                                       output_height);
    if (!result) {
      SetLastError("failed to resample image with Lanczos3");
      return nullptr;
    }

    return fastslide_image_create_from_cpp(std::move(*result));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_lanczos2_resample(const FastSlideImage* image,
                                            uint32_t output_width,
                                            uint32_t output_height) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  if (output_width == 0 || output_height == 0) {
    SetLastError("output dimensions must be positive");
    return nullptr;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    // Check if image has separate planar configuration
    if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
      SetLastError("image must have separate planar configuration");
      return nullptr;
    }

    // Call the C++ Lanczos2Resample function
    auto result = fastslide::resample::Lanczos2Resample(cpp_image, output_width,
                                                        output_height);
    if (!result) {
      SetLastError("failed to resample image with Lanczos2");
      return nullptr;
    }

    return fastslide_image_create_from_cpp(std::move(*result));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_cosine_resample(const FastSlideImage* image,
                                          uint32_t output_width,
                                          uint32_t output_height) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  if (output_width == 0 || output_height == 0) {
    SetLastError("output dimensions must be positive");
    return nullptr;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    // Check if image has separate planar configuration
    if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
      SetLastError("image must have separate planar configuration");
      return nullptr;
    }

    // Call the C++ CosineResample function
    auto result = fastslide::resample::CosineResample(cpp_image, output_width,
                                                      output_height);
    if (!result) {
      SetLastError("failed to resample image with Cosine windowed sinc");
      return nullptr;
    }

    return fastslide_image_create_from_cpp(std::move(*result));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_average_resample(const FastSlideImage* image,
                                           uint32_t factor) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  if (factor == 0) {
    SetLastError("factor must be greater than 0");
    return nullptr;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    // Call the C++ AverageResample function
    auto result = fastslide::resample::AverageResample(cpp_image, factor);
    if (!result) {
      SetLastError("failed to resample image with average downsampling");
      return nullptr;
    }

    return fastslide_image_create_from_cpp(std::move(*result));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_average_2x2_resample(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    // Check if image has separate planar configuration
    if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
      SetLastError("image must have separate planar configuration");
      return nullptr;
    }

    // Call the C++ Average2x2Resample function
    auto result = fastslide::resample::Average2x2Resample(cpp_image);
    if (!result) {
      SetLastError("failed to resample image with 2x2 average downsampling");
      return nullptr;
    }

    return fastslide_image_create_from_cpp(std::move(*result));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_average_4x4_resample(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    // Check if image has separate planar configuration
    if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
      SetLastError("image must have separate planar configuration");
      return nullptr;
    }

    // Call the C++ Average4x4Resample function
    auto result = fastslide::resample::Average4x4Resample(cpp_image);
    if (!result) {
      SetLastError("failed to resample image with 4x4 average downsampling");
      return nullptr;
    }

    return fastslide_image_create_from_cpp(std::move(*result));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

FastSlideImage* fastslide_average_8x8_resample(const FastSlideImage* image) {
  if (!image) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    // Check if image has separate planar configuration
    if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
      SetLastError("image must have separate planar configuration");
      return nullptr;
    }

    // Call the C++ Average8x8Resample function
    auto result = fastslide::resample::Average8x8Resample(cpp_image);
    if (!result) {
      SetLastError("failed to resample image with 8x8 average downsampling");
      return nullptr;
    }

    return fastslide_image_create_from_cpp(std::move(*result));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}

extern "C" int fastslide_examples_save_as_png(const FastSlideImage* image,
                                              const char* filename) {
  if (!image || !filename) {
    SetLastError("image and filename cannot be null");
    return 0;
  }

  try {
    const auto& cpp_image = GetCppImage(image);

    if (cpp_image.GetWidth() == 0 || cpp_image.GetHeight() == 0) {
      SetLastError("Invalid image dimensions");
      return 0;
    }

    // Ensure it's uint8 RGB format
    if (cpp_image.GetDataType() != fastslide::DataType::kUInt8) {
      SetLastError("Expected uint8 RGB(A) image for PNG output");
      return 0;
    }

    if (cpp_image.GetFormat() != fastslide::ImageFormat::kRGB &&
        cpp_image.GetFormat() != fastslide::ImageFormat::kRGBA) {
      SetLastError("Expected RGB or RGBA image for PNG output");
      return 0;
    }

    // Use lodepng to encode
    unsigned error;
    if (cpp_image.GetChannels() == 3) {
      // RGB format
      error =
          lodepng_encode24_file(filename, cpp_image.GetData(),
                                cpp_image.GetWidth(), cpp_image.GetHeight());
    } else {
      // RGBA format
      error =
          lodepng_encode32_file(filename, cpp_image.GetData(),
                                cpp_image.GetWidth(), cpp_image.GetHeight());
    }

    if (error) {
      std::string error_msg =
          "Failed to save PNG: " + std::string(lodepng_error_text(error));
      SetLastError(error_msg.c_str());
      return 0;
    }

    return 1;

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return 0;
  }
}

extern "C" FastSlideImage* fastslide_examples_load_from_png(
    const char* filename) {
  if (!filename) {
    SetLastError("filename cannot be null");
    return nullptr;
  }

  try {
    unsigned width, height;
    std::vector<unsigned char> image_data;

    // Try to load as RGBA first
    unsigned error = lodepng::decode(image_data, width, height, filename);

    if (error) {
      std::string error_msg =
          "Failed to load PNG: " + std::string(lodepng_error_text(error));
      SetLastError(error_msg.c_str());
      return nullptr;
    }

    // lodepng::decode returns RGBA, convert to RGB by dropping alpha channel
    std::vector<unsigned char> rgb_data;
    rgb_data.reserve(width * height * 3);

    for (size_t i = 0; i < width * height; ++i) {
      rgb_data.push_back(image_data[i * 4 + 0]);  // R
      rgb_data.push_back(image_data[i * 4 + 1]);  // G
      rgb_data.push_back(image_data[i * 4 + 2]);  // B
      // Skip alpha channel (i * 4 + 3)
    }

    // Create FastSlide image
    auto cpp_image = fastslide::CreateRGBImage(
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
        fastslide::DataType::kUInt8);

    // Copy data to FastSlide image
    std::memcpy(cpp_image->GetData(), rgb_data.data(), rgb_data.size());

    return fastslide_image_create_from_cpp(std::move(*cpp_image));

  } catch (const std::exception& e) {
    SetLastError(e.what());
    return nullptr;
  }
}
