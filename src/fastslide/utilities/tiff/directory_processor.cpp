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

#include "fastslide/utilities/tiff/directory_processor.h"

#include <string>
#include <utility>
#include "absl/log/log.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {

TiffDirectoryProcessor::TiffDirectoryProcessor(TiffFile tiff_file)
    : tiff_file_(std::move(tiff_file)) {}

absl::Status TiffDirectoryProcessor::ProcessAllDirectories(
    TiffDirectoryCallback& callback) {
  // Get directory count
  uint16_t dir_count = 0;
  ASSIGN_OR_RETURN(dir_count, GetDirectoryCount());

  // Process each directory
  for (uint16_t page = 0; page < dir_count; ++page) {
    auto dir_info_result = ExtractDirectoryInfo(page);
    if (!dir_info_result.ok()) {
      // TODO(jonasteuwen): This might create weird
      // problems if the directory is not found
      // Need to check if the rest will still work
      LOG(WARNING) << "Failed to extract directory info for page " << page
                   << ": " << dir_info_result.status().message();
      continue;  // Continue processing other directories
    }

    auto status = callback.ProcessDirectory(dir_info_result.value());
    if (!status.ok()) {
      LOG(WARNING) << "Failed to process directory " << page << ": "
                   << status.message();
      // Continue processing other directories
    }
  }

  // Finalize processing
  return callback.Finalize();
}

absl::StatusOr<uint16_t> TiffDirectoryProcessor::GetDirectoryCount() const {
  return tiff_file_.GetDirectoryCount();
}

absl::StatusOr<TiffDirectoryInfoWithPage>
TiffDirectoryProcessor::ExtractDirectoryInfo(uint16_t page) {
  // Set directory using type-safe method
  RETURN_IF_ERROR(tiff_file_.SetDirectory(page),
                  "Failed to set TIFF directory");

  TiffDirectoryInfo dir_info;

  // Extract basic image information
  ASSIGN_OR_RETURN(dir_info, ExtractBasicImageInfo(),
                   "Failed to extract basic image info");

  // Extract additional metadata
  ASSIGN_OR_RETURN(dir_info, ExtractMetadata(dir_info),
                   "Failed to extract metadata");

  TiffDirectoryInfoWithPage result;
  result.page = page;
  result.info = std::move(dir_info);

  return result;
}

absl::StatusOr<TiffDirectoryInfo>
TiffDirectoryProcessor::ExtractBasicImageInfo() {
  TiffDirectoryInfo dir_info;

  // Get image dimensions
  ASSIGN_OR_RETURN(dir_info.image_dims, tiff_file_.GetImageDimensions(),
                   "Failed to get image dimensions");

  // Validate dimensions
  if (dir_info.image_dims[0] == 0 || dir_info.image_dims[1] == 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid image dimensions: {}x{}",
                              dir_info.image_dims[0], dir_info.image_dims[1]));
  }

  // Get other directory information
  ASSIGN_OR_RETURN(dir_info.subfile_type, tiff_file_.GetSubfileType(),
                   "Failed to get subfile type");

  // Check if tiled
  dir_info.is_tiled = tiff_file_.IsTiled();

  // Get tile dimensions if tiled
  if (dir_info.is_tiled) {
    ASSIGN_OR_RETURN(dir_info.tile_dims, tiff_file_.GetTileDimensions(),
                     "Failed to get tile dimensions");
  }

  return dir_info;
}

absl::StatusOr<TiffDirectoryInfo> TiffDirectoryProcessor::ExtractMetadata(
    TiffDirectoryInfo dir_info) {
  // Get image description
  std::string desc = tiff_file_.GetImageDescription();
  if (!desc.empty()) {
    dir_info.image_description = desc;
  }

  // Get pixel format information
  ASSIGN_OR_RETURN(dir_info.samples_per_pixel, tiff_file_.GetSamplesPerPixel());
  ASSIGN_OR_RETURN(dir_info.bits_per_sample, tiff_file_.GetBitsPerSample());
  ASSIGN_OR_RETURN(dir_info.photometric, tiff_file_.GetPhotometric());
  ASSIGN_OR_RETURN(dir_info.compression, tiff_file_.GetCompression());
  ASSIGN_OR_RETURN(dir_info.sample_format, tiff_file_.GetSampleFormat());
  ASSIGN_OR_RETURN(dir_info.planar_config, tiff_file_.GetPlanarConfig());

  // Get resolution info
  dir_info.resolution_unit = tiff_file_.GetResolutionUnit();
  dir_info.x_resolution = tiff_file_.GetXResolution();
  dir_info.y_resolution = tiff_file_.GetYResolution();

  // Get software info
  std::string software = tiff_file_.GetSoftware();
  if (!software.empty()) {
    dir_info.software = software;
  }

  return dir_info;
}

}  // namespace fastslide
