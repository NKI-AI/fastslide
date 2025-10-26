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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_PLANAR_INTERLEAVER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_PLANAR_INTERLEAVER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fastslide/utilities/tiff/tiff_file.h"
#include "fastslide/utilities/tile_cache_manager.h"

namespace fastslide {
namespace tiff {

/// @brief Callback function for reading data from a specific sample/plane
///
/// @param buffer Buffer to read data into
/// @param sample_idx Sample index (for planar separate configuration)
/// @return Status indicating success or failure
using SampleReadCallback =
    std::function<absl::Status(uint8_t* buffer, uint16_t sample_idx)>;

/// @brief Interleaves planar data based on TIFF planar configuration
///
/// This class handles the complexity of reading and interleaving planar data
/// from TIFF files, supporting both contiguous and separate planar configurations.
/// It abstracts the reading mechanism through callbacks, making it reusable
/// for both tiled and strip-based reading.
class PlanarInterleaver {
 public:
  /// @brief Configuration for planar interleaving
  struct Config {
    TiffPlanarConfig planar_config;  ///< TIFF planar configuration
    uint16_t samples_per_pixel;      ///< Number of samples per pixel
    uint32_t bytes_per_sample;       ///< Bytes per individual sample
    size_t data_size;                ///< Size of the data to be read
  };

  /// @brief Constructor
  /// @param config Interleaving configuration
  explicit PlanarInterleaver(const Config& config);

  /// @brief Read and interleave data using provided callback
  ///
  /// This method handles reading data from TIFF using the provided callback
  /// and performs any necessary interleaving based on the planar configuration.
  ///
  /// @param read_callback Function to read data for a specific sample
  /// @return Vector containing the interleaved data, or error status
  absl::StatusOr<std::vector<uint8_t>> ReadAndInterleave(
      const SampleReadCallback& read_callback) const;

 private:
  Config config_;
  std::vector<uint8_t> sample_buffer_;  ///< Temporary buffer for sample data
};

/// @brief Helper function for creating cached tile loaders with interleaving
///
/// This function creates a tile loader that handles caching and planar
/// interleaving for tiled TIFF data. It's designed to work with TileCacheManager.
///
/// @param tiff_file TiffFile wrapper for reading
/// @param tile_coords Tile coordinates to read
/// @param tile_dims Tile dimensions
/// @param config Planar interleaving configuration
/// @param bytes_per_pixel Total bytes per pixel (all samples)
/// @return Cached tile loader function
std::function<absl::StatusOr<std::shared_ptr<CachedTile>>()>
CreateTileLoaderWithInterleaving(TiffFile& tiff_file,
                                 const aifocore::Size<uint32_t, 2>& tile_coords,
                                 const aifocore::Size<uint32_t, 2>& tile_dims,
                                 const PlanarInterleaver::Config& config,
                                 uint32_t bytes_per_pixel);

/// @brief Helper function for creating cached scanline loaders with interleaving
///
/// This function creates a scanline loader that handles caching and planar
/// interleaving for strip-based TIFF data. It's designed to work with TileCacheManager.
///
/// @param tiff_file TiffFile wrapper for reading
/// @param row Row number to read
/// @param image_width Image width for scanline
/// @param config Planar interleaving configuration
/// @param bytes_per_pixel Total bytes per pixel (all samples)
/// @return Cached tile loader function (scanline treated as 1-row tile)
std::function<absl::StatusOr<std::shared_ptr<CachedTile>>()>
CreateScanlineLoaderWithInterleaving(TiffFile& tiff_file, uint32_t row,
                                     uint32_t image_width,
                                     const PlanarInterleaver::Config& config,
                                     uint32_t bytes_per_pixel);

}  // namespace tiff
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_TIFF_PLANAR_INTERLEAVER_H_
