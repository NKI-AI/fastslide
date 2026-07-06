// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

/// @file mrxs_position_reader.cpp
/// @brief Implementation of MRXS camera position / gain parsing.
///
/// FORMAT SPECIFICATION (from OpenSlide documentation):
/// https://lists.andrew.cmu.edu/pipermail/openslide-users/2012-July/000373.html
/// (step 11)
///
/// Position buffer structure (per camera position, 9 bytes each):
///   - flag: uint8 (typically 0 or 1, meaning unknown)
///   - x: int32 (little-endian, level 0 pixel coordinate)
///   - y: int32 (little-endian, level 0 pixel coordinate)
///
/// Total size = 9 * (images_x / image_divisions) * (images_y / image_divisions)
///
/// Storage locations (non-hierarchical records):
///   1. VIMSLIDE_POSITION_BUFFER (older slides): uncompressed raw position data
///   2. StitchingIntensityLayer (newer slides): DEFLATE/zlib compressed
///      (magic `0x78 0x9C`)
///
/// Position scaling: positions are stored for the smallest tile unit; we
/// multiply by `2^level0.downsample_exponent` to get level 0 coordinates.

#include "fastslide/readers/mrxs/mrxs_position_reader.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/mrxs/mrxs_data_reader.h"
#include "fastslide/runtime/io/binary_utils.h"
#include "fastslide/runtime/io/file_reader.h"

namespace fastslide {
namespace mrxs {
namespace {

// Byte-swap a 32-bit value. MRXS stores coordinates little-endian; this is only
// applied on big-endian hosts. std::byteswap is C++23, so swap by hand.
constexpr uint32_t ByteSwap32(uint32_t v) {
  return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
         ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

void LogGainMetadataSizeMismatchOnce(size_t expected_size, size_t actual_size) {
  static std::once_flag once_flag;
  std::call_once(once_flag, [expected_size, actual_size]() {
    std::cerr << "MRXS: gain metadata size mismatch (expected " << expected_size
              << " bytes, got " << actual_size
              << "); ignoring gain metadata and using gain=1.0\n";
  });
}

/// @brief Compute the number of camera positions for a slide.
constexpr int NumCameraPositions(const SlideDataInfo& slide_info) {
  const int positions_x = slide_info.images_x / slide_info.image_divisions;
  const int positions_y = slide_info.images_y / slide_info.image_divisions;
  return positions_x * positions_y;
}

/// @brief Parse `npositions` little-endian floats into `gains`.
///
/// Logs (once) and clears `gains` if @p data does not have exactly
/// `4 * npositions` bytes.
void ParseGainBuffer(const uint8_t* data, size_t data_size, int npositions,
                     std::vector<float>& gains) {
  const size_t expected_size = static_cast<size_t>(4) * npositions;
  if (data_size != expected_size) {
    LogGainMetadataSizeMismatchOnce(expected_size, data_size);
    gains.clear();
    return;
  }

  gains.clear();
  gains.reserve(static_cast<size_t>(npositions));
  for (int i = 0; i < npositions; ++i) {
    float gain;
    std::memcpy(&gain, data + (static_cast<size_t>(i) * sizeof(float)),
                sizeof(float));
    if constexpr (std::endian::native == std::endian::big) {
      uint32_t temp;
      std::memcpy(&temp, data + (static_cast<size_t>(i) * sizeof(uint32_t)),
                  sizeof(uint32_t));
      temp = ByteSwap32(temp);
      std::memcpy(&gain, &temp, sizeof(float));
    }
    gains.push_back(gain);
  }
}

/// @brief Read and (optionally) decode the per-position gain metadata.
///
/// `compressed_metadata` is the raw payload of the second item in the
/// position non-hier data page. If it carries a zlib magic (0x78 0x9C) we
/// decompress first; otherwise we treat it as raw float buffer.
aifocore::Status ParseGainMetadata(
    const std::vector<uint8_t>& compressed_metadata,
    const SlideDataInfo& slide_info, std::vector<float>& gains) {
  const int npositions = NumCameraPositions(slide_info);

  if (compressed_metadata.size() >= 2 && compressed_metadata[0] == 0x78 &&
      compressed_metadata[1] == 0x9C) {
    const int expected_size = 4 * npositions;
    AIFOCORE_ASSIGN_OR_RETURN(
        runtime::io::ZlibDecompressionResult decompressed,
        DecompressZlibWithActualSize(compressed_metadata.data(),
                                     compressed_metadata.size(),
                                     /*expected_size_hint=*/expected_size));
    ParseGainBuffer(decompressed.data.data(), decompressed.actual_size_bytes,
                    npositions, gains);
  } else {
    ParseGainBuffer(compressed_metadata.data(), compressed_metadata.size(),
                    npositions, gains);
  }
  return aifocore::Status::OkStatus();
}

/// @brief Decompress a zlib-wrapped position buffer to its expected size.
///
/// 3DHISTECH stitching layers use a fixed three-byte zlib prefix
/// `78 9C ED`. If the prefix matches we decompress and right-pad / clamp
/// to `expected_size`. Otherwise the buffer is already raw and is returned
/// unchanged.
aifocore::Result<std::vector<uint8_t>> MaybeDecompressPositionData(
    std::vector<uint8_t> compressed_data, int expected_size,
    bool is_stitching_layer) {
  if (!is_stitching_layer) {
    return compressed_data;
  }
  const bool has_zlib_prefix =
      compressed_data.size() >= 3 && compressed_data[0] == 0x78 &&
      compressed_data[1] == 0x9C && compressed_data[2] == 0xED;
  if (!has_zlib_prefix) {
    return compressed_data;
  }

  AIFOCORE_ASSIGN_OR_RETURN(runtime::io::ZlibDecompressionResult decompressed,
                            DecompressZlibWithActualSize(
                                compressed_data.data(), compressed_data.size(),
                                /*expected_size_hint=*/expected_size));
  if (decompressed.actual_size_bytes > static_cast<size_t>(expected_size)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kResourceExhausted,
        aifocore::fmt::format(
            "Position buffer decompressed to {} bytes, expected at most {} "
            "bytes",
            decompressed.actual_size_bytes, expected_size));
  }
  std::vector<uint8_t> result = std::move(decompressed.data);
  // Preserve old behavior: caller expects exactly `expected_size` bytes.
  result.resize(static_cast<size_t>(expected_size), 0);
  return result;
}

/// @brief Parse the 9-byte-per-position layout into level-0 (x, y) pairs.
void ParsePositionBuffer(const std::vector<uint8_t>& position_data,
                         int npositions, int level_0_concat,
                         std::vector<int32_t>& camera_positions) {
  camera_positions.clear();
  camera_positions.reserve(static_cast<size_t>(npositions) * 2);

  const uint8_t* p = position_data.data();
  for (int i = 0; i < npositions; ++i) {
    const uint8_t flag = *p++;
    if (flag & 0xFE) {
      std::cerr << "Unexpected flag value in position buffer: "
                << static_cast<int>(flag);
    }

    int32_t x;
    int32_t y;
    std::memcpy(&x, p, sizeof(x));
    p += sizeof(x);
    std::memcpy(&y, p, sizeof(y));
    p += sizeof(y);

    if constexpr (std::endian::native == std::endian::big) {
      x = static_cast<int32_t>(ByteSwap32(static_cast<uint32_t>(x)));
      y = static_cast<int32_t>(ByteSwap32(static_cast<uint32_t>(y)));
    }

    camera_positions.push_back(x * level_0_concat);
    camera_positions.push_back(y * level_0_concat);
  }
}

/// @brief Compute level-0 concatenation factor for position scaling.
///
/// Returns 1 when `zoom_levels` is empty (defensive; the INI parser already
/// guarantees a non-empty list on success).
int LevelZeroConcatFactor(const SlideDataInfo& slide_info) {
  if (slide_info.zoom_levels.empty()) {
    return 1;
  }
  return 1 << slide_info.zoom_levels[0].downsample_exponent;
}

}  // namespace

aifocore::Status MrxsPositionReader::Read(const fs::path& dirname,
                                          SlideDataInfo& slide_info) {
  // Check if position data is available (determined during INI parsing)
  if (slide_info.using_synthetic_positions ||
      slide_info.position_layer_record_offset == -1) {
    return aifocore::Status::OkStatus();
  }

  const int position_record = slide_info.position_layer_record_offset;
  const bool is_stitching_layer = slide_info.position_layer_compressed;

  fs::path index_path = dirname / slide_info.index_filename;
  FileReader indexfile;
  AIFOCORE_ASSIGN_OR_RETURN(indexfile, FileReader::Open(index_path, "rb"));

  // Walk the index header to find the position record's data page.
  // `nonhier_root` lives 4 bytes after `hier_root` per the MRXS layout.
  const int64_t hier_root =
      static_cast<int64_t>(strlen("01.02") + slide_info.slide_id.length());
  const int64_t nonhier_root = hier_root + 4;

  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(nonhier_root));

  int32_t ptr_32;
  AIFOCORE_ASSIGN_OR_RETURN(ptr_32, ReadLeInt32(indexfile.Get()));
  const int64_t ptr = ptr_32;

  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(ptr + 4 * position_record));

  int32_t record_ptr_32;
  AIFOCORE_ASSIGN_OR_RETURN(record_ptr_32, ReadLeInt32(indexfile.Get()));
  const int64_t record_ptr = record_ptr_32;

  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(record_ptr));

  int32_t zero_value_32;
  AIFOCORE_ASSIGN_OR_RETURN(zero_value_32, ReadLeInt32(indexfile.Get()));
  if (zero_value_32 != 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Expected 0 at beginning of nonhier record, got {}",
            zero_value_32));
  }

  int32_t data_ptr_32;
  AIFOCORE_ASSIGN_OR_RETURN(data_ptr_32, ReadLeInt32(indexfile.Get()));
  AIFOCORE_RETURN_IF_ERROR(indexfile.Seek(data_ptr_32));

  int32_t page_len_32;
  AIFOCORE_ASSIGN_OR_RETURN(page_len_32, ReadLeInt32(indexfile.Get()));
  const int64_t page_len = page_len_32;

  if (page_len < 1) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Expected at least one data item in position data page");
  }

  // Skip next pointer and two zeros.
  for (int i = 0; i < 3; ++i) {
    int32_t skip_32;
    AIFOCORE_ASSIGN_OR_RETURN(skip_32, ReadLeInt32(indexfile.Get()));
    (void)skip_32;
  }

  int32_t offset_32;
  int32_t size_32;
  int32_t fileno_32;
  AIFOCORE_ASSIGN_OR_RETURN(offset_32, ReadLeInt32(indexfile.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(size_32, ReadLeInt32(indexfile.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(fileno_32, ReadLeInt32(indexfile.Get()));
  const int64_t offset = offset_32;
  const int64_t size = size_32;
  const int64_t fileno = fileno_32;

  if (fileno < 0 ||
      fileno >= static_cast<int>(slide_info.datafile_paths.size())) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid datafile number: {} (must be 0-{})",
                              fileno, slide_info.datafile_paths.size() - 1));
  }

  fs::path datafile_path = dirname / slide_info.datafile_paths[fileno];

  // Optional second item: per-position gain metadata (MRXS slides ≥ v2.2).
  if (page_len >= 2) {
    for (int i = 0; i < 2; ++i) {
      int32_t reserved_32;
      AIFOCORE_ASSIGN_OR_RETURN(reserved_32, ReadLeInt32(indexfile.Get()));
      (void)reserved_32;
    }

    int32_t offset2_32;
    int32_t size2_32;
    int32_t fileno2_32;
    AIFOCORE_ASSIGN_OR_RETURN(offset2_32, ReadLeInt32(indexfile.Get()));
    AIFOCORE_ASSIGN_OR_RETURN(size2_32, ReadLeInt32(indexfile.Get()));
    AIFOCORE_ASSIGN_OR_RETURN(fileno2_32, ReadLeInt32(indexfile.Get()));

    if (fileno2_32 < 0 ||
        fileno2_32 >= static_cast<int>(slide_info.datafile_paths.size())) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("Invalid gain metadata file number: {}",
                                fileno2_32));
    }

    fs::path datafile2_path = dirname / slide_info.datafile_paths[fileno2_32];
    std::vector<uint8_t> compressed_metadata;
    AIFOCORE_ASSIGN_OR_RETURN(
        compressed_metadata,
        MrxsDataReader::ReadData(datafile2_path, offset2_32, size2_32));
    AIFOCORE_RETURN_IF_ERROR(ParseGainMetadata(
        compressed_metadata, slide_info, slide_info.camera_position_gains));
  }

  // Read and (optionally) decompress the position buffer itself.
  std::vector<uint8_t> compressed_data;
  AIFOCORE_ASSIGN_OR_RETURN(
      compressed_data, MrxsDataReader::ReadData(datafile_path, offset, size));

  const int npositions = NumCameraPositions(slide_info);
  const int expected_size = 9 * npositions;  // 9 bytes per position

  std::vector<uint8_t> position_data;
  AIFOCORE_ASSIGN_OR_RETURN(
      position_data,
      MaybeDecompressPositionData(std::move(compressed_data), expected_size,
                                  is_stitching_layer));

  if (position_data.size() != static_cast<size_t>(expected_size)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "Position buffer size mismatch. Expected {}, got {}", expected_size,
            position_data.size()));
  }

  ParsePositionBuffer(position_data, npositions,
                      LevelZeroConcatFactor(slide_info),
                      slide_info.camera_positions);

  slide_info.using_synthetic_positions = false;
  return aifocore::Status::OkStatus();
}

}  // namespace mrxs
}  // namespace fastslide
