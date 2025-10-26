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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TIFF_FILE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TIFF_FILE_H_

#include <tiffio.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/concepts/numeric.h"
#include "fastslide/image.h"
#include "fastslide/utilities/tiff/tiff_pool.h"

namespace fastslide {

using aifocore::Size;

/// @brief Image dimensions structure
using TiffImageDimensions = Size<uint32_t, 2>;

/// @brief Tile dimensions structure
using TiffTileDimensions = Size<uint32_t, 2>;

/// @brief TIFF photometric interpretation enumeration
/// @details Specifies how pixel data should be interpreted colorimetrically.
/// Common values include RGB, grayscale, and palette-based images.
enum class TiffPhotometric : uint16_t {
  MinIsWhite =
      PHOTOMETRIC_MINISWHITE,  ///< Minimum value is white (inverted grayscale)
  MinIsBlack =
      PHOTOMETRIC_MINISBLACK,     ///< Minimum value is black (normal grayscale)
  RGB = PHOTOMETRIC_RGB,          ///< RGB color model
  Palette = PHOTOMETRIC_PALETTE,  ///< Palette/indexed color
  Mask = PHOTOMETRIC_MASK,        ///< Holdout mask
  Separated = PHOTOMETRIC_SEPARATED,  ///< Color separations (CMYK)
  YCbCr = PHOTOMETRIC_YCBCR,          ///< YCbCr color space
  CIELab = PHOTOMETRIC_CIELAB,        ///< CIE L*a*b* color space
  ICCLab = PHOTOMETRIC_ICCLAB,        ///< ICC L*a*b* color space
  ITULab = PHOTOMETRIC_ITULAB,        ///< ITU L*a*b* color space
  LogL = PHOTOMETRIC_LOGL,            ///< CIE Log2(L) color space
  LogLuv = PHOTOMETRIC_LOGLUV         ///< CIE Log2(L) + (u,v) color space
};

/// @brief TIFF compression enumeration
/// @details Specifies the compression algorithm used for image data.
/// Different algorithms offer trade-offs between file size and decode speed.
enum class TiffCompression : uint16_t {
  None = COMPRESSION_NONE,                   ///< No compression
  CCITTRLE = COMPRESSION_CCITTRLE,           ///< CCITT modified Huffman RLE
  CCITTFAX3 = COMPRESSION_CCITTFAX3,         ///< CCITT Group 3 fax
  CCITTFAX4 = COMPRESSION_CCITTFAX4,         ///< CCITT Group 4 fax
  LZW = COMPRESSION_LZW,                     ///< Lempel-Ziv & Welch
  OJPEG = COMPRESSION_OJPEG,                 ///< Original JPEG (deprecated)
  JPEG = COMPRESSION_JPEG,                   ///< JPEG compression
  Next = COMPRESSION_NEXT,                   ///< NeXT 2-bit RLE
  CCITTRLEW = COMPRESSION_CCITTRLEW,         ///< CCITT RLE with word alignment
  Packbits = COMPRESSION_PACKBITS,           ///< Macintosh RLE
  Thunderscan = COMPRESSION_THUNDERSCAN,     ///< ThunderScan RLE
  IT8CTPAD = COMPRESSION_IT8CTPAD,           ///< IT8 CT with padding
  IT8LW = COMPRESSION_IT8LW,                 ///< IT8 linework RLE
  IT8MP = COMPRESSION_IT8MP,                 ///< IT8 monochrome picture
  IT8BL = COMPRESSION_IT8BL,                 ///< IT8 binary line art
  PixarFilm = COMPRESSION_PIXARFILM,         ///< Pixar companded 10-bit LZW
  PixarLog = COMPRESSION_PIXARLOG,           ///< Pixar companded 11-bit ZIP
  Deflate = COMPRESSION_DEFLATE,             ///< Deflate compression
  AdobeDeflate = COMPRESSION_ADOBE_DEFLATE,  ///< Adobe-style deflate
  DCS = COMPRESSION_DCS,                     ///< Kodak DCS
  JBIG = COMPRESSION_JBIG,                   ///< ISO JBIG
  SGILog = COMPRESSION_SGILOG,               ///< SGI log luminance RLE
  SGILog24 = COMPRESSION_SGILOG24,           ///< SGI log 24-bit packed
  JP2000 = COMPRESSION_JP2000                ///< Leadtools JPEG2000
};

/// @brief TIFF sample format enumeration
/// @details Specifies the data format of pixel samples.
enum class TiffSampleFormat : uint16_t {
  UInt = SAMPLEFORMAT_UINT,                   ///< Unsigned integer data
  Int = SAMPLEFORMAT_INT,                     ///< Signed integer data
  IEEEfp = SAMPLEFORMAT_IEEEFP,               ///< IEEE floating point data
  Void = SAMPLEFORMAT_VOID,                   ///< Untyped data
  ComplexInt = SAMPLEFORMAT_COMPLEXINT,       ///< Complex signed integer
  ComplexIEEEfp = SAMPLEFORMAT_COMPLEXIEEEFP  ///< Complex IEEE floating point
};

/// @brief TIFF planar configuration enumeration
/// @details Specifies how multi-sample pixels are stored.
enum class TiffPlanarConfig : uint8_t {
  Contig = PLANARCONFIG_CONTIG,  ///< Samples are interleaved (RGBRGBRGB...)
  Separate =
      PLANARCONFIG_SEPARATE  ///< Samples stored separately (RRR...GGG...BBB...)
};

/// @brief TIFF directory information structure
/// @details Contains comprehensive metadata about a TIFF directory/page.
/// This structure aggregates all commonly-used TIFF fields for efficient
/// access.
struct TiffDirectoryInfo {
  TiffImageDimensions image_dims;               ///< Image width and height
  std::optional<TiffTileDimensions> tile_dims;  ///< Tile dimensions (if tiled)
  uint16_t samples_per_pixel;                   ///< Number of samples per pixel
  uint16_t bits_per_sample;                     ///< Bits per sample
  TiffPhotometric photometric;                  ///< Photometric interpretation
  TiffCompression compression;                  ///< Compression algorithm
  TiffSampleFormat sample_format;               ///< Sample data format
  TiffPlanarConfig planar_config;               ///< Planar configuration
  uint32_t subfile_type;                        ///< Subfile type flags
  std::optional<std::string> image_description;  ///< Image description
  std::optional<std::string> software;           ///< Software identifier
  std::optional<std::string> datetime;           ///< Date/time string
  std::optional<float> x_resolution;             ///< X resolution
  std::optional<float> y_resolution;             ///< Y resolution
  uint16_t resolution_unit;                      ///< Resolution unit
  bool is_tiled;                                 ///< Whether image is tiled

  /// @brief Get bytes per pixel calculation
  /// @return Number of bytes per pixel
  /// @details Calculates (samples_per_pixel * bits_per_sample + 7) / 8
  [[nodiscard]] size_t GetBytesPerPixel() const noexcept {
    return (samples_per_pixel * bits_per_sample + 7) / 8;
  }

  /// @brief Check if this is a reduced resolution image
  /// @return True if this directory represents a reduced resolution image
  [[nodiscard]] bool IsReducedResolution() const noexcept {
    return (subfile_type & FILETYPE_REDUCEDIMAGE) != 0;
  }

  /// @brief Check if this is a mask
  /// @return True if this directory represents a mask
  [[nodiscard]] bool IsMask() const noexcept {
    return (subfile_type & FILETYPE_MASK) != 0;
  }
};

/// @brief Comprehensive RAII wrapper for TIFF file operations
/// @details This class provides a type-safe, modern C++ interface to libtiff
/// operations while integrating with the existing thread-safe handle pool
/// infrastructure. It replaces scattered TIFFGetField calls throughout the
/// codebase with a clean, error-handling API.
///
/// Key features:
/// - RAII resource management with automatic cleanup
/// - Type-safe field access with comprehensive error handling
/// - Performance optimizations including directory count caching
/// - Thread-safe when used with TIFFHandlePool
/// - Comprehensive metadata extraction in single calls
///
/// @note This class is not thread-safe on its own. Thread safety is provided
/// by the underlying TIFFHandlePool which manages handle allocation.
class TiffFile {
 public:
  /// @brief Factory method to create a TiffFile instance
  /// @param pool Handle pool to acquire TIFF handles from
  /// @return StatusOr containing TiffFile instance or error
  /// @details Creates a new TiffFile instance by acquiring a handle from the
  /// pool. The handle is automatically returned to the pool when the TiffFile
  /// is destroyed.
  static absl::StatusOr<TiffFile> Create(TIFFHandlePool* pool);

  /// @brief Validate that a TIFF file can be opened (static utility method)
  /// @param filename Path to the TIFF file
  /// @return Status indicating whether the file is a valid TIFF file
  /// @details Attempts to open the file with libtiff to verify it's a valid
  /// TIFF. This is a lightweight validation that doesn't parse the full file
  /// structure.
  static absl::Status ValidateFile(const std::filesystem::path& filename);

  /// @brief Destructor (RAII cleanup)
  /// @details Automatically returns the TIFF handle to the pool when
  /// destroyed.
  ~TiffFile() = default;

  // Non-copyable but movable
  TiffFile(const TiffFile&) = delete;
  TiffFile& operator=(const TiffFile&) = delete;

  TiffFile(TiffFile&& other) noexcept = default;
  TiffFile& operator=(TiffFile&& other) noexcept = default;

  /// @brief Get the total number of directories in the TIFF file
  /// @return Number of directories or error status
  /// @details This method caches the result for performance since directory
  /// counting requires iteration through all directories.
  absl::StatusOr<uint16_t> GetDirectoryCount() const;

  /// @brief Set the current directory
  /// @param dir_index Directory index (0-based)
  /// @return Status indicating success or failure
  /// @details Changes the current directory context for subsequent operations.
  /// Optimized to avoid unnecessary directory switches.
  absl::Status SetDirectory(uint16_t dir_index);

  /// @brief Get the current directory index
  /// @return Current directory index (0-based)
  /// @details Returns the currently active directory without accessing the
  /// TIFF handle.
  [[nodiscard]] uint16_t GetCurrentDirectory() const;

  /// @brief Get comprehensive information about the current directory
  /// @return TiffDirectoryInfo structure with all metadata or error status
  /// @details Efficiently retrieves all commonly-used directory metadata in a
  /// single call. This is more efficient than making multiple individual field
  /// requests.
  absl::StatusOr<TiffDirectoryInfo> GetDirectoryInfo() const;

  /// @brief Get image dimensions for current directory
  /// @return Image dimensions (width, height) or error status
  /// @details Retrieves TIFFTAG_IMAGEWIDTH and TIFFTAG_IMAGELENGTH.
  absl::StatusOr<TiffImageDimensions> GetImageDimensions() const;

  /// @brief Get tile dimensions (only valid for tiled images)
  /// @return Tile dimensions or error if not tiled
  /// @details Retrieves TIFFTAG_TILEWIDTH and TIFFTAG_TILELENGTH.
  /// Returns error if the image is not tiled.
  absl::StatusOr<TiffTileDimensions> GetTileDimensions() const;

  /// @brief Check if current directory is tiled
  /// @return True if tiled, false if strip-based
  /// @details Uses TIFFIsTiled() to determine image organization.
  [[nodiscard]] bool IsTiled() const;

  /// @brief Get image description field
  /// @return Image description string or empty if not present
  /// @details Retrieves TIFFTAG_IMAGEDESCRIPTION. Returns empty string if not
  /// set.
  [[nodiscard]] std::string GetImageDescription() const;

  /// @brief Get software field
  /// @return Software string or empty if not present
  /// @details Retrieves TIFFTAG_SOFTWARE. Returns empty string if not set.
  [[nodiscard]] std::string GetSoftware() const;

  /// @brief Get photometric interpretation
  /// @return Photometric interpretation or error status
  /// @details Retrieves TIFFTAG_PHOTOMETRIC and converts to enum.
  [[nodiscard]] absl::StatusOr<TiffPhotometric> GetPhotometric() const;

  /// @brief Get compression type
  /// @return Compression type or error status
  /// @details Retrieves TIFFTAG_COMPRESSION and converts to enum.
  [[nodiscard]] absl::StatusOr<TiffCompression> GetCompression() const;

  /// @brief Get samples per pixel
  /// @return Samples per pixel or error status
  /// @details Retrieves TIFFTAG_SAMPLESPERPIXEL. Required field.
  [[nodiscard]] absl::StatusOr<uint16_t> GetSamplesPerPixel() const;

  /// @brief Get bits per sample
  /// @return Bits per sample or error status
  /// @details Retrieves TIFFTAG_BITSPERSAMPLE. Required field.
  [[nodiscard]] absl::StatusOr<uint16_t> GetBitsPerSample() const;

  /// @brief Get sample format
  /// @return Sample format or error status
  /// @details Retrieves TIFFTAG_SAMPLEFORMAT. Defaults to SAMPLEFORMAT_UINT if
  /// not set.
  [[nodiscard]] absl::StatusOr<TiffSampleFormat> GetSampleFormat() const;

  /// @brief Get planar configuration
  /// @return Planar configuration or error status
  /// @details Retrieves TIFFTAG_PLANARCONFIG. Defaults to PLANARCONFIG_CONTIG
  /// if not set.
  [[nodiscard]] absl::StatusOr<TiffPlanarConfig> GetPlanarConfig() const;

  /// @brief Get subfile type
  /// @return Subfile type flags or error status
  /// @details Retrieves TIFFTAG_SUBFILETYPE. Defaults to 0 if not set.
  [[nodiscard]] absl::StatusOr<uint32_t> GetSubfileType() const;

  /// @brief Get X resolution
  /// @return X resolution or nullopt if not set
  /// @details Retrieves TIFFTAG_XRESOLUTION. Optional field.
  [[nodiscard]] std::optional<float> GetXResolution() const;

  /// @brief Get Y resolution
  /// @return Y resolution or nullopt if not set
  /// @details Retrieves TIFFTAG_YRESOLUTION. Optional field.
  [[nodiscard]] std::optional<float> GetYResolution() const;

  /// @brief Get resolution unit
  /// @return Resolution unit (1=none, 2=inch, 3=cm)
  /// @details Retrieves TIFFTAG_RESOLUTIONUNIT. Defaults to RESUNIT_INCH if
  /// not set.
  [[nodiscard]] uint16_t GetResolutionUnit() const;

  /// @brief Get data type based on bits per sample and sample format
  /// @return Data type or error status
  /// @details Converts TIFF bits per sample and sample format to DataType enum.
  /// Supports unsigned integers (8, 16, 32 bit), signed integers (16, 32 bit),
  /// and floating point (32, 64 bit).
  [[nodiscard]] absl::StatusOr<DataType> GetDataType() const;

  /// @brief Read a tile from the current directory
  /// @param buffer Pre-allocated buffer to read into (must be at least
  /// GetTileSize() bytes)
  /// @param tile_coords Tile coordinates (in tile units, not pixels)
  /// @param sample Sample number (for planar separate images, 0 for
  /// interleaved)
  /// @return Status indicating success or failure
  /// @details Reads a single tile using TIFFReadTile(). The buffer must be
  /// pre-allocated with sufficient space. Use GetTileSize() to determine
  /// required buffer size.
  absl::Status ReadTile(void* buffer,
                        const aifocore::Size<uint32_t, 2>& tile_coords,
                        uint16_t sample = 0) const;

  /// @brief Read a scanline from the current directory
  /// @param buffer Pre-allocated buffer to read into (must be at least
  /// GetScanlineSize() bytes)
  /// @param row Row number to read (0-based)
  /// @param sample Sample number (for planar separate images, 0 for
  /// interleaved)
  /// @return Status indicating success or failure
  /// @details Reads a single scanline using TIFFReadScanline(). Only works for
  /// strip-based images. Use GetScanlineSize() to determine required buffer
  /// size.
  absl::Status ReadScanline(void* buffer, uint32_t row,
                            uint16_t sample = 0) const;

  /// @brief Get the size of a tile in bytes
  /// @return Tile size in bytes or error status
  /// @details Uses TIFFTileSize() to determine buffer size needed for
  /// ReadTile(). Only valid for tiled images.
  [[nodiscard]] absl::StatusOr<size_t> GetTileSize() const;

  /// @brief Get the size of a scanline in bytes
  /// @return Scanline size in bytes or error status
  /// @details Uses TIFFScanlineSize() to determine buffer size needed for
  /// ReadScanline().
  [[nodiscard]] absl::StatusOr<size_t> GetScanlineSize() const;

  /// @brief Read the entire current directory as RGBA
  /// @param buffer Pre-allocated RGBA buffer (width * height * 4 bytes)
  /// @param width Image width in pixels
  /// @param height Image height in pixels
  /// @param stop_on_error Whether to stop on first error or continue
  /// @return Status indicating success or failure
  /// @details Uses TIFFReadRGBAImage() to read the entire directory as RGBA
  /// data. The buffer must be pre-allocated with width * height * 4 bytes.
  absl::Status ReadRGBAImage(uint32_t* buffer, uint32_t width, uint32_t height,
                             bool stop_on_error = true) const;

  /// @brief Read a region from the current directory (both tiled and strip)
  /// @param buffer Pre-allocated buffer
  /// @param x X coordinate in pixels
  /// @param y Y coordinate in pixels
  /// @param width Region width in pixels
  /// @param height Region height in pixels
  /// @param bytes_per_pixel Bytes per pixel (from GetBytesPerPixel())
  /// @return Actual dimensions read and status
  /// @details Reads a rectangular region from the image, handling both tiled
  /// and strip-based images. The actual read dimensions may be smaller than
  /// requested if the region extends beyond image bounds.
  absl::StatusOr<TiffImageDimensions> ReadRegion(uint8_t* buffer, uint32_t x,
                                                 uint32_t y, uint32_t width,
                                                 uint32_t height,
                                                 size_t bytes_per_pixel) const;

  /// @brief Get access to the underlying TIFF handle (for advanced operations)
  /// @return Raw TIFF handle (use with caution)
  /// @warning This provides direct access to the underlying libtiff handle.
  /// Use with extreme caution as improper use can break the RAII guarantees.
  [[nodiscard]] TIFF* GetHandle() const;

  /// @brief Check if the file is valid and accessible
  /// @return True if valid, false if handle is invalid
  /// @details Checks if the underlying TIFF handle is valid and usable.
  [[nodiscard]] bool IsValid() const;

 private:
  /// @brief Private constructor - use Create() factory method
  /// @param handle_guard RAII guard for TIFF handle
  /// @details Private constructor ensures proper initialization through factory
  /// method.
  explicit TiffFile(TIFFHandleGuard handle_guard);

  /// @brief Helper to get a required field value
  /// @tparam T Field value type (uint16_t, uint32_t, double)
  /// @param tag TIFF tag identifier
  /// @param field_name Human-readable field name for error messages
  /// @return Field value or error status
  /// @details Template helper for type-safe field access with error handling.
  template <typename T>
  absl::StatusOr<T> GetRequiredField(ttag_t tag) const;

  /// @brief Helper to get an optional field value
  /// @tparam T Field value type (uint16_t, uint32_t, double)
  /// @param tag TIFF tag identifier
  /// @return Field value or nullopt if not present
  /// @details Template helper for optional field access without error on
  /// missing fields.
  template <typename T>
  std::optional<T> GetOptionalField(ttag_t tag) const;

  /// @brief Helper to get a string field
  /// @param tag TIFF tag identifier
  /// @return String value or empty string if not present
  /// @details Optimized string field access with length checking.
  [[nodiscard]] std::string GetStringField(ttag_t tag) const;

  /// @brief Copy tile data to output buffer with bounds checking
  /// @param tile_buffer Source tile buffer
  /// @param output_buffer Destination buffer
  /// @param tile_dims Tile dimensions
  /// @param tile_x Tile X coordinate
  /// @param tile_y Tile Y coordinate
  /// @param region_x Region X coordinate
  /// @param region_y Region Y coordinate
  /// @param region_dims Region dimensions
  /// @param bytes_per_pixel Bytes per pixel
  /// @return Status indicating success or failure
  /// @details Internal helper for copying partial tile data to output buffer.
  absl::Status CopyTileToBuffer(const uint8_t* tile_buffer,
                                uint8_t* output_buffer,
                                const TiffTileDimensions& tile_dims,
                                uint32_t tile_x, uint32_t tile_y,
                                uint32_t region_x, uint32_t region_y,
                                const TiffImageDimensions& region_dims,
                                size_t bytes_per_pixel) const;

  TIFFHandleGuard handle_guard_;  ///< RAII guard for TIFF handle
  uint16_t current_directory_;    ///< Current directory index
  mutable uint16_t
      cached_directory_count_;  ///< Cached directory count for performance
  mutable bool directory_count_cached_;  ///< Whether directory count is cached
};

/// @brief Convenience function to create TiffFile from handle pool
/// @param pool Handle pool to acquire handles from
/// @return TiffFile instance or error status
/// @details Simple wrapper around TiffFile::Create() for convenience.
inline absl::StatusOr<TiffFile> CreateTiffFile(TIFFHandlePool* pool) {
  return TiffFile::Create(pool);
}

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_TIFF_FILE_H_
