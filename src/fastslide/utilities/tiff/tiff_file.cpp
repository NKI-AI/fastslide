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

#include "fastslide/utilities/tiff/tiff_file.h"

#include <tiffio.h>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {

// Static factory method
absl::StatusOr<TiffFile> TiffFile::Create(TIFFHandlePool* pool) {
  if (pool == nullptr) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "TIFFHandlePool cannot be null");
  }

  auto handle_guard = pool->Acquire();
  if (!handle_guard.Valid()) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to acquire TIFF handle from pool");
  }

  return TiffFile(std::move(handle_guard));
}

// Static validation method
absl::Status TiffFile::ValidateFile(const std::filesystem::path& filename) {
  TIFF* tif = TIFFOpen(filename.string().c_str(), "rm");
  if (tif == nullptr) {
    return MAKE_STATUS(absl::StatusCode::kInvalidArgument,
                       "Cannot open TIFF file: " + filename.string());
  }
  TIFFClose(tif);
  return absl::OkStatus();
}

// Private constructor
TiffFile::TiffFile(TIFFHandleGuard handle_guard)
    : handle_guard_(std::move(handle_guard)),
      current_directory_(0),
      cached_directory_count_(0),
      directory_count_cached_(false) {}

// Directory operations
absl::StatusOr<uint16_t> TiffFile::GetDirectoryCount() const {
  if (directory_count_cached_) {
    return cached_directory_count_;
  }

  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  TIFF* tif = handle_guard_.Get();
  uint16_t dir_count = 0;
  const uint16_t current_dir = TIFFCurrentDirectory(tif);

  // Count directories by iterating through them
  if (TIFFSetDirectory(tif, 0) == 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to set directory to 0");
  }

  ++dir_count;  // Count the first directory (directory 0)
  while (TIFFReadDirectory(tif) != 0) {
    ++dir_count;
  }

  // Restore original directory
  if (TIFFSetDirectory(tif, current_dir) == 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to restore original directory");
  }

  // Cache the result for future calls
  cached_directory_count_ = dir_count;
  directory_count_cached_ = true;

  return dir_count;
}

absl::Status TiffFile::SetDirectory(uint16_t dir_index) {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  TIFF* tif = handle_guard_.Get();

  // Always call TIFFSetDirectory to ensure we're on the right directory
  // Don't trust the cached current_directory_ as handles may be reused
  if (TIFFSetDirectory(tif, dir_index) == 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Failed to set directory to {}", dir_index));
  }

  current_directory_ = dir_index;

  return absl::OkStatus();
}

uint16_t TiffFile::GetCurrentDirectory() const {
  return current_directory_;
}

// Field access helpers
template <typename T>
absl::StatusOr<T> TiffFile::GetRequiredField(ttag_t tag) const {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  T value;
  TIFF* tif = handle_guard_.Get();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  if (TIFFGetField(tif, tag, &value) != 1) {
    return MAKE_STATUS(
        absl::StatusCode::kNotFound,
        aifocore::fmt::format("Required field (tag {}) not found",
                              static_cast<uint32_t>(tag)));
  }
  return value;
}

template <typename T>
std::optional<T> TiffFile::GetOptionalField(ttag_t tag) const {
  if (!IsValid()) {
    return std::nullopt;
  }

  T value;
  TIFF* tif = handle_guard_.Get();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  if (TIFFGetField(tif, tag, &value) == 1) {
    return value;
  }
  return std::nullopt;
}

std::string TiffFile::GetStringField(ttag_t tag) const {
  if (!IsValid()) {
    return "";
  }

  char* str_ptr = nullptr;
  TIFF* tif = handle_guard_.Get();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  if (TIFFGetField(tif, tag, &str_ptr) == 1 && str_ptr != nullptr) {
    // Optimize string creation by checking length first
    const size_t len = std::strlen(str_ptr);
    if (len > 0) {
      return std::string(str_ptr, len);
    }
  }
  return "";
}

// Image dimensions
uint32_t width, height;

absl::StatusOr<TiffImageDimensions> TiffFile::GetImageDimensions() const {
  ASSIGN_OR_RETURN_STATUSOR(width,
                            GetRequiredField<uint32_t>(TIFFTAG_IMAGEWIDTH));
  ASSIGN_OR_RETURN_STATUSOR(height,
                            GetRequiredField<uint32_t>(TIFFTAG_IMAGELENGTH));
  return TiffImageDimensions{width, height};
}

absl::StatusOr<TiffTileDimensions> TiffFile::GetTileDimensions() const {
  if (!IsTiled()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "Image is not tiled");
  }

  ASSIGN_OR_RETURN_STATUSOR(width,
                            GetRequiredField<uint32_t>(TIFFTAG_TILEWIDTH));
  ASSIGN_OR_RETURN_STATUSOR(height,
                            GetRequiredField<uint32_t>(TIFFTAG_TILELENGTH));
  return TiffTileDimensions{width, height};
}

bool TiffFile::IsTiled() const {
  if (!IsValid()) {
    return false;
  }
  return TIFFIsTiled(handle_guard_.Get()) != 0;
}

// Simple field accessors
std::string TiffFile::GetImageDescription() const {
  return GetStringField(TIFFTAG_IMAGEDESCRIPTION);
}

std::string TiffFile::GetSoftware() const {
  return GetStringField(TIFFTAG_SOFTWARE);
}

absl::StatusOr<TiffPhotometric> TiffFile::GetPhotometric() const {
  uint16_t raw_result;
  ASSIGN_OR_RETURN_STATUSOR(raw_result,
                            GetRequiredField<uint16_t>(TIFFTAG_PHOTOMETRIC));
  return static_cast<TiffPhotometric>(raw_result);
}

absl::StatusOr<TiffCompression> TiffFile::GetCompression() const {
  // TODO(jonasteuwen): This is a weird macro, but it works
  // Do simplify
  DECLARE_ASSIGN_OR_RETURN_STATUSOR(
      uint16_t, result, GetRequiredField<uint16_t>(TIFFTAG_COMPRESSION));
  return static_cast<TiffCompression>(result);
}

absl::StatusOr<uint16_t> TiffFile::GetSamplesPerPixel() const {
  return GetRequiredField<uint16_t>(TIFFTAG_SAMPLESPERPIXEL);
}

absl::StatusOr<uint16_t> TiffFile::GetBitsPerSample() const {
  return GetRequiredField<uint16_t>(TIFFTAG_BITSPERSAMPLE);
}

absl::StatusOr<TiffSampleFormat> TiffFile::GetSampleFormat() const {
  auto value = GetOptionalField<uint16_t>(TIFFTAG_SAMPLEFORMAT)
                   .value_or(SAMPLEFORMAT_UINT);
  return static_cast<TiffSampleFormat>(value);
}

absl::StatusOr<TiffPlanarConfig> TiffFile::GetPlanarConfig() const {
  auto value = GetOptionalField<uint16_t>(TIFFTAG_PLANARCONFIG)
                   .value_or(PLANARCONFIG_CONTIG);
  return static_cast<TiffPlanarConfig>(value);
}

absl::StatusOr<uint32_t> TiffFile::GetSubfileType() const {
  return GetOptionalField<uint32_t>(TIFFTAG_SUBFILETYPE).value_or(0);
}

std::optional<float> TiffFile::GetXResolution() const {
  return GetOptionalField<float>(TIFFTAG_XRESOLUTION);
}

std::optional<float> TiffFile::GetYResolution() const {
  return GetOptionalField<float>(TIFFTAG_YRESOLUTION);
}

uint16_t TiffFile::GetResolutionUnit() const {
  return GetOptionalField<uint16_t>(TIFFTAG_RESOLUTIONUNIT)
      .value_or(RESUNIT_INCH);  // Inch is the default if not set
}

absl::StatusOr<DataType> TiffFile::GetDataType() const {
  uint16_t bits_per_sample;
  ASSIGN_OR_RETURN_STATUSOR(bits_per_sample, GetBitsPerSample());
  TiffSampleFormat sample_format;
  ASSIGN_OR_RETURN_STATUSOR(sample_format, GetSampleFormat());

  switch (sample_format) {
    case TiffSampleFormat::UInt:
      switch (bits_per_sample) {
        case 8:
          return DataType::kUInt8;
        case 16:
          return DataType::kUInt16;
        case 32:
          return DataType::kUInt32;
        default:
          return MAKE_STATUS(
              absl::StatusCode::kInvalidArgument,
              aifocore::fmt::format("Unsupported unsigned bit-depth: {}",
                                    bits_per_sample));
      }

    case TiffSampleFormat::Int:
      switch (bits_per_sample) {
        case 16:
          return DataType::kInt16;
        case 32:
          return DataType::kInt32;
        default:
          return MAKE_STATUS(
              absl::StatusCode::kInvalidArgument,
              aifocore::fmt::format("Unsupported signed bit-depth: {}",
                                    bits_per_sample));
      }

    case TiffSampleFormat::IEEEfp:
      switch (bits_per_sample) {
        case 32:
          return DataType::kFloat32;
        case 64:
          return DataType::kFloat64;
        default:
          return MAKE_STATUS(
              absl::StatusCode::kInvalidArgument,
              aifocore::fmt::format("Unsupported float bit-depth: {}",
                                    bits_per_sample));
      }

    default:
      return MAKE_STATUS(
          absl::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Unknown TIFF SampleFormat: {}",
                                static_cast<uint16_t>(sample_format)));
  }
}

// Reading operations
absl::Status TiffFile::ReadTile(void* buffer,
                                const aifocore::Size<uint32_t, 2>& tile_coords,
                                uint16_t sample) const {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  if (!IsTiled()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "Image is not tiled");
  }

  TIFF* tif = handle_guard_.Get();
  const tsize_t bytes_read =
      TIFFReadTile(tif, buffer, tile_coords[0], tile_coords[1], 0, sample);
  if (bytes_read < 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read tile ({}, {}) sample {}",
                              tile_coords[0], tile_coords[1], sample));
  }

  return absl::OkStatus();
}

absl::Status TiffFile::ReadScanline(void* buffer, uint32_t row,
                                    uint16_t sample) const {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  if (IsTiled()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "Image is tiled, use ReadTile instead");
  }

  TIFF* tif = handle_guard_.Get();
  if (TIFFReadScanline(tif, buffer, row, sample) < 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        aifocore::fmt::format("Failed to read scanline {} sample {}", row,
                              sample));
  }

  return absl::OkStatus();
}

absl::StatusOr<size_t> TiffFile::GetTileSize() const {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  if (!IsTiled()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "Image is not tiled");
  }

  TIFF* tif = handle_guard_.Get();
  const tsize_t size = TIFFTileSize(tif);
  if (size < 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal, "Failed to get tile size");
  }

  return static_cast<size_t>(size);
}

absl::StatusOr<size_t> TiffFile::GetScanlineSize() const {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  TIFF* tif = handle_guard_.Get();
  const tsize_t size = TIFFScanlineSize(tif);
  if (size < 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to get scanline size");
  }

  return static_cast<size_t>(size);
}

absl::Status TiffFile::ReadRGBAImage(uint32_t* buffer, uint32_t width,
                                     uint32_t height,
                                     bool stop_on_error) const {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  TIFF* tif = handle_guard_.Get();
  if (TIFFReadRGBAImage(tif, width, height, buffer, stop_on_error ? 1 : 0) ==
      0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to read RGBA image");
  }

  return absl::OkStatus();
}

// Utility methods
TIFF* TiffFile::GetHandle() const {
  return handle_guard_.Get();
}

bool TiffFile::IsValid() const {
  return handle_guard_.Valid();
}

absl::StatusOr<TiffDirectoryInfo> TiffFile::GetDirectoryInfo() const {
  if (!IsValid()) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "TIFF handle is not valid");
  }

  TiffDirectoryInfo info;

  // Get required fields
  ASSIGN_OR_RETURN_STATUSOR(info.image_dims, GetImageDimensions());
  ASSIGN_OR_RETURN_STATUSOR(info.samples_per_pixel, GetSamplesPerPixel());
  ASSIGN_OR_RETURN_STATUSOR(info.bits_per_sample, GetBitsPerSample());
  ASSIGN_OR_RETURN_STATUSOR(info.photometric, GetPhotometric());
  ASSIGN_OR_RETURN_STATUSOR(info.compression, GetCompression());
  ASSIGN_OR_RETURN_STATUSOR(info.sample_format, GetSampleFormat());
  ASSIGN_OR_RETURN_STATUSOR(info.planar_config, GetPlanarConfig());
  ASSIGN_OR_RETURN_STATUSOR(info.subfile_type, GetSubfileType());
  info.resolution_unit = GetResolutionUnit();

  // Get optional fields
  info.is_tiled = IsTiled();
  if (info.is_tiled) {
    ASSIGN_OR_RETURN_STATUSOR(info.tile_dims, GetTileDimensions());
  } else {
    info.tile_dims = std::nullopt;
  }

  const std::string desc = GetImageDescription();
  if (!desc.empty()) {
    info.image_description = desc;
  }

  const std::string software = GetSoftware();
  if (!software.empty()) {
    info.software = software;
  }

  info.x_resolution = GetXResolution();
  info.y_resolution = GetYResolution();

  return info;
}

// Explicit template instantiations for common types
template absl::StatusOr<uint16_t> TiffFile::GetRequiredField<uint16_t>(
    ttag_t tag) const;
template absl::StatusOr<uint32_t> TiffFile::GetRequiredField<uint32_t>(
    ttag_t tag) const;
template absl::StatusOr<float> TiffFile::GetRequiredField<float>(
    ttag_t tag) const;

template std::optional<uint16_t> TiffFile::GetOptionalField<uint16_t>(
    ttag_t tag) const;
template std::optional<uint32_t> TiffFile::GetOptionalField<uint32_t>(
    ttag_t tag) const;
template std::optional<float> TiffFile::GetOptionalField<float>(
    ttag_t tag) const;

}  // namespace fastslide
