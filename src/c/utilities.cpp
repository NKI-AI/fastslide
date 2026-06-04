// Copyright 2025 Jonas Teuwen. All rights reserved.
//
// This file is part of FastSlide.
//
// Use of this source code is governed by the terms found in the
// LICENSE file located in the FastSlide project root.

#include "fastslide/c/utilities.h"

#include <cstring>

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/c/image.h"
#include "fastslide/c/slide_reader.h"
#include "fastslide/image.h"
#include "fastslide/resample/average.h"
#include "fastslide/resample/lanczos.h"
#include "fastslide/runtime/decoders/png_decoder.h"
#include "fastslide/slide_reader.h"
#include "fastslide/utilities/colors.h"

extern "C" void fastslide_set_last_error(const char* message);
extern "C" FastSlideImage* fastslide_image_create_from_cpp(
    fastslide::Image image);

const fastslide::Image& fastslide_image_get_cpp_image(
    const FastSlideImage* image);

namespace {

void SetLastError(const char* message) {
  fastslide_set_last_error(message);
}

const fastslide::Image& GetCppImage(const FastSlideImage* image) {
  return fastslide_image_get_cpp_image(image);
}

}  // namespace

FastSlideImage* fastslide_lanczos_resample(const FastSlideImage* image,
                                           uint32_t output_width,
                                           uint32_t output_height) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  if (output_width == 0 || output_height == 0) {
    SetLastError("output dimensions must be positive");
    return nullptr;
  }

  const auto& cpp_image = GetCppImage(image);
  if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
    SetLastError("image must have separate planar configuration");
    return nullptr;
  }

  auto result = fastslide::resample::LanczosResample(cpp_image, output_width,
                                                     output_height);
  if (!result) {
    SetLastError("failed to resample image with Lanczos3");
    return nullptr;
  }
  return fastslide_image_create_from_cpp(std::move(*result));
}

FastSlideImage* fastslide_lanczos2_resample(const FastSlideImage* image,
                                            uint32_t output_width,
                                            uint32_t output_height) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  if (output_width == 0 || output_height == 0) {
    SetLastError("output dimensions must be positive");
    return nullptr;
  }

  const auto& cpp_image = GetCppImage(image);
  if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
    SetLastError("image must have separate planar configuration");
    return nullptr;
  }

  auto result = fastslide::resample::Lanczos2Resample(cpp_image, output_width,
                                                      output_height);
  if (!result) {
    SetLastError("failed to resample image with Lanczos2");
    return nullptr;
  }
  return fastslide_image_create_from_cpp(std::move(*result));
}

FastSlideImage* fastslide_cosine_resample(const FastSlideImage* image,
                                          uint32_t output_width,
                                          uint32_t output_height) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  if (output_width == 0 || output_height == 0) {
    SetLastError("output dimensions must be positive");
    return nullptr;
  }

  const auto& cpp_image = GetCppImage(image);
  if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
    SetLastError("image must have separate planar configuration");
    return nullptr;
  }

  auto result = fastslide::resample::CosineResample(cpp_image, output_width,
                                                    output_height);
  if (!result) {
    SetLastError("failed to resample image with Cosine windowed sinc");
    return nullptr;
  }
  return fastslide_image_create_from_cpp(std::move(*result));
}

FastSlideImage* fastslide_average_resample(const FastSlideImage* image,
                                           uint32_t factor) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }
  if (factor == 0) {
    SetLastError("factor must be greater than 0");
    return nullptr;
  }

  const auto& cpp_image = GetCppImage(image);
  auto result = fastslide::resample::AverageResample(cpp_image, factor);
  if (!result) {
    SetLastError("failed to resample image with average downsampling");
    return nullptr;
  }
  return fastslide_image_create_from_cpp(std::move(*result));
}

FastSlideImage* fastslide_average_2x2_resample(const FastSlideImage* image) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  const auto& cpp_image = GetCppImage(image);
  if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
    SetLastError("image must have separate planar configuration");
    return nullptr;
  }

  auto result = fastslide::resample::Average2x2Resample(cpp_image);
  if (!result) {
    SetLastError("failed to resample image with 2x2 average downsampling");
    return nullptr;
  }
  return fastslide_image_create_from_cpp(std::move(*result));
}

FastSlideImage* fastslide_average_4x4_resample(const FastSlideImage* image) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  const auto& cpp_image = GetCppImage(image);
  if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
    SetLastError("image must have separate planar configuration");
    return nullptr;
  }

  auto result = fastslide::resample::Average4x4Resample(cpp_image);
  if (!result) {
    SetLastError("failed to resample image with 4x4 average downsampling");
    return nullptr;
  }
  return fastslide_image_create_from_cpp(std::move(*result));
}

FastSlideImage* fastslide_average_8x8_resample(const FastSlideImage* image) {
  if (image == nullptr) {
    SetLastError("image cannot be null");
    return nullptr;
  }

  const auto& cpp_image = GetCppImage(image);
  if (cpp_image.GetPlanarConfig() != fastslide::PlanarConfig::kSeparate) {
    SetLastError("image must have separate planar configuration");
    return nullptr;
  }

  auto result = fastslide::resample::Average8x8Resample(cpp_image);
  if (!result) {
    SetLastError("failed to resample image with 8x8 average downsampling");
    return nullptr;
  }
  return fastslide_image_create_from_cpp(std::move(*result));
}

extern "C" int fastslide_examples_save_as_png(const FastSlideImage* image,
                                              const char* filename) {
  if (image == nullptr || filename == nullptr) {
    SetLastError("image and filename cannot be null");
    return 0;
  }

  const auto& cpp_image = GetCppImage(image);
  if (cpp_image.GetWidth() == 0 || cpp_image.GetHeight() == 0) {
    SetLastError("Invalid image dimensions");
    return 0;
  }
  if (cpp_image.GetDataType() != fastslide::DataType::kUInt8) {
    SetLastError("Expected uint8 RGB(A) image for PNG output");
    return 0;
  }
  if (cpp_image.GetFormat() != fastslide::ImageFormat::kRGB &&
      cpp_image.GetFormat() != fastslide::ImageFormat::kRGBA) {
    SetLastError("Expected RGB or RGBA image for PNG output");
    return 0;
  }

  const uint32_t channels = cpp_image.GetChannels();
  const std::size_t pixel_bytes = static_cast<std::size_t>(channels) *
                                  cpp_image.GetWidth() * cpp_image.GetHeight();
  const auto status = fastslide::runtime::decoders::EncodePngToFile(
      filename, std::span<const uint8_t>(cpp_image.GetData(), pixel_bytes),
      cpp_image.GetWidth(), cpp_image.GetHeight(), channels);
  if (!status.ok()) {
    const std::string error_msg =
        "Failed to save PNG: " + std::string(status.message());
    SetLastError(error_msg.c_str());
    return 0;
  }
  return 1;
}

extern "C" FastSlideImage* fastslide_examples_load_from_png(
    const char* filename) {
  if (filename == nullptr) {
    SetLastError("filename cannot be null");
    return nullptr;
  }

  auto decoded_or = fastslide::runtime::decoders::DecodePngFileToRgb(filename);
  if (!decoded_or.ok()) {
    const std::string error_msg =
        "Failed to load PNG: " + std::string(decoded_or.status().message());
    SetLastError(error_msg.c_str());
    return nullptr;
  }
  auto decoded = std::move(decoded_or).value();

  fastslide::Image cpp_image(
      fastslide::ImageDimensions{decoded.width, decoded.height},
      fastslide::ImageFormat::kRGB, fastslide::DataType::kUInt8);
  std::memcpy(cpp_image.GetData(), decoded.rgb.data(), decoded.rgb.size());
  return fastslide_image_create_from_cpp(std::move(cpp_image));
}
