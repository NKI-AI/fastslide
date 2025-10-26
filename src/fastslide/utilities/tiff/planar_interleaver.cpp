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

#include "fastslide/utilities/tiff/planar_interleaver.h"

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "aifocore/status/status_macros.h"
#include "fastslide/utilities/cache.h"

namespace fastslide {
namespace tiff {

PlanarInterleaver::PlanarInterleaver(const Config& config) : config_(config) {
  // Pre-allocate sample buffer for separate planar configuration
  if (config_.planar_config == TiffPlanarConfig::Separate) {
    sample_buffer_.resize(config_.data_size);
  }
}

absl::StatusOr<std::vector<uint8_t>> PlanarInterleaver::ReadAndInterleave(
    const SampleReadCallback& read_callback) const {

  std::vector<uint8_t> final_data;

  if (config_.planar_config == TiffPlanarConfig::Separate) {
    // Read each sample separately and interleave
    const size_t total_size = config_.data_size * config_.samples_per_pixel;
    final_data.resize(total_size);

    const uint32_t bytes_per_pixel =
        config_.bytes_per_sample * config_.samples_per_pixel;
    const size_t pixels_in_data = config_.data_size / config_.bytes_per_sample;

    for (uint16_t sample = 0; sample < config_.samples_per_pixel; ++sample) {
      // Read this sample's data
      RETURN_IF_ERROR(
          read_callback(const_cast<uint8_t*>(sample_buffer_.data()), sample),
          "Failed to read sample " + std::to_string(sample));

      // Interleave this sample's data into the final buffer
      for (size_t pixel = 0; pixel < pixels_in_data; ++pixel) {
        const size_t src_offset = pixel * config_.bytes_per_sample;
        const size_t dst_offset =
            pixel * bytes_per_pixel + sample * config_.bytes_per_sample;

        if (src_offset + config_.bytes_per_sample <= sample_buffer_.size() &&
            dst_offset + config_.bytes_per_sample <= final_data.size()) {
          std::memcpy(&final_data[dst_offset], &sample_buffer_[src_offset],
                      config_.bytes_per_sample);
        }
      }
    }
  } else {
    // Read interleaved data directly
    final_data.resize(config_.data_size);
    RETURN_IF_ERROR(read_callback(final_data.data(), 0),
                    "Failed to read interleaved data");
  }

  return final_data;
}

std::function<absl::StatusOr<std::shared_ptr<CachedTile>>()>
CreateTileLoaderWithInterleaving(TiffFile& tiff_file,
                                 const aifocore::Size<uint32_t, 2>& tile_coords,
                                 const aifocore::Size<uint32_t, 2>& tile_dims,
                                 const PlanarInterleaver::Config& config,
                                 uint32_t bytes_per_pixel) {

  return [&tiff_file, tile_coords, tile_dims, config,
          bytes_per_pixel]() -> absl::StatusOr<std::shared_ptr<CachedTile>> {
    PlanarInterleaver interleaver(config);

    // Create callback for reading tile data
    auto read_callback = [&tiff_file, tile_coords](
                             uint8_t* buffer,
                             uint16_t sample_idx) -> absl::Status {
      return tiff_file.ReadTile(buffer, tile_coords, sample_idx);
    };

    // Read and interleave the data
    auto data_result = interleaver.ReadAndInterleave(read_callback);
    if (!data_result.ok()) {
      return data_result.status();
    }

    return std::make_shared<CachedTile>(std::move(data_result.value()),
                                        tile_dims, bytes_per_pixel);
  };
}

std::function<absl::StatusOr<std::shared_ptr<CachedTile>>()>
CreateScanlineLoaderWithInterleaving(TiffFile& tiff_file, uint32_t row,
                                     uint32_t image_width,
                                     const PlanarInterleaver::Config& config,
                                     uint32_t bytes_per_pixel) {

  return [&tiff_file, row, image_width, config,
          bytes_per_pixel]() -> absl::StatusOr<std::shared_ptr<CachedTile>> {
    PlanarInterleaver interleaver(config);

    // Create callback for reading scanline data
    auto read_callback = [&tiff_file, row](
                             uint8_t* buffer,
                             uint16_t sample_idx) -> absl::Status {
      return tiff_file.ReadScanline(buffer, row, sample_idx);
    };

    // Read and interleave the data
    auto data_result = interleaver.ReadAndInterleave(read_callback);
    if (!data_result.ok()) {
      return data_result.status();
    }

    // Treat scanline as a tile with height = 1
    const aifocore::Size<uint32_t, 2> scanline_dims{image_width, 1};
    return std::make_shared<CachedTile>(std::move(data_result.value()),
                                        scanline_dims, bytes_per_pixel);
  };
}

}  // namespace tiff
}  // namespace fastslide
