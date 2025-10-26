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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_IMAGE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_IMAGE_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "aifocore/concepts/numeric.h"

namespace fastslide {

using aifocore::Size;

/// @brief Image dimensions
using ImageDimensions = Size<uint32_t, 2>;  // [width, height]

/// @brief Image coordinate
using ImageCoordinate = Size<uint32_t, 2>;  // [x, y]

/// @brief Image format enumeration
enum class ImageFormat {
  kGray = 1,     ///< Single channel grayscale
  kRGB = 3,      ///< 3 channels: Red, Green, Blue
  kRGBA = 4,     ///< 4 channels: Red, Green, Blue, Alpha
  kSpectral = 0  ///< N channels (determined at runtime)
};

/// @brief Data type enumeration for pixel values
enum class DataType {
  kUInt8,    ///< 8-bit unsigned integer
  kUInt16,   ///< 16-bit unsigned integer
  kInt16,    ///< 16-bit signed integer
  kUInt32,   ///< 32-bit unsigned integer
  kInt32,    ///< 32-bit signed integer
  kFloat32,  ///< 32-bit floating point
  kFloat64   ///< 64-bit floating point
};

/// @brief Get size in bytes for a given data type
/// @param dtype Data type
/// @return Size in bytes
constexpr size_t GetDataTypeSize(DataType dtype) {
  switch (dtype) {
    case DataType::kUInt8:
      return sizeof(uint8_t);
    case DataType::kUInt16:
      return sizeof(uint16_t);
    case DataType::kInt16:
      return sizeof(int16_t);
    case DataType::kUInt32:
      return sizeof(uint32_t);
    case DataType::kInt32:
      return sizeof(int32_t);
    case DataType::kFloat32:
      return sizeof(float);
    case DataType::kFloat64:
      return sizeof(double);
  }
  return 0;  // Should never reach here
}

// Type traits for mapping DataType enum to C++ types
template <DataType>
struct DataTypeToCpp;

template <>
struct DataTypeToCpp<DataType::kUInt8> {
  using type = uint8_t;
};

template <>
struct DataTypeToCpp<DataType::kUInt16> {
  using type = uint16_t;
};

template <>
struct DataTypeToCpp<DataType::kInt16> {
  using type = int16_t;
};

template <>
struct DataTypeToCpp<DataType::kUInt32> {
  using type = uint32_t;
};

template <>
struct DataTypeToCpp<DataType::kInt32> {
  using type = int32_t;
};

template <>
struct DataTypeToCpp<DataType::kFloat32> {
  using type = float;
};

template <>
struct DataTypeToCpp<DataType::kFloat64> {
  using type = double;
};

/// @brief Get string representation of data type
/// @param dtype Data type
/// @return String representation
constexpr const char* GetName(DataType dtype) {
  switch (dtype) {
    case DataType::kUInt8:
      return "uint8";
    case DataType::kUInt16:
      return "uint16";
    case DataType::kInt16:
      return "int16";
    case DataType::kUInt32:
      return "uint32";
    case DataType::kInt32:
      return "int32";
    case DataType::kFloat32:
      return "float32";
    case DataType::kFloat64:
      return "float64";
  }
  return "unknown";
}

// Dispatching helper
template <typename Func>
void DispatchByDataType(DataType dtype, Func&& func) {
  switch (dtype) {
    case DataType::kUInt8:
      func.template operator()<uint8_t>();
      break;
    case DataType::kUInt16:
      func.template operator()<uint16_t>();
      break;
    case DataType::kInt16:
      func.template operator()<int16_t>();
      break;
    case DataType::kUInt32:
      func.template operator()<uint32_t>();
      break;
    case DataType::kInt32:
      func.template operator()<int32_t>();
      break;
    case DataType::kFloat32:
      func.template operator()<float>();
      break;
    case DataType::kFloat64:
      func.template operator()<double>();
      break;
    default:
      throw std::runtime_error("Unsupported data type");
  }
}

/// @brief Memory layout configuration for multi-channel images
/// Based on libtiff PLANARCONFIG values
/// @details For spectral/hyperspectral images, kSeparate (separate) is
/// strongly recommended for better cache locality and pixel-wise operations
/// common in spectral processing. For RGB(A) images, kContiguous (interleaved)
/// is recommended.
enum class PlanarConfig {
  kContiguous =
      1,         ///< Chunky/Interleaved: RGBRGBRGB... (default, recommended for
                 // spectral)
  kSeparate = 2  ///< Planar/Band separate: RRR...GGG...BBB... (for RGB(A))
};

/// @brief Get string representation of image format
/// @param format Image format
/// @return String representation
constexpr const char* GetName(ImageFormat format) {
  switch (format) {
    case ImageFormat::kGray:
      return "Gray";
    case ImageFormat::kRGB:
      return "RGB";
    case ImageFormat::kRGBA:
      return "RGBA";
    case ImageFormat::kSpectral:
      return "Spectral";
  }
  return "unknown";
}

/// @brief Get string representation of planar configuration
/// @param config Planar configuration
/// @return String representation
constexpr const char* GetName(PlanarConfig config) {
  switch (config) {
    case PlanarConfig::kContiguous:
      return "Contig";
    case PlanarConfig::kSeparate:
      return "Separate";
  }
  return "unknown";
}

// Backward compatibility aliases
constexpr const char* GetDataTypeName(DataType dtype) {
  return GetName(dtype);
}

constexpr const char* GetImageFormatName(ImageFormat format) {
  return GetName(format);
}

/// @brief Get number of channels for standard formats
/// @param format Image format
/// @return Number of channels (0 for spectral - determined at runtime)
constexpr uint32_t GetFormatChannels(ImageFormat format) {
  return static_cast<uint32_t>(format);
}

/// @brief Generic image container with flexible format and data type support
class Image {
 public:
  /// @brief Default constructor for empty image
  Image()
      : dimensions_({0, 0}),
        format_(ImageFormat::kSpectral),  // Use kSpectral for uninitialized
        dtype_(DataType::kUInt8),         // Default, will be overridden
        channels_(0),                     // Uninitialized
        bytes_per_sample_(0),             // Uninitialized
        planar_config_(PlanarConfig::kContiguous),  // Default
        initialized_(false) {}                      // Not initialized

  /// @brief Constructor for truly blank/uninitialized image with specific
  /// dimensions
  /// @param dimensions Image dimensions [width, height]
  /// @details Creates an uninitialized image that will adapt to the first
  /// paste operation
  explicit Image(const ImageDimensions& dimensions)
      : dimensions_(dimensions),
        format_(ImageFormat::kSpectral),  // Use kSpectral for uninitialized
        dtype_(DataType::kUInt8),         // Default, will be overridden
        channels_(0),                     // Uninitialized
        bytes_per_sample_(0),             // Uninitialized
        planar_config_(PlanarConfig::kContiguous),  // Default
        initialized_(false) {}                      // Not initialized

  /// @brief Constructor for standard formats
  /// @param dimensions Image dimensions [width, height]
  /// @param format Image format
  /// @param dtype Data type
  /// @param config Planar configuration (default: Contig/Interleaved)
  Image(const ImageDimensions& dimensions, ImageFormat format, DataType dtype,
        PlanarConfig config = PlanarConfig::kContiguous)
      : dimensions_(dimensions),
        format_(format),
        dtype_(dtype),
        bytes_per_sample_(GetDataTypeSize(dtype)),
        planar_config_(config) {

    if (format == ImageFormat::kSpectral) {
      throw std::invalid_argument(
          "Use spectral constructor for spectral images");
    }

    channels_ = GetFormatChannels(format);
    size_t total_size =
        dimensions_[0] * dimensions_[1] * channels_ * bytes_per_sample_;
    data_.resize(total_size, 0);
    initialized_ = true;
  }

  /// @brief Constructor for spectral/hyperspectral images
  /// @param dimensions Image dimensions [width, height]
  /// @param channels Number of channels
  /// @param dtype Data type
  /// @param config Planar configuration (default: kContiguous)
  /// @details Spectral images should use interleaved format
  /// (PlanarConfig::kContiguous) for better cache locality and pixel-wise
  /// operations common in spectral processing.
  Image(const ImageDimensions& dimensions, uint32_t channels, DataType dtype,
        PlanarConfig config = PlanarConfig::kContiguous)
      : dimensions_(dimensions),
        format_(ImageFormat::kSpectral),
        dtype_(dtype),
        channels_(channels),
        bytes_per_sample_(GetDataTypeSize(dtype)),
        planar_config_(config) {

    size_t total_size =
        dimensions_[0] * dimensions_[1] * channels_ * bytes_per_sample_;
    data_.resize(total_size, 0);
    initialized_ = true;
  }

  /// @brief Constructor for blank image with properties inherited from
  /// reference image
  /// @param reference_image Image to inherit properties from
  /// (format, data type, channels, planar config)
  /// @param dimensions New dimensions for the blank image [width, height]
  /// @details Creates an uninitialized image with the same data type, channels,
  /// and memory layout as the reference image but with different dimensions.
  /// Memory is not zero-initialized for performance.
  Image(const Image& reference_image, const ImageDimensions& dimensions)
      : dimensions_(dimensions),
        format_(reference_image.format_),
        dtype_(reference_image.dtype_),
        channels_(reference_image.channels_),
        bytes_per_sample_(reference_image.bytes_per_sample_),
        planar_config_(reference_image.planar_config_) {

    size_t total_size =
        dimensions_[0] * dimensions_[1] * channels_ * bytes_per_sample_;
    data_.resize(total_size);  // Note: Not zero-initialized
    initialized_ = true;
  }

  /// @brief Copy constructor
  Image(const Image& other) = default;

  /// @brief Move constructor
  Image(Image&& other) noexcept = default;

  /// @brief Copy assignment
  Image& operator=(const Image& other) = default;

  /// @brief Move assignment
  Image& operator=(Image&& other) noexcept = default;

  /// @brief RGB color assignment operator for RGB images
  /// @param rgb_values RGB values as {R, G, B} (0-255 for uint8, scaled for
  /// other types)
  /// @return Reference to this image
  /// @details Fills the entire RGB image with the specified color
  template <typename T = uint8_t>
  Image& operator=(std::initializer_list<T> rgb_values) {
    if (format_ != ImageFormat::kRGB) {
      throw std::invalid_argument(
          "Color assignment only supported for RGB images");
    }
    if (rgb_values.size() != 3) {
      throw std::invalid_argument(
          "RGB color assignment requires exactly 3 values");
    }

    auto it = rgb_values.begin();
    T r = *it++;
    T g = *it++;
    T b = *it;

    FillWithColor(r, g, b);
    return *this;
  }

  /// @brief Destructor
  ~Image() = default;

  // Accessors

  /// @brief Get image dimensions
  /// @return Image dimensions [width, height]
  [[nodiscard]] const ImageDimensions& GetDimensions() const noexcept {
    return dimensions_;
  }

  /// @brief Get image width
  /// @return Image width
  [[nodiscard]] uint32_t GetWidth() const noexcept { return dimensions_[0]; }

  /// @brief Get image height
  /// @return Image height
  [[nodiscard]] uint32_t GetHeight() const noexcept { return dimensions_[1]; }

  /// @brief Get number of channels
  /// @return Number of channels
  [[nodiscard]] uint32_t GetChannels() const noexcept { return channels_; }

  /// @brief Get image format
  /// @return Image format
  [[nodiscard]] ImageFormat GetFormat() const noexcept { return format_; }

  /// @brief Get data type
  /// @return Data type
  [[nodiscard]] DataType GetDataType() const noexcept { return dtype_; }

  /// @brief Get planar configuration
  /// @return Planar configuration
  [[nodiscard]] PlanarConfig GetPlanarConfig() const noexcept {
    return planar_config_;
  }

  /// @brief Get bytes per sample
  /// @return Bytes per sample
  [[nodiscard]] size_t GetBytesPerSample() const noexcept {
    return bytes_per_sample_;
  }

  /// @brief Check if image is empty
  /// @return True if image is empty
  [[nodiscard]] bool Empty() const noexcept {
    return dimensions_[0] == 0 || dimensions_[1] == 0 || channels_ == 0 ||
           !initialized_;
  }

  /// @brief Check if image is initialized
  /// @return True if image has been initialized with format and data type
  [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

  /// @brief Manually initialize a blank image with specified properties
  /// @param format Image format
  /// @param dtype Data type
  /// @param config Planar configuration (default: Contig)
  /// @details Initializes a blank image that was created with dimensions only
  void Initialize(ImageFormat format, DataType dtype,
                  PlanarConfig config = PlanarConfig::kContiguous) {
    if (initialized_) {
      throw std::runtime_error("Image is already initialized");
    }
    if (dimensions_[0] == 0 || dimensions_[1] == 0) {
      throw std::invalid_argument(
          "Cannot initialize image with zero dimensions");
    }

    format_ = format;
    dtype_ = dtype;
    planar_config_ = config;
    bytes_per_sample_ = GetDataTypeSize(dtype);

    if (format == ImageFormat::kSpectral) {
      throw std::invalid_argument("Use InitializeSpectral for spectral images");
    }

    channels_ = GetFormatChannels(format);
    size_t total_size =
        dimensions_[0] * dimensions_[1] * channels_ * bytes_per_sample_;
    data_.resize(total_size, 0);
    initialized_ = true;
  }

  /// @brief Manually initialize a blank image as spectral with specified
  /// channels
  /// @param channels Number of channels
  /// @param dtype Data type
  /// @param config Planar configuration (default: Contig)
  /// @details Initializes a blank image as spectral with custom channel count
  void InitializeSpectral(uint32_t channels, DataType dtype,
                          PlanarConfig config = PlanarConfig::kContiguous) {
    if (initialized_) {
      throw std::runtime_error("Image is already initialized");
    }
    if (dimensions_[0] == 0 || dimensions_[1] == 0) {
      throw std::invalid_argument(
          "Cannot initialize image with zero dimensions");
    }
    if (channels == 0) {
      throw std::invalid_argument("Channel count must be greater than 0");
    }

    format_ = ImageFormat::kSpectral;
    dtype_ = dtype;
    channels_ = channels;
    planar_config_ = config;
    bytes_per_sample_ = GetDataTypeSize(dtype);

    size_t total_size =
        dimensions_[0] * dimensions_[1] * channels_ * bytes_per_sample_;
    data_.resize(total_size, 0);
    initialized_ = true;
  }

  /// @brief Set number of channels for an existing initialized image
  /// (advanced operation)
  /// @param new_channels New number of channels
  /// @details This is a potentially dangerous operation that changes the
  /// channel count for an existing image. Data will be reallocated and
  /// existing data may be lost. Only use this if you understand the
  /// implications.
  void SetChannels(uint32_t new_channels) {
    if (!initialized_) {
      throw std::runtime_error("Cannot set channels on uninitialized image");
    }
    if (new_channels == 0) {
      throw std::invalid_argument("Channel count must be greater than 0");
    }
    if (new_channels == channels_) {
      return;  // No change needed
    }

    // Update format based on new channel count
    if (new_channels == 1) {
      format_ = ImageFormat::kGray;
    } else if (new_channels == 3) {
      format_ = ImageFormat::kRGB;
    } else if (new_channels == 4) {
      format_ = ImageFormat::kRGBA;
    } else {
      format_ = ImageFormat::kSpectral;
    }

    channels_ = new_channels;

    // Reallocate data with new channel count
    size_t total_size =
        dimensions_[0] * dimensions_[1] * channels_ * bytes_per_sample_;
    data_.resize(total_size, 0);  // Zero-initialize new data
  }

  /// @brief Convert existing image to spectral format with specified channels
  /// @param new_channels Number of spectral channels
  /// @details Converts an existing image to spectral format. Existing data
  /// will be lost.
  void ConvertToSpectral(uint32_t new_channels) {
    if (!initialized_) {
      throw std::runtime_error(
          "Cannot convert uninitialized image to spectral");
    }
    if (new_channels == 0) {
      throw std::invalid_argument("Channel count must be greater than 0");
    }

    format_ = ImageFormat::kSpectral;
    channels_ = new_channels;

    // Reallocate data
    size_t total_size =
        dimensions_[0] * dimensions_[1] * channels_ * bytes_per_sample_;
    data_.resize(total_size, 0);  // Zero-initialize
  }

  /// @brief Get total size in bytes
  /// @return Total size in bytes
  [[nodiscard]] size_t SizeBytes() const noexcept { return data_.size(); }

  /// @brief Get total number of pixels
  /// @return Total number of pixels
  [[nodiscard]] size_t GetPixelCount() const noexcept {
    return static_cast<size_t>(dimensions_[0]) * dimensions_[1];
  }

  // Data access

  /// @brief Get raw data pointer
  /// @return Pointer to raw data
  [[nodiscard]] const uint8_t* GetData() const noexcept { return data_.data(); }

  /// @brief Get mutable raw data pointer
  /// @return Pointer to raw data
  [[nodiscard]] uint8_t* GetData() noexcept { return data_.data(); }

  /// @brief Get raw data as vector reference
  /// @return Reference to data vector
  [[nodiscard]] const std::vector<uint8_t>& GetDataVector() const noexcept {
    return data_;
  }

  /// @brief Get mutable raw data as vector reference
  /// @return Reference to data vector
  [[nodiscard]] std::vector<uint8_t>& GetDataVector() noexcept { return data_; }

  /// @brief Get typed data pointer
  /// @tparam T Data type
  /// @return Typed pointer to data
  template <typename T>
  [[nodiscard]] const T* GetDataAs() const {
    static_assert(std::is_arithmetic_v<T>, "T must be an arithmetic type");
    if (sizeof(T) != bytes_per_sample_) {
      throw std::runtime_error("Type size mismatch with image data type");
    }
    return reinterpret_cast<const T*>(data_.data());
  }

  /// @brief Get mutable typed data pointer
  /// @tparam T Data type
  /// @return Typed pointer to data
  template <typename T>
  [[nodiscard]] T* GetDataAs() {
    static_assert(std::is_arithmetic_v<T>, "T must be an arithmetic type");
    if (sizeof(T) != bytes_per_sample_) {
      throw std::runtime_error("Type size mismatch with image data type");
    }
    return reinterpret_cast<T*>(data_.data());
  }

  // Pixel access

  /// @brief Get pixel value (typed)
  /// @tparam T Data type
  /// @param y Row coordinate
  /// @param x Column coordinate
  /// @param channel Channel index
  /// @return Reference to pixel value
  template <typename T>
  [[nodiscard]] T& At(uint32_t y, uint32_t x, uint32_t channel) {
    ValidateCoordinates(y, x, channel);
    size_t index = GetPixelIndex(y, x, channel);
    return GetDataAs<T>()[index];
  }

  /// @brief Get pixel value (typed, const)
  /// @tparam T Data type
  /// @param y Row coordinate
  /// @param x Column coordinate
  /// @param channel Channel index
  /// @return Const reference to pixel value
  template <typename T>
  [[nodiscard]] const T& At(uint32_t y, uint32_t x, uint32_t channel) const {
    ValidateCoordinates(y, x, channel);
    size_t index = GetPixelIndex(y, x, channel);
    return GetDataAs<T>()[index];
  }

  /// @brief Get pixel data offset in bytes
  /// @param y Row coordinate
  /// @param x Column coordinate
  /// @param channel Channel index
  /// @return Byte offset
  [[nodiscard]] size_t GetPixelOffset(uint32_t y, uint32_t x,
                                      uint32_t channel) const {
    ValidateCoordinates(y, x, channel);
    return GetPixelIndex(y, x, channel) * bytes_per_sample_;
  }

  // Conversion and utility methods

  /// @brief Convert to RGB format (if possible)
  /// @return RGB image or nullptr if conversion not supported
  [[nodiscard]] std::unique_ptr<Image> ToRGB() const;

  /// @brief Convert to grayscale format
  /// @return Grayscale image
  [[nodiscard]] std::unique_ptr<Image> ToGrayscale() const;

  /// @brief Extract specific channels
  /// @param channel_indices Indices of channels to extract
  /// @return New image with extracted channels
  [[nodiscard]] std::unique_ptr<Image> ExtractChannels(
      const std::vector<uint32_t>& channel_indices) const;

  /// @brief Convert to interleaved (contig) layout
  /// @return Image with interleaved layout
  [[nodiscard]] std::unique_ptr<Image> ToInterleaved() const;

  /// @brief Convert to planar (separate) layout
  /// @return Image with planar layout
  [[nodiscard]] std::unique_ptr<Image> ToPlanar() const;

  /// @brief Convert memory layout format
  /// @param target_config Target planar configuration
  /// @return Image with requested memory layout (copy if already in target
  /// format)
  [[nodiscard]] std::unique_ptr<Image> ConvertMemoryFormat(
      PlanarConfig target_config) const;

  /// @brief Fill entire image with a solid color
  /// @tparam T Color component type (should match image data type)
  /// @param r Red component
  /// @param g Green component
  /// @param b Blue component
  /// @details Only works for RGB images. Color values should match the image's
  /// data type range.
  template <typename T>
  void FillWithColor(T r, T g, T b) {
    if (format_ != ImageFormat::kRGB) {
      throw std::invalid_argument(
          "FillWithColor only supported for RGB images");
    }

    // Handle different data types
    switch (dtype_) {
      case DataType::kUInt8: {
        auto* data = GetDataAs<uint8_t>();
        FillWithColorTyped(data, static_cast<uint8_t>(r),
                           static_cast<uint8_t>(g), static_cast<uint8_t>(b));
        break;
      }
      case DataType::kUInt16: {
        auto* data = GetDataAs<uint16_t>();
        FillWithColorTyped(data, static_cast<uint16_t>(r),
                           static_cast<uint16_t>(g), static_cast<uint16_t>(b));
        break;
      }
      case DataType::kInt16: {
        auto* data = GetDataAs<int16_t>();
        FillWithColorTyped(data, static_cast<int16_t>(r),
                           static_cast<int16_t>(g), static_cast<int16_t>(b));
        break;
      }
      case DataType::kUInt32: {
        auto* data = GetDataAs<uint32_t>();
        FillWithColorTyped(data, static_cast<uint32_t>(r),
                           static_cast<uint32_t>(g), static_cast<uint32_t>(b));
        break;
      }
      case DataType::kInt32: {
        auto* data = GetDataAs<int32_t>();
        FillWithColorTyped(data, static_cast<int32_t>(r),
                           static_cast<int32_t>(g), static_cast<int32_t>(b));
        break;
      }
      case DataType::kFloat32: {
        auto* data = GetDataAs<float>();
        FillWithColorTyped(data, static_cast<float>(r), static_cast<float>(g),
                           static_cast<float>(b));
        break;
      }
      case DataType::kFloat64: {
        auto* data = GetDataAs<double>();
        FillWithColorTyped(data, static_cast<double>(r), static_cast<double>(g),
                           static_cast<double>(b));
        break;
      }
    }
  }

  /// @brief Paste another image onto this image at specified coordinates
  /// @param source_image Image to paste
  /// @param dest_x Destination x coordinate (left edge)
  /// @param dest_y Destination y coordinate (top edge)
  /// @param source_x Source x coordinate to start copying from (default: 0)
  /// @param source_y Source y coordinate to start copying from (default: 0)
  /// @param source_width Width of source region to copy
  /// (default: 0 = full width)
  /// @param source_height Height of source region to copy
  /// (default: 0 = full height)
  /// @details Pastes source image data onto this image. Images must have
  /// compatible data types, channels, and planar configurations. Source image
  /// will be clipped if it extends beyond this image's boundaries.
  void Paste(const Image& source_image, uint32_t dest_x, uint32_t dest_y,
             uint32_t source_x = 0, uint32_t source_y = 0,
             uint32_t source_width = 0, uint32_t source_height = 0);

  /// @brief Clone the image
  /// @return Deep copy of the image
  [[nodiscard]] std::unique_ptr<Image> Clone() const {
    return std::make_unique<Image>(*this);
  }

  /// @brief Get description string
  /// @return Human-readable description
  [[nodiscard]] std::string GetDescription() const {
    if (!initialized_) {
      return "Uninitialized " + std::to_string(dimensions_[0]) + "x" +
             std::to_string(dimensions_[1]);
    }
    return std::string(GetName(format_)) + " " +
           std::to_string(dimensions_[0]) + "x" +
           std::to_string(dimensions_[1]) + "x" + std::to_string(channels_) +
           " " + GetName(dtype_) + " " + GetName(planar_config_);
  }

 private:
  ImageDimensions dimensions_;  ///< Image dimensions [width, height]
  ImageFormat format_;          ///< Image format
  DataType dtype_;              ///< Data type
  uint32_t channels_;           ///< Number of channels
  size_t bytes_per_sample_;     ///< Bytes per sample
  PlanarConfig planar_config_;  ///< Memory layout configuration
  std::vector<uint8_t> data_;   ///< Raw image data
  bool initialized_;  ///< Flag to indicate if the image has been initialized

  /// @brief Helper template for filling with typed data
  template <typename DataT>
  void FillWithColorTyped(DataT* data, DataT r, DataT g, DataT b) {
    size_t pixel_count = GetPixelCount();
    for (size_t i = 0; i < pixel_count; ++i) {
      if (planar_config_ == PlanarConfig::kContiguous) {
        // Interleaved: RGBRGBRGB...
        data[i * 3 + 0] = r;
        data[i * 3 + 1] = g;
        data[i * 3 + 2] = b;
      } else {
        // Planar: RRR...GGG...BBB...
        data[i] = r;                    // R channel
        data[pixel_count + i] = g;      // G channel
        data[2 * pixel_count + i] = b;  // B channel
      }
    }
  }

  /// @brief Validate coordinates and channel index
  /// @param y Row coordinate
  /// @param x Column coordinate
  /// @param channel Channel index
  void ValidateCoordinates(uint32_t y, uint32_t x, uint32_t channel) const {
    if (x >= dimensions_[0] || y >= dimensions_[1] || channel >= channels_) {
      throw std::out_of_range(
          "Pixel coordinates or channel index out of bounds");
    }
  }

  /// @brief Calculate the pixel index based on planar configuration
  /// @param y Row coordinate
  /// @param x Column coordinate
  /// @param channel Channel index
  /// @return Pixel index
  size_t GetPixelIndex(uint32_t y, uint32_t x, uint32_t channel) const {
    if (planar_config_ == PlanarConfig::kContiguous) {
      // Interleaved: RGBRGBRGB...
      return ((static_cast<size_t>(y) * dimensions_[0] + x) * channels_ +
              channel);
    } else {  // PlanarConfig::kSeparate
      // Planar: RRR...GGG...BBB...
      size_t pixels_per_channel =
          static_cast<size_t>(dimensions_[0]) * dimensions_[1];
      size_t pixel_offset = static_cast<size_t>(y) * dimensions_[0] + x;
      return channel * pixels_per_channel + pixel_offset;
    }
  }
};

/// @brief Stream output operator for Image
/// @param os Output stream
/// @param image Image to output
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& os, const Image& image) {
  os << image.GetDescription();
  return os;
}

/// @brief Convenient type aliases for backward compatibility
using RGBImage = Image;  // Can be constructed with ImageFormat::kRGB

/// @brief Factory functions for common image types

/// @brief Create RGB image
/// @param dimensions Image dimensions
/// @param dtype Data type
/// @param config Planar configuration
/// @return RGB image
inline std::unique_ptr<Image> CreateRGBImage(
    const ImageDimensions& dimensions, DataType dtype = DataType::kUInt8,
    PlanarConfig config = PlanarConfig::kContiguous) {
  return std::make_unique<Image>(dimensions, ImageFormat::kRGB, dtype, config);
}

/// @brief Create RGBA image
/// @param dimensions Image dimensions
/// @param dtype Data type
/// @param config Planar configuration
/// @return RGBA image
inline std::unique_ptr<Image> CreateRGBAImage(
    const ImageDimensions& dimensions, DataType dtype = DataType::kUInt8,
    PlanarConfig config = PlanarConfig::kContiguous) {
  return std::make_unique<Image>(dimensions, ImageFormat::kRGBA, dtype, config);
}

/// @brief Create grayscale image
/// @param dimensions Image dimensions
/// @param dtype Data type
/// @param config Planar configuration (usually Contig for single channel)
/// @return Grayscale image
inline std::unique_ptr<Image> CreateGrayscaleImage(
    const ImageDimensions& dimensions, DataType dtype = DataType::kUInt8,
    PlanarConfig config = PlanarConfig::kContiguous) {
  return std::make_unique<Image>(dimensions, ImageFormat::kGray, dtype, config);
}

/// @brief Create spectral/hyperspectral image
/// @param dimensions Image dimensions
/// @param channels Number of spectral channels
/// @param dtype Data type
/// @param config Planar configuration (default: kContiguous)
/// @return Spectral image with interleaved channel layout for optimal
/// performance
inline std::unique_ptr<Image> CreateSpectralImage(
    const ImageDimensions& dimensions, uint32_t channels,
    DataType dtype = DataType::kFloat32,
    PlanarConfig config = PlanarConfig::kContiguous) {
  return std::make_unique<Image>(dimensions, channels, dtype, config);
}

/// @brief Create blank image with properties inherited from reference image
/// @param reference_image Image to inherit properties from
/// (format, data type, channels, planar config)
/// @param dimensions New dimensions for the blank image [width, height]
/// @return Blank image with same properties as reference but different
/// dimensions
/// @details Creates an uninitialized image for performance.
/// Use Paste() to populate with data.
inline std::unique_ptr<Image> CreateBlankImage(
    const Image& reference_image, const ImageDimensions& dimensions) {
  return std::make_unique<Image>(reference_image, dimensions);
}

/// @brief Create truly blank/uninitialized image that adapts to first paste
/// @param dimensions Image dimensions [width, height]
/// @return Uninitialized blank image that will adapt to first paste operation
/// @details Creates a completely uninitialized image. Format, data type, and
/// channels will be determined by the first paste operation. Use Initialize()
/// to manually set properties.
inline std::unique_ptr<Image> CreateBlankImage(
    const ImageDimensions& dimensions) {
  return std::make_unique<Image>(dimensions);
}

/// @brief Create initialized blank RGB image (for backward compatibility)
/// @param dimensions Image dimensions [width, height]
/// @param dtype Data type (default: uint8)
/// @param config Planar configuration (default: interleaved)
/// @return Initialized blank RGB image with zeroed memory
/// @details Creates an initialized RGB image ready for direct pixel access.
inline std::unique_ptr<Image> CreateInitializedBlankImage(
    const ImageDimensions& dimensions, DataType dtype = DataType::kUInt8,
    PlanarConfig config = PlanarConfig::kContiguous) {
  return std::make_unique<Image>(dimensions, ImageFormat::kRGB, dtype, config);
}

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_IMAGE_H_
