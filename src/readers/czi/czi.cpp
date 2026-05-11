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

#include "fastslide/readers/czi/czi.h"

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zstd.h>
#include <pugixml.hpp>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/czi/czi_embedded.h"
#include "fastslide/readers/czi/czi_exec_context.h"
#include "fastslide/readers/czi/czi_plan_builder.h"
#include "fastslide/readers/czi/czi_plan_context.h"
#include "fastslide/readers/czi/czi_tile_executor.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/runtime/io/ascii_utils.h"
#include "fastslide/runtime/io/binary_utils.h"
#include "fastslide/runtime/io/file_reader.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

namespace {

using runtime::io::ReadFixedAscii;
using runtime::io::StartsWithMagic;

constexpr std::string_view kCziExt = ".czi";
constexpr std::string_view kSidZisRawFile = "ZISRAWFILE";
constexpr std::string_view kSidZisRawDirectory = "ZISRAWDIRECTORY";
constexpr std::string_view kSidZisRawMetadata = "ZISRAWMETADATA";
constexpr std::string_view kSidZisRawAttDir = "ZISRAWATTDIR";
constexpr std::string_view kSidZisRawAttach = "ZISRAWATTACH";

constexpr size_t kCziAttachmentHdrLen = 288;

constexpr std::string_view kAttachmentLabel = "Label";
constexpr std::string_view kAttachmentSlidePreview = "SlidePreview";
constexpr std::string_view kAttachmentThumbnail = "Thumbnail";

int32_t ComputeDownsampleFactor(int32_t full_size, int32_t stored_size) {
  // CZI directory entries provide both the logical size and stored size for a
  // dimension. A practical downsample estimate is full_size / stored_size,
  // rounded to the nearest integer.
  if (stored_size <= 0) {
    return 1;
  }
  const double ratio =
      static_cast<double>(full_size) / static_cast<double>(stored_size);
  const auto rounded = static_cast<int64_t>(std::llround(ratio));
  if (rounded <= 0) {
    return 1;
  }
  if (rounded > std::numeric_limits<int32_t>::max()) {
    return std::numeric_limits<int32_t>::max();
  }
  return static_cast<int32_t>(rounded);
}

}  // namespace

CziReader::CziReader(std::string filename) : filename_(std::move(filename)) {}

aifocore::Status CziReader::ValidateInput(const fs::path& filename) {
  if (filename.extension() != kCziExt) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("File does not have {} extension", kCziExt));
  }
  if (!fs::exists(filename)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("File does not exist: {}", filename.string()));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Result<std::unique_ptr<CziReader>> CziReader::CreateReaderImpl(
    const fs::path& filename) {
  auto reader = std::unique_ptr<CziReader>(new CziReader(filename.string()));
  AIFOCORE_RETURN_IF_ERROR(reader->Initialize());
  return reader;
}

aifocore::Status CziReader::Initialize() {
  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(filename_, "rb"));

  AIFOCORE_RETURN_IF_ERROR(ParseFileHeader(file));
  AIFOCORE_RETURN_IF_ERROR(ParseSubblockDirectory(file));
  (void)ParseMetadataXml(file);          // optional
  (void)ParseAttachmentDirectory(file);  // optional

  FinalizeDerivedState();
  return aifocore::Status::OkStatus();
}

aifocore::Status CziReader::ParseFileHeader(FileReader& file) {
  AIFOCORE_RETURN_IF_ERROR(file.Seek(0));

  // CZI file header segment (ZISRAWFILE).
  // On-disk layout (little-endian):
  // - sid[16]
  // - allocated_size (int64), used_size (int64)
  // - major (int32), minor (int32), reserved1 (int32), reserved2 (int32)
  // - primary_file_guid[16], file_guid[16]
  // - file_part (int32)
  // - subblk_dir_pos (int64), meta_pos (int64), update_pending (int32),
  // att_dir_pos (int64)
  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawFile)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Bad magic: expected {}, got {}", kSidZisRawFile,
                              sid));
  }

  // allocated_size, used_size (unused)
  (void)ReadLeInt64(file.Get());
  (void)ReadLeInt64(file.Get());
  // major/minor/reserved
  (void)ReadLeInt32(file.Get());
  (void)ReadLeInt32(file.Get());
  (void)ReadLeInt32(file.Get());
  (void)ReadLeInt32(file.Get());
  // GUIDs (unused)
  char guid1[16];
  char guid2[16];
  AIFOCORE_RETURN_IF_ERROR(file.Read(guid1, sizeof(guid1)));
  AIFOCORE_RETURN_IF_ERROR(file.Read(guid2, sizeof(guid2)));
  (void)ReadLeInt32(file.Get());  // file_part

  AIFOCORE_ASSIGN_OR_RETURN(subblk_dir_pos_, ReadLeInt64(file.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(meta_pos_, ReadLeInt64(file.Get()));
  (void)ReadLeInt32(file.Get());  // update_pending
  AIFOCORE_ASSIGN_OR_RETURN(att_dir_pos_, ReadLeInt64(file.Get()));

  return aifocore::Status::OkStatus();
}

aifocore::Status CziReader::ParseSubblockDirectory(FileReader& file) {
  if (subblk_dir_pos_ == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Missing CZI subblock directory");
  }
  AIFOCORE_RETURN_IF_ERROR(file.Seek(subblk_dir_pos_));

  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawDirectory)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Bad magic: expected {}, got {}",
                              kSidZisRawDirectory, sid));
  }

  int64_t allocated_size = 0;
  int64_t used_size = 0;
  int32_t entry_count = 0;
  AIFOCORE_ASSIGN_OR_RETURN(allocated_size, ReadLeInt64(file.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(used_size, ReadLeInt64(file.Get()));
  AIFOCORE_ASSIGN_OR_RETURN(entry_count, ReadLeInt32(file.Get()));
  (void)allocated_size;
  // reserved 124 bytes
  char reserved[124];
  AIFOCORE_RETURN_IF_ERROR(file.Read(reserved, sizeof(reserved)));

  const int64_t header_size = 16 + 8 + 8 + 4 + 124;
  const int64_t seg_hdr_size = 16 + 8 + 8;
  const int64_t seg_size = used_size - header_size + seg_hdr_size;
  if (seg_size < 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid directory used_size={}", used_size));
  }

  std::vector<uint8_t> buf(static_cast<size_t>(seg_size));
  if (!buf.empty()) {
    AIFOCORE_RETURN_IF_ERROR(file.Read(buf.data(), buf.size()));
  }

  size_t p = 0;
  auto require = [&](size_t n) -> aifocore::Status {
    if (p + n > buf.size()) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Premature end of directory buffer");
    }
    return aifocore::Status::OkStatus();
  };

  subblocks_.clear();
  subblocks_.reserve(static_cast<size_t>(std::max(0, entry_count)));

  for (int i = 0; i < entry_count; ++i) {
    // Directory entry (schema "DV").
    // Fixed fields:
    // - schema[2]
    // - pixel_type (int32)
    // - file_pos (int64)
    // - file_part (int32)
    // - compression (int32)
    // - pyramid_type (int8)
    // - reserved1 (int8)
    // - reserved2[4]
    // - ndimensions (int32)
    // Followed by ndimensions DimensionEntryDV records.
    AIFOCORE_RETURN_IF_ERROR(require(2));
    const std::string schema(reinterpret_cast<const char*>(buf.data() + p), 2);
    p += 2;
    if (schema != "DV") {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Unexpected CZI directory entry schema");
    }
    AIFOCORE_RETURN_IF_ERROR(require(4 + 8 + 4 + 4 + 1 + 1 + 4 + 4));

    int32_t pixel_type = 0;
    std::memcpy(&pixel_type, buf.data() + p, sizeof(pixel_type));
    p += 4;

    int64_t file_pos = 0;
    std::memcpy(&file_pos, buf.data() + p, sizeof(file_pos));
    p += 8;

    int32_t file_part = 0;
    std::memcpy(&file_part, buf.data() + p, sizeof(file_part));
    p += 4;
    (void)file_part;

    int32_t compression = 0;
    std::memcpy(&compression, buf.data() + p, sizeof(compression));
    p += 4;

    int8_t pyramid_type = 0;
    std::memcpy(&pyramid_type, buf.data() + p, sizeof(pyramid_type));
    p += 1;
    // reserved1/reserved2
    p += 1;  // reserved1
    p += 4;  // reserved2

    int32_t ndimensions = 0;
    std::memcpy(&ndimensions, buf.data() + p, sizeof(ndimensions));
    p += 4;

    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    int32_t scene = 0;
    int32_t channel = 0;
    int32_t z_index = 0;
    int32_t downsample = 1;

    for (int d = 0; d < ndimensions; ++d) {
      // Dimension entry (little-endian):
      // - dimension[4] (ASCII, e.g. "X", "Y", "S", "C", "M")
      // - start (int32), size (int32), start_coordinate (float32), stored_size
      // (int32)
      AIFOCORE_RETURN_IF_ERROR(require(4 + 4 + 4 + 4 + 4));
      char dim_raw[4];
      std::memcpy(dim_raw, buf.data() + p, sizeof(dim_raw));
      p += 4;
      const std::string dim_name = ReadFixedAscii(dim_raw, sizeof(dim_raw));

      int32_t start = 0;
      int32_t size0 = 0;
      float start_coord = 0.0f;
      int32_t stored_size = 0;
      std::memcpy(&start, buf.data() + p, sizeof(start));
      p += 4;
      std::memcpy(&size0, buf.data() + p, sizeof(size0));
      p += 4;
      std::memcpy(&start_coord, buf.data() + p, sizeof(start_coord));
      p += 4;
      std::memcpy(&stored_size, buf.data() + p, sizeof(stored_size));
      p += 4;
      (void)start_coord;

      if (dim_name == "X") {
        x = start;
        w = static_cast<uint32_t>(std::max(0, stored_size));
        downsample = ComputeDownsampleFactor(size0, stored_size);
      } else if (dim_name == "Y") {
        y = start;
        h = static_cast<uint32_t>(std::max(0, stored_size));
      } else if (dim_name == "S") {
        scene = start;
      } else if (dim_name == "C") {
        channel = start;
      } else if (dim_name == "M") {
        z_index = start;
      } else {
        return AIFOCORE_MAKE_STATUS(
            aifocore::StatusCode::kInvalidArgument,
            aifocore::fmt::format("Unrecognized subblock dimension: {}",
                                  dim_name));
      }
    }

    if (w == 0 || h == 0) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          "Missing X/Y dimension in CZI subblock entry");
    }

    Subblock sb{};
    sb.index = static_cast<uint32_t>(subblocks_.size());
    sb.file_pos = file_pos;
    sb.pixel_type = pixel_type;
    sb.compression = compression;
    sb.pyramid_type = pyramid_type;
    sb.x = x;
    sb.y = y;
    sb.w = w;
    sb.h = h;
    sb.scene = scene;
    sb.channel = channel;
    sb.z_index = z_index;
    sb.downsample = downsample;
    subblocks_.push_back(sb);
  }

  // Normalize origin
  int32_t min_x = std::numeric_limits<int32_t>::max();
  int32_t min_y = std::numeric_limits<int32_t>::max();
  for (const auto& sb : subblocks_) {
    min_x = std::min(min_x, sb.x);
    min_y = std::min(min_y, sb.y);
  }
  if (min_x != 0 || min_y != 0) {
    for (auto& sb : subblocks_) {
      sb.x -= min_x;
      sb.y -= min_y;
    }
  }

  // Compute bounds from downsample==1 tiles (or all tiles as fallback).
  bool have_l0 = false;
  int32_t bx = std::numeric_limits<int32_t>::max();
  int32_t by = std::numeric_limits<int32_t>::max();
  int32_t bx2 = std::numeric_limits<int32_t>::lowest();
  int32_t by2 = std::numeric_limits<int32_t>::lowest();
  for (const auto& sb : subblocks_) {
    if (sb.downsample == 1) {
      have_l0 = true;
      bx = std::min(bx, sb.x);
      by = std::min(by, sb.y);
      bx2 = std::max<int32_t>(bx2, sb.x + static_cast<int32_t>(sb.w));
      by2 = std::max<int32_t>(by2, sb.y + static_cast<int32_t>(sb.h));
    }
  }
  if (!have_l0) {
    for (const auto& sb : subblocks_) {
      bx = std::min(bx, sb.x);
      by = std::min(by, sb.y);
      bx2 = std::max<int32_t>(bx2, sb.x + static_cast<int32_t>(sb.w));
      by2 = std::max<int32_t>(by2, sb.y + static_cast<int32_t>(sb.h));
    }
  }
  bounds_l0_ = SlideBounds(bx, by, std::max<int32_t>(0, bx2 - bx),
                           std::max<int32_t>(0, by2 - by));

  // Build level list.
  std::set<int32_t> ds_set;
  for (const auto& sb : subblocks_) {
    if (sb.downsample > 0) {
      ds_set.insert(sb.downsample);
    }
  }
  downsamples_.assign(ds_set.begin(), ds_set.end());
  if (downsamples_.empty()) {
    downsamples_.push_back(1);
  }

  // Group subblocks by level.
  level_subblocks_.assign(downsamples_.size(), {});
  for (const auto& sb : subblocks_) {
    const auto it = std::lower_bound(downsamples_.begin(), downsamples_.end(),
                                     sb.downsample);
    if (it == downsamples_.end() || *it != sb.downsample) {
      continue;
    }
    const size_t level = static_cast<size_t>(it - downsamples_.begin());
    level_subblocks_[level].push_back(sb.index);
  }

  // Resize spatial index cache.
  spatial_indices_.assign(downsamples_.size(), nullptr);

  return aifocore::Status::OkStatus();
}

aifocore::Status CziReader::ParseMetadataXml(FileReader& file) {
  if (meta_pos_ == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No metadata segment");
  }
  AIFOCORE_RETURN_IF_ERROR(file.Seek(meta_pos_));

  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawMetadata)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Bad magic: expected {}, got {}",
                              kSidZisRawMetadata, sid));
  }

  (void)ReadLeInt64(file.Get());  // allocated_size
  (void)ReadLeInt64(file.Get());  // used_size
  int32_t xml_size = 0;
  AIFOCORE_ASSIGN_OR_RETURN(xml_size, ReadLeInt32(file.Get()));
  (void)ReadLeInt32(file.Get());  // attach_size
  char reserved[248];
  AIFOCORE_RETURN_IF_ERROR(file.Read(reserved, sizeof(reserved)));

  if (xml_size <= 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid metadata XML size");
  }

  std::vector<char> xml_buf(static_cast<size_t>(xml_size));
  AIFOCORE_RETURN_IF_ERROR(file.Read(xml_buf.data(), xml_buf.size()));
  metadata_xml_.assign(xml_buf.begin(), xml_buf.end());

  // Parse minimal metadata fields via pugixml (robust, no exceptions).
  pugi::xml_document doc;
  pugi::xml_parse_result ok =
      doc.load_buffer(metadata_xml_.data(), metadata_xml_.size());
  if (!ok) {
    return aifocore::Status::OkStatus();  // Metadata parsing is optional.
  }

  pugi::xml_node root = doc.document_element();
  if (std::string_view(root.name()) != "ImageDocument") {
    auto alt = doc.child("ImageDocument");
    if (alt) {
      root = alt;
    }
  }

  // SizeX/SizeY.
  auto size_x_node =
      root.select_node("./Metadata/Information/Image/SizeX").node();
  auto size_y_node =
      root.select_node("./Metadata/Information/Image/SizeY").node();
  if (size_x_node && size_y_node) {
    const uint32_t sx =
        static_cast<uint32_t>(std::max(0, size_x_node.text().as_int(-1)));
    const uint32_t sy =
        static_cast<uint32_t>(std::max(0, size_y_node.text().as_int(-1)));
    if (sx > 0 && sy > 0) {
      metadata_size_l0_ = ImageDimensions{sx, sy};
    }
  }

  // Scaling distance in meters per pixel -> microns per pixel.
  auto x_val =
      root.select_node(".//Metadata/Scaling/Items/Distance[@Id='X']/Value")
          .node();
  auto y_val =
      root.select_node(".//Metadata/Scaling/Items/Distance[@Id='Y']/Value")
          .node();
  if (x_val && y_val) {
    const double mx = x_val.text().as_double(0.0);
    const double my = y_val.text().as_double(0.0);
    if (mx > 0.0 && my > 0.0) {
      metadata_mpp_ = std::make_pair(mx * 1'000'000.0, my * 1'000'000.0);
    }
  }

  // Objective magnification.
  auto obj_ref =
      root.select_node(
              ".//Metadata/Information/Image/ObjectiveSettings/ObjectiveRef")
          .node();
  if (obj_ref) {
    const char* obj_id = obj_ref.attribute("Id").value();
    if (obj_id != nullptr && obj_id[0] != '\0') {
      std::string query = aifocore::fmt::format(
          ".//Metadata/Information/Instrument/Objectives/Objective[@Id='{}']"
          "/NominalMagnification",
          obj_id);
      auto mag_node = root.select_node(query.c_str()).node();
      if (mag_node) {
        const double mag = mag_node.text().as_double(0.0);
        if (mag > 0.0) {
          metadata_objective_magnification_ = mag;
        }
      }
    }
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status CziReader::ParseAttachmentDirectory(FileReader& file) {
  if (att_dir_pos_ == 0) {
    return aifocore::Status::OkStatus();
  }
  AIFOCORE_RETURN_IF_ERROR(file.Seek(att_dir_pos_));

  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawAttDir)) {
    return aifocore::Status::OkStatus();
  }

  (void)ReadLeInt64(file.Get());  // alloc
  (void)ReadLeInt64(file.Get());  // used
  int32_t entry_count = 0;
  AIFOCORE_ASSIGN_OR_RETURN(entry_count, ReadLeInt32(file.Get()));
  char reserved[252];
  AIFOCORE_RETURN_IF_ERROR(file.Read(reserved, sizeof(reserved)));

  attachments_.clear();
  attachments_.reserve(static_cast<size_t>(std::max(0, entry_count)));

  for (int i = 0; i < entry_count; ++i) {
    // Attachment directory entry (schema "A1").
    // Layout (little-endian):
    // - schema[2]
    // - reserved[10]
    // - file_pos (int64)
    // - file_part (int32)
    // - guid[16]
    // - file_type[8]  (ASCII, e.g. "JPG", "CZI")
    // - name[80]      (ASCII, e.g. "Label", "SlidePreview", "Thumbnail")
    char schema_raw[2];
    AIFOCORE_RETURN_IF_ERROR(file.Read(schema_raw, sizeof(schema_raw)));
    const std::string schema(schema_raw, schema_raw + 2);
    if (schema != "A1") {
      return aifocore::Status::OkStatus();
    }
    char r2[10];
    AIFOCORE_RETURN_IF_ERROR(file.Read(r2, sizeof(r2)));
    int64_t file_pos = 0;
    AIFOCORE_ASSIGN_OR_RETURN(file_pos, ReadLeInt64(file.Get()));
    (void)ReadLeInt32(file.Get());  // file_part
    char guid[16];
    AIFOCORE_RETURN_IF_ERROR(file.Read(guid, sizeof(guid)));
    char file_type_raw[8];
    AIFOCORE_RETURN_IF_ERROR(file.Read(file_type_raw, sizeof(file_type_raw)));
    char name_raw[80];
    AIFOCORE_RETURN_IF_ERROR(file.Read(name_raw, sizeof(name_raw)));

    AttachmentInfo info{};
    info.file_pos = file_pos;
    info.file_type = ReadFixedAscii(file_type_raw, sizeof(file_type_raw));
    info.name = ReadFixedAscii(name_raw, sizeof(name_raw));
    info.data_offset = static_cast<uint32_t>(kCziAttachmentHdrLen);
    attachments_.push_back(std::move(info));
  }

  return aifocore::Status::OkStatus();
}

void CziReader::FinalizeDerivedState() {
  // Determine base dimensions (level-0).
  uint32_t base_w = 0;
  uint32_t base_h = 0;
  if (metadata_size_l0_.has_value()) {
    base_w = (*metadata_size_l0_)[0];
    base_h = (*metadata_size_l0_)[1];
  } else {
    // If metadata doesn't provide the full size, fall back to the maximal
    // extent implied by the content bounds.
    base_w = static_cast<uint32_t>(
        std::max<int64_t>(0, bounds_l0_.x + bounds_l0_.width));
    base_h = static_cast<uint32_t>(
        std::max<int64_t>(0, bounds_l0_.y + bounds_l0_.height));
  }

  base_size_l0_ = ImageDimensions{base_w, base_h};

  // SlideProperties.
  properties_.scanner_model = "Zeiss";
  if (metadata_mpp_) {
    properties_.mpp = {metadata_mpp_->first, metadata_mpp_->second};
  }
  if (metadata_objective_magnification_) {
    properties_.objective_magnification = *metadata_objective_magnification_;
  }

  // Publish content bounds (MRXS-style): clamp to the full L0 image size.
  int64_t bx = std::clamp<int64_t>(bounds_l0_.x, 0, base_w);
  int64_t by = std::clamp<int64_t>(bounds_l0_.y, 0, base_h);
  int64_t bx2 = bounds_l0_.x + bounds_l0_.width;
  int64_t by2 = bounds_l0_.y + bounds_l0_.height;
  bx2 = std::clamp<int64_t>(bx2, 0, base_w);
  by2 = std::clamp<int64_t>(by2, 0, base_h);
  if (bx2 < bx) {
    bx2 = bx;
  }
  if (by2 < by) {
    by2 = by;
  }
  properties_.bounds = SlideBounds(bx, by, bx2 - bx, by2 - by);
}

int CziReader::GetLevelCount() const {
  return static_cast<int>(downsamples_.size());
}

aifocore::Result<LevelInfo> CziReader::GetLevelInfo(int level) const {
  if (level < 0 || level >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }

  const uint32_t base_w = base_size_l0_[0];
  const uint32_t base_h = base_size_l0_[1];

  const int32_t ds = downsamples_[static_cast<size_t>(level)];
  LevelInfo info{};
  info.dimensions = {std::max<uint32_t>(1, base_w / static_cast<uint32_t>(ds)),
                     std::max<uint32_t>(1, base_h / static_cast<uint32_t>(ds))};
  info.downsample_factor = static_cast<double>(ds);
  return info;
}

const SlideProperties& CziReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> CziReader::GetChannelMetadata() const {
  ChannelMetadata red;
  red.name = "Red";
  red.color = ColorRGB{255, 0, 0};
  ChannelMetadata green;
  green.name = "Green";
  green.color = ColorRGB{0, 255, 0};
  ChannelMetadata blue;
  blue.name = "Blue";
  blue.color = ColorRGB{0, 0, 255};
  return {red, green, blue};
}

std::vector<std::string> CziReader::GetAssociatedImageNames() const {
  std::vector<std::string> out;
  for (const auto& a : attachments_) {
    if (a.file_type != "JPG" && a.file_type != "CZI") {
      continue;
    }
    if (a.name == kAttachmentLabel) {
      out.push_back("label");
    } else if (a.name == kAttachmentSlidePreview) {
      out.push_back("macro");
    } else if (a.name == kAttachmentThumbnail) {
      out.push_back("thumbnail");
    }
  }
  return out;
}

aifocore::Result<ImageDimensions> CziReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  std::string_view attachment_name;
  if (name == "label") {
    attachment_name = kAttachmentLabel;
  } else if (name == "macro") {
    attachment_name = kAttachmentSlidePreview;
  } else if (name == "thumbnail") {
    attachment_name = kAttachmentThumbnail;
  } else {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Associated image not found");
  }

  const AttachmentInfo* found = nullptr;
  for (const auto& a : attachments_) {
    if (a.name == attachment_name &&
        (a.file_type == "JPG" || a.file_type == "CZI")) {
      found = &a;
      break;
    }
  }
  if (found == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Associated image not found");
  }

  if (found->file_type == "JPG") {
    FileReader file;
    AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(filename_, "rb"));
    AIFOCORE_RETURN_IF_ERROR(file.Seek(found->file_pos));

    char sid_raw[16] = {};
    AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
    const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
    if (!StartsWithMagic(sid, kSidZisRawAttach)) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Bad attachment segment magic");
    }

    (void)ReadLeInt64(file.Get());  // alloc
    (void)ReadLeInt64(file.Get());  // used
    int32_t data_size = 0;
    AIFOCORE_ASSIGN_OR_RETURN(data_size, ReadLeInt32(file.Get()));
    if (data_size <= 0) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Invalid attachment data size");
    }

    AIFOCORE_RETURN_IF_ERROR(file.Seek(
        found->file_pos + static_cast<int64_t>(kCziAttachmentHdrLen)));
    AIFOCORE_ASSIGN_OR_RETURN(auto jpg_bytes,
                              file.ReadBytes(static_cast<size_t>(data_size)));
    AIFOCORE_ASSIGN_OR_RETURN(const auto dims,
                              runtime::decoders::GetJpegDimensions(jpg_bytes));
    return dims;
  }

  // Embedded CZI: parse embedded header+directory and extract single subblock
  // dims.
  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(filename_, "rb"));
  const int64_t embedded_base =
      found->file_pos + static_cast<int64_t>(kCziAttachmentHdrLen);
  AIFOCORE_ASSIGN_OR_RETURN(
      const auto sb,
      czi::internal::ParseEmbeddedSingleSubblock(file, embedded_base));
  return ImageDimensions{sb.w, sb.h};
}

aifocore::Result<Image> CziReader::ReadAssociatedImage(
    std::string_view name) const {
  std::string_view attachment_name;
  if (name == "label") {
    attachment_name = kAttachmentLabel;
  } else if (name == "macro") {
    attachment_name = kAttachmentSlidePreview;
  } else if (name == "thumbnail") {
    attachment_name = kAttachmentThumbnail;
  } else {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Associated image not found");
  }

  const AttachmentInfo* found = nullptr;
  for (const auto& a : attachments_) {
    if (a.name == attachment_name &&
        (a.file_type == "JPG" || a.file_type == "CZI")) {
      found = &a;
      break;
    }
  }
  if (found == nullptr) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Associated image not found");
  }

  if (found->file_type == "JPG") {
    FileReader file;
    AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(filename_, "rb"));
    AIFOCORE_RETURN_IF_ERROR(file.Seek(found->file_pos));

    char sid_raw[16] = {};
    AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
    const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
    if (!StartsWithMagic(sid, kSidZisRawAttach)) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Bad attachment segment magic");
    }

    (void)ReadLeInt64(file.Get());  // alloc
    (void)ReadLeInt64(file.Get());  // used
    int32_t data_size = 0;
    AIFOCORE_ASSIGN_OR_RETURN(data_size, ReadLeInt32(file.Get()));
    if (data_size <= 0) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                  "Invalid attachment data size");
    }

    AIFOCORE_RETURN_IF_ERROR(file.Seek(
        found->file_pos + static_cast<int64_t>(kCziAttachmentHdrLen)));
    AIFOCORE_ASSIGN_OR_RETURN(auto jpg_bytes,
                              file.ReadBytes(static_cast<size_t>(data_size)));

    AIFOCORE_ASSIGN_OR_RETURN(auto decoded,
                              runtime::decoders::DecodeJpegToRgb(jpg_bytes));
    if (decoded.rgb.empty() || decoded.width == 0 || decoded.height == 0) {
      return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                  "Failed to decode JPEG associated image");
    }

    Image img(ImageDimensions{decoded.width, decoded.height}, ImageFormat::kRGB,
              DataType::kUInt8);
    std::memcpy(img.GetData(), decoded.rgb.data(), decoded.rgb.size());
    return img;
  }

  // Embedded CZI associated image.
  FileReader file;
  AIFOCORE_ASSIGN_OR_RETURN(file, FileReader::Open(filename_, "rb"));
  const int64_t embedded_base =
      found->file_pos + static_cast<int64_t>(kCziAttachmentHdrLen);
  AIFOCORE_ASSIGN_OR_RETURN(
      const auto sb,
      czi::internal::ParseEmbeddedSingleSubblock(file, embedded_base));
  AIFOCORE_ASSIGN_OR_RETURN(auto rgb, czi::internal::ReadEmbeddedSubblockRgb8(
                                          file, embedded_base, sb));
  Image img(ImageDimensions{sb.w, sb.h}, ImageFormat::kRGB, DataType::kUInt8);
  std::memcpy(img.GetData(), rgb.data(), rgb.size());
  return img;
}

Metadata CziReader::GetMetadata() const {
  Metadata meta;
  meta[std::string(MetadataKeys::kFormat)] = std::string("CZI");
  meta[std::string(MetadataKeys::kLevels)] = downsamples_.size();
  if (metadata_mpp_) {
    meta[std::string(MetadataKeys::kMppX)] = metadata_mpp_->first;
    meta[std::string(MetadataKeys::kMppY)] = metadata_mpp_->second;
  }
  if (metadata_objective_magnification_) {
    meta[std::string(MetadataKeys::kMagnification)] =
        *metadata_objective_magnification_;
  }
  meta[std::string(MetadataKeys::kScannerModel)] = std::string("Zeiss");
  meta[std::string(MetadataKeys::kChannels)] = static_cast<size_t>(3);
  return meta;
}

ImageDimensions CziReader::GetTileSize() const {
  // Use the most common tile size at level 0, fallback to 512x512.
  if (level_subblocks_.empty() || level_subblocks_[0].empty()) {
    return {512, 512};
  }
  const auto& sb = subblocks_.at(level_subblocks_[0][0]);
  return {sb.w, sb.h};
}

aifocore::Result<core::TilePlan> CziReader::PrepareRequest(
    const core::TileRequest& request) const {
  AIFOCORE_ASSIGN_OR_RETURN(const auto level_info, GetLevelInfo(request.level));
  AIFOCORE_ASSIGN_OR_RETURN(const auto spatial_index,
                            GetSpatialIndex(request.level));
  const CziPlanContext context{
      .level_count = GetLevelCount(),
      .level_info = level_info,
      .spatial_index = spatial_index,
  };
  return CziPlanBuilder::BuildPlan(request, context);
}

aifocore::Status CziReader::ExecutePlan(const core::TilePlan& plan,
                                        runtime::Canvas& writer) const {
  const CziExecContext context(filename_, subblocks_, GetCache());
  return CziTileExecutor::ExecutePlan(plan, context, writer);
}

aifocore::Result<std::shared_ptr<czi::CziSpatialIndex>>
CziReader::GetSpatialIndex(int level) const {
  std::lock_guard<std::mutex> lock(spatial_index_mutex_);
  if (level < 0 || level >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid level for spatial index");
  }
  if (spatial_indices_[static_cast<size_t>(level)]) {
    return spatial_indices_[static_cast<size_t>(level)];
  }

  const int32_t ds = downsamples_[static_cast<size_t>(level)];
  const auto& sb_indices = level_subblocks_[static_cast<size_t>(level)];
  if (sb_indices.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No tiles at this level");
  }

  // Derive cell step from max tile dimension in level coordinates.
  uint32_t max_dim = 1;
  for (uint32_t idx : sb_indices) {
    const auto& sb = subblocks_[idx];
    max_dim = std::max(max_dim, std::max(sb.w, sb.h));
  }
  const double step = static_cast<double>(max_dim);

  std::vector<czi::SpatialTile> tiles;
  tiles.reserve(sb_indices.size());
  for (uint32_t idx : sb_indices) {
    const auto& sb = subblocks_[idx];
    const double tx = static_cast<double>(sb.x) / static_cast<double>(ds);
    const double ty = static_cast<double>(sb.y) / static_cast<double>(ds);
    czi::SpatialTile st{};
    st.info.subblock_index = idx;
    st.info.width = sb.w;
    st.info.height = sb.h;
    st.bbox.min = {tx, ty};
    st.bbox.max = {tx + sb.w, ty + sb.h};
    tiles.push_back(st);
  }

  AIFOCORE_ASSIGN_OR_RETURN(
      auto index, czi::CziSpatialIndex::Build(std::move(tiles), step));
  spatial_indices_[static_cast<size_t>(level)] = index;
  return index;
}

fastslide::CziReader::SubblockInfo CziReader::GetSubblockInfo(
    uint32_t index) const {
  return subblocks_.at(index);
}

}  // namespace fastslide
