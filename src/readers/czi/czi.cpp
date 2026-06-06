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

// CZI / ZISRAW reader.
//
// The permissively licensed `czifile` Python library (Christoph Gohlke,
// BSD-3-Clause) was used as the reference for the on-disk layout.

#include "fastslide/readers/czi/czi.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pugixml.hpp>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/czi/czi_embedded.h"
#include "fastslide/readers/czi/czi_parse.h"
#include "fastslide/readers/czi/czi_scene_image.h"
#include "fastslide/runtime/decoders/jpeg_decoder.h"
#include "fastslide/runtime/io/ascii_utils.h"
#include "fastslide/runtime/io/binary_utils.h"
#include "fastslide/runtime/io/file_reader.h"
#include "fastslide/utilities/colors.h"

namespace fastslide {

namespace {

using runtime::io::ReadFixedAscii;
using runtime::io::StartsWithMagic;

/// @brief Parse a CZI display color string ("#AARRGGBB" or "#RRGGBB").
///
/// CZI `DisplaySetting/Channels/Channel/Color` stores an ARGB (or RGB) hex
/// string. Zeiss writes a leading alpha byte that we ignore. Returns nullopt
/// for malformed input so the caller can fall back to a generated color.
[[nodiscard]] std::optional<ColorRGB> ParseCziColor(std::string_view s) {
  if (!s.empty() && s.front() == '#') {
    s.remove_prefix(1);
  }
  // Accept 6 (RRGGBB) or 8 (AARRGGBB) hex digits.
  if (s.size() != 6 && s.size() != 8) {
    return std::nullopt;
  }
  if (s.size() == 8) {
    s.remove_prefix(2);  // drop the alpha byte
  }
  auto hex_pair = [](char hi, char lo) -> std::optional<uint8_t> {
    auto nibble = [](char ch) -> int {
      if (ch >= '0' && ch <= '9')
        return ch - '0';
      if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
      if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
      return -1;
    };
    const int h = nibble(hi);
    const int l = nibble(lo);
    if (h < 0 || l < 0) {
      return std::nullopt;
    }
    return static_cast<uint8_t>((h << 4) | l);
  };
  const auto r = hex_pair(s[0], s[1]);
  const auto g = hex_pair(s[2], s[3]);
  const auto b = hex_pair(s[4], s[5]);
  if (!r || !g || !b) {
    return std::nullopt;
  }
  return ColorRGB{*r, *g, *b};
}

constexpr std::string_view kCziExt = ".czi";
constexpr std::string_view kSidZisRawFile = "ZISRAWFILE";
constexpr std::string_view kSidZisRawDirectory = "ZISRAWDIRECTORY";
constexpr std::string_view kSidZisRawMetadata = "ZISRAWMETADATA";
constexpr std::string_view kSidZisRawAttDir = "ZISRAWATTDIR";
constexpr std::string_view kSidZisRawAttach = "ZISRAWATTACH";

// Size of the ZISRAWATTACH segment header that precedes attachment payload
// data: the 32-byte segment header plus the fixed 256-byte attachment data
// section (`SIZE_ATTACHMENT_DATA` in the ZISRAW layout). Unlike the subblock
// data section this part is a fixed size, so the constant is exact.
constexpr size_t kCziAttachmentHdrLen = 288;

// Bytes of the SubBlockDirectory data section before the entry list:
// entry_count[4] + reserved[124].
constexpr size_t kDirectoryDataHeaderLen = 128;

// Only attachments carrying decodable bitmap payloads are surfaced.
[[nodiscard]] bool IsImageAttachment(std::string_view file_type) {
  return file_type == "JPG" || file_type == "CZI";
}

}  // namespace

CziReader::CziReader(std::string filename) : filename_(std::move(filename)) {}

CziReader::~CziReader() = default;

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

  BuildSceneImages();
  if (images_.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "CZI file contains no decodable scenes");
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status CziReader::ParseFileHeader(FileReader& file) {
  AIFOCORE_RETURN_IF_ERROR(file.Seek(0));

  // ZISRAWFILE segment: a 16-byte identifier, the 16-byte allocated/used
  // sizes, version + reserved ints, two 16-byte GUIDs, the file-part index,
  // then the three directory/metadata/attachment offsets we actually need.
  char sid_raw[16] = {};
  AIFOCORE_RETURN_IF_ERROR(file.Read(sid_raw, sizeof(sid_raw)));
  const std::string sid = ReadFixedAscii(sid_raw, sizeof(sid_raw));
  if (!StartsWithMagic(sid, kSidZisRawFile)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Bad magic: expected {}, got {}", kSidZisRawFile,
                              sid));
  }

  (void)ReadLeInt64(file.Get());  // allocated_size
  (void)ReadLeInt64(file.Get());  // used_size
  (void)ReadLeInt32(file.Get());  // major
  (void)ReadLeInt32(file.Get());  // minor
  (void)ReadLeInt32(file.Get());  // reserved
  (void)ReadLeInt32(file.Get());  // reserved
  char guid_primary[16];
  char guid_file[16];
  AIFOCORE_RETURN_IF_ERROR(file.Read(guid_primary, sizeof(guid_primary)));
  AIFOCORE_RETURN_IF_ERROR(file.Read(guid_file, sizeof(guid_file)));
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

  (void)ReadLeInt64(file.Get());  // allocated_size
  (void)ReadLeInt64(file.Get());  // used_size
  int32_t entry_count = 0;
  AIFOCORE_ASSIGN_OR_RETURN(entry_count, ReadLeInt32(file.Get()));
  if (entry_count < 0) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Negative directory entry count {}",
                              entry_count));
  }
  // Skip the rest of the fixed data header (entry_count already consumed).
  std::vector<char> data_header(kDirectoryDataHeaderLen - sizeof(int32_t));
  AIFOCORE_RETURN_IF_ERROR(file.Read(data_header.data(), data_header.size()));

  subblocks_.clear();
  subblocks_.reserve(static_cast<size_t>(entry_count));

  // Read entries one at a time straight from the stream: each is a 32-byte
  // fixed header followed by `dimension_count` 20-byte dimension records.
  for (int32_t i = 0; i < entry_count; ++i) {
    AIFOCORE_ASSIGN_OR_RETURN(auto fixed,
                              file.ReadBytes(czi::kDirEntryFixedSize));
    AIFOCORE_ASSIGN_OR_RETURN(const auto header,
                              czi::ParseDirEntryHeader(fixed));

    Subblock sb{};
    sb.index = static_cast<uint32_t>(subblocks_.size());
    sb.file_pos = header.file_position;
    sb.pixel_type = header.pixel_type;
    sb.compression = header.compression;
    sb.dim_count = header.dimension_count;

    if (header.dimension_count > 0) {
      const size_t dims_bytes = static_cast<size_t>(header.dimension_count) *
                                czi::kDimensionEntrySize;
      AIFOCORE_ASSIGN_OR_RETURN(auto dims_buf, file.ReadBytes(dims_bytes));
      std::span<const uint8_t> dims_span(dims_buf);
      for (int32_t d = 0; d < header.dimension_count; ++d) {
        AIFOCORE_ASSIGN_OR_RETURN(
            const auto dim,
            czi::ParseDimensionRecord(dims_span.subspan(
                static_cast<size_t>(d) * czi::kDimensionEntrySize)));
        switch (dim.axis) {
          case 'X':
            sb.x = dim.start;
            sb.w = static_cast<uint32_t>(std::max(0, dim.stored_size));
            sb.downsample = czi::DownsampleFromSizes(dim.size, dim.stored_size);
            break;
          case 'Y':
            sb.y = dim.start;
            sb.h = static_cast<uint32_t>(std::max(0, dim.stored_size));
            break;
          case 'S':
            sb.scene = dim.start;
            break;
          case 'Z':
            sb.z = dim.start;
            break;
          case 'T':
            sb.t = dim.start;
            break;
          case 'C':
            sb.c = dim.start;
            break;
          default:
            // Other dimensions (M, ...) do not affect tile layout or channel
            // selection.
            break;
        }
      }
    }

    if (sb.w == 0 || sb.h == 0) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          "Missing X/Y dimension in CZI subblock entry");
    }
    subblocks_.push_back(sb);
  }

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
  (void)ReadLeInt32(file.Get());  // binary attachment size
  char reserved[248];
  AIFOCORE_RETURN_IF_ERROR(file.Read(reserved, sizeof(reserved)));

  if (xml_size <= 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid metadata XML size");
  }

  std::vector<char> xml_buf(static_cast<size_t>(xml_size));
  AIFOCORE_RETURN_IF_ERROR(file.Read(xml_buf.data(), xml_buf.size()));
  metadata_xml_.assign(xml_buf.begin(), xml_buf.end());

  // Pull a few scalar fields out of the CZI ImageDocument XML schema. Parsing
  // is best-effort: a malformed or partial document never blocks opening.
  pugi::xml_document doc;
  pugi::xml_parse_result ok =
      doc.load_buffer(metadata_xml_.data(), metadata_xml_.size());
  if (!ok) {
    return aifocore::Status::OkStatus();
  }

  pugi::xml_node root = doc.document_element();
  if (std::string_view(root.name()) != "ImageDocument") {
    auto alt = doc.child("ImageDocument");
    if (alt) {
      root = alt;
    }
  }

  // Whole-image pixel size: Metadata/Information/Image/Size{X,Y}.
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

  // Physical pixel scale: Metadata/Scaling/Items/Distance[@Id]/Value, stored
  // in metres; convert to microns per pixel.
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

  // Focal-plane (Z) spacing: same Scaling/Items/Distance schema, stored in
  // metres; convert to microns. Mirrors czifile's `scale_xyz['Z']`.
  auto z_val =
      root.select_node(".//Metadata/Scaling/Items/Distance[@Id='Z']/Value")
          .node();
  if (z_val) {
    const double mz = z_val.text().as_double(0.0);
    if (mz > 0.0) {
      metadata_z_spacing_um_ = mz * 1'000'000.0;
    }
  }

  // Time-point (T) interval: Metadata/.../Interval/TimeSpan/Value, in the unit
  // named by the sibling DefaultUnitFormat ("ms" -> seconds). This follows
  // czifile's T-interval derivation (czifile.py `coords_data`).
  auto t_val = root.select_node(".//Interval/TimeSpan/Value").node();
  if (t_val) {
    double ts = t_val.text().as_double(0.0);
    if (ts > 0.0) {
      auto unit_node =
          root.select_node(".//Interval/TimeSpan/DefaultUnitFormat").node();
      std::string unit = unit_node ? std::string(unit_node.text().as_string(""))
                                   : std::string();
      std::transform(unit.begin(), unit.end(), unit.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (unit == "ms") {
        ts /= 1000.0;
      }
      metadata_t_interval_s_ = ts;
    }
  }

  // Objective magnification: follow ObjectiveSettings/ObjectiveRef@Id to the
  // matching Objective's NominalMagnification.
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

  ParseChannelMetadataXml(root);

  return aifocore::Status::OkStatus();
}

namespace {

/// @brief Copy a scalar child element's text into the `additional` map.
void CopyChildText(ChannelMetadata& md, const pugi::xml_node& ch,
                   const char* tag, std::string_view key) {
  const pugi::xml_node child = ch.child(tag);
  if (!child) {
    return;
  }
  const std::string value = child.text().as_string("");
  if (!value.empty()) {
    md.additional[std::string(key)] = value;
  }
}

/// @brief Channel display color hex string ("#AARRGGBB"), preferring the
///        channel's own <Color> and falling back to a DisplaySetting overlay.
std::string ChannelColorString(const pugi::xml_node& info_ch,
                               const pugi::xml_node& display_ch) {
  std::string color = info_ch.child("Color").text().as_string("");
  if (color.empty() && display_ch) {
    color = display_ch.child("Color").text().as_string("");
  }
  if (color.empty() && display_ch) {
    color = display_ch.child("OriginalColor").text().as_string("");
  }
  return color;
}

/// @brief Build a ChannelMetadata from one Information <Channel>, overlaying
///        display-only fields (dye, short name) from the matching
///        DisplaySetting <Channel>.
ChannelMetadata BuildChannelMetadata(const pugi::xml_node& info_ch,
                                     const pugi::xml_node& display_ch) {
  ChannelMetadata md;

  // Name: @Name attribute, then <Name> child, then @Id.
  std::string name = info_ch.attribute("Name").as_string("");
  if (name.empty()) {
    name = info_ch.child("Name").text().as_string("");
  }
  if (name.empty()) {
    name = info_ch.attribute("Id").as_string("");
  }
  md.name = std::move(name);

  // Biomarker: fluorophore name, then dye name.
  if (auto fluor = info_ch.child("Fluor"); fluor) {
    md.biomarker = fluor.text().as_string("");
  } else if (auto dye = info_ch.child("DyeName"); dye) {
    md.biomarker = dye.text().as_string("");
  } else if (display_ch) {
    md.biomarker = display_ch.child("DyeName").text().as_string("");
  }

  // Color (#AARRGGBB); the alpha byte is dropped by ParseCziColor.
  const std::string color_str = ChannelColorString(info_ch, display_ch);
  if (const auto rgb = ParseCziColor(color_str)) {
    md.color = *rgb;
  }

  // Exposure time: CZI stores nanoseconds; ChannelMetadata is microseconds.
  if (auto exp = info_ch.child("ExposureTime"); exp) {
    const double ns = exp.text().as_double(0.0);
    if (ns > 0.0) {
      md.exposure_time = static_cast<uint32_t>(ns / 1000.0);
    }
  }

  // Component bit depth maps onto the "signal units" (bit-depth) field.
  if (auto bits = info_ch.child("ComponentBitCount"); bits) {
    const int n = bits.text().as_int(0);
    if (n > 0) {
      md.signal_units = static_cast<uint32_t>(n);
    }
  }

  // Richer, format-specific fields kept in `additional` so the full channel
  // description (matching czifile) is available to consumers that want it.
  CopyChildText(md, info_ch, "Fluor", "Fluor");
  CopyChildText(md, info_ch, "ExposureTime", "ExposureTime");  // raw ns
  CopyChildText(md, info_ch, "ExcitationWavelength", "ExcitationWavelength");
  CopyChildText(md, info_ch, "EmissionWavelength", "EmissionWavelength");
  CopyChildText(md, info_ch, "AcquisitionMode", "AcquisitionMode");
  CopyChildText(md, info_ch, "ContrastMethod", "ContrastMethod");
  CopyChildText(md, info_ch, "IlluminationType", "IlluminationType");
  CopyChildText(md, info_ch, "ComponentBitCount", "ComponentBitCount");
  if (!color_str.empty()) {
    md.additional["Color"] = color_str;
  }
  if (display_ch) {
    CopyChildText(md, display_ch, "DyeName", "DyeName");
    CopyChildText(md, display_ch, "ShortName", "ShortName");
    CopyChildText(md, display_ch, "DyeMaxExcitation", "DyeMaxExcitation");
    CopyChildText(md, display_ch, "DyeMaxEmission", "DyeMaxEmission");
  }

  return md;
}

}  // namespace

void CziReader::ParseChannelMetadataXml(const pugi::xml_node& root) {
  // Channel acquisition data (name, color, exposure, wavelengths) lives under
  // Information/.../Channels; display-only data (dye short name, dye maxima)
  // lives under DisplaySetting/Channels. Both are keyed by the same Channel
  // @Id, so iterate Information and overlay DisplaySetting by Id (with a
  // positional fallback).
  metadata_channels_.clear();

  std::vector<std::pair<std::string, pugi::xml_node>> display;
  for (auto node :
       root.select_nodes(".//Metadata/DisplaySetting/Channels/Channel")) {
    display.emplace_back(node.node().attribute("Id").as_string(""),
                         node.node());
  }
  auto display_for = [&](const std::string& id, size_t pos) -> pugi::xml_node {
    if (!id.empty()) {
      for (const auto& [cid, node] : display) {
        if (cid == id) {
          return node;
        }
      }
    }
    if (pos < display.size()) {
      return display[pos].second;
    }
    return pugi::xml_node();
  };

  size_t pos = 0;
  for (auto node : root.select_nodes(
           ".//Metadata/Information/Image/Dimensions/Channels/Channel")) {
    const pugi::xml_node info_ch = node.node();
    const std::string id = info_ch.attribute("Id").as_string("");
    metadata_channels_.push_back(
        BuildChannelMetadata(info_ch, display_for(id, pos)));
    ++pos;
  }

  // Some files only populate DisplaySetting; synthesize from it if Information
  // carried no Channel nodes (BuildChannelMetadata also reads from there).
  if (metadata_channels_.empty()) {
    for (const auto& [id, display_ch] : display) {
      (void)id;
      metadata_channels_.push_back(
          BuildChannelMetadata(display_ch, display_ch));
    }
  }
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

  (void)ReadLeInt64(file.Get());  // allocated_size
  (void)ReadLeInt64(file.Get());  // used_size
  int32_t entry_count = 0;
  AIFOCORE_ASSIGN_OR_RETURN(entry_count, ReadLeInt32(file.Get()));
  char reserved[252];
  AIFOCORE_RETURN_IF_ERROR(file.Read(reserved, sizeof(reserved)));

  attachments_.clear();
  attachments_.reserve(static_cast<size_t>(std::max(0, entry_count)));

  // AttachmentEntryA1: schema[2] + reserved[10] + file_pos[8] + file_part[4]
  // + content_guid[16] + content_file_type[8] + name[80].
  for (int32_t i = 0; i < entry_count; ++i) {
    char schema_raw[2];
    AIFOCORE_RETURN_IF_ERROR(file.Read(schema_raw, sizeof(schema_raw)));
    if (schema_raw[0] != 'A' || schema_raw[1] != '1') {
      return aifocore::Status::OkStatus();
    }
    char reserved_entry[10];
    AIFOCORE_RETURN_IF_ERROR(file.Read(reserved_entry, sizeof(reserved_entry)));
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
    attachments_.push_back(std::move(info));
  }

  return aifocore::Status::OkStatus();
}

void CziReader::BuildSceneImages() {
  const double mpp_x = metadata_mpp_ ? metadata_mpp_->first : 0.0;
  const double mpp_y = metadata_mpp_ ? metadata_mpp_->second : 0.0;
  const double mag = metadata_objective_magnification_.value_or(0.0);

  // GroupSubblocksByScene returns groups ordered by ascending scene id, so the
  // first group is the lowest-numbered scene and becomes the primary image.
  const auto groups = czi::GroupSubblocksByScene(subblocks_);
  images_.clear();
  images_.reserve(groups.size());
  for (const auto& group : groups) {
    // Match the czifile naming convention: "Scene <S-coordinate>".
    std::string name = aifocore::fmt::format("Scene {}", group.scene_id);
    images_.push_back(std::make_unique<CziSceneImage>(
        *this, group.scene_id, std::move(name), group.subblock_indices, mpp_x,
        mpp_y, mag, "Zeiss", metadata_z_spacing_um_, metadata_t_interval_s_));
  }
  primary_index_ = 0;
}

// ---- Multi-image container API ----------------------------------------

std::vector<std::string> CziReader::GetImageNames() const {
  std::vector<std::string> names;
  names.reserve(images_.size());
  for (const auto& img : images_) {
    names.push_back(img->GetName());
  }
  return names;
}

aifocore::Result<const SlideImage*> CziReader::GetImage(int index) const {
  if (index < 0 || static_cast<size_t>(index) >= images_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("CZI image index {} out of range [0, {})", index,
                              images_.size()));
  }
  return static_cast<const SlideImage*>(
      images_[static_cast<size_t>(index)].get());
}

// ---- Primary-scene forwarders -----------------------------------------

int CziReader::GetLevelCount() const {
  return Primary().GetLevelCount();
}

aifocore::Result<LevelInfo> CziReader::GetLevelInfo(int level) const {
  return Primary().GetLevelInfo(level);
}

const SlideProperties& CziReader::GetProperties() const {
  return Primary().GetProperties();
}

std::vector<ChannelMetadata> CziReader::GetChannelMetadata() const {
  return Primary().GetChannelMetadata();
}

ImageDimensions CziReader::GetTileSize() const {
  return Primary().GetTileSize();
}

StackInfo CziReader::GetStackInfo() const {
  return Primary().GetStackInfo();
}

aifocore::Result<core::TilePlan> CziReader::PrepareRequest(
    const core::TileRequest& request) const {
  return Primary().PrepareRequest(request);
}

aifocore::Status CziReader::ExecutePlan(const core::TilePlan& plan,
                                        runtime::Canvas& writer) const {
  return Primary().ExecutePlan(plan, writer);
}

// ---- Associated images (native CZI names) -----------------------------

const CziReader::AttachmentInfo* CziReader::FindAttachment(
    std::string_view name) const {
  for (const auto& a : attachments_) {
    if (a.name == name && IsImageAttachment(a.file_type)) {
      return &a;
    }
  }
  return nullptr;
}

std::vector<std::string> CziReader::GetAssociatedImageNames() const {
  std::vector<std::string> out;
  for (const auto& a : attachments_) {
    if (IsImageAttachment(a.file_type)) {
      out.push_back(a.name);
    }
  }
  return out;
}

aifocore::Result<ImageDimensions> CziReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  const AttachmentInfo* found = FindAttachment(name);
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

    (void)ReadLeInt64(file.Get());  // allocated_size
    (void)ReadLeInt64(file.Get());  // used_size
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

  // Embedded CZI: parse the embedded directory and read its single subblock.
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
  const AttachmentInfo* found = FindAttachment(name);
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

    (void)ReadLeInt64(file.Get());  // allocated_size
    (void)ReadLeInt64(file.Get());  // used_size
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
  AIFOCORE_ASSIGN_OR_RETURN(auto rgb, czi::internal::ReadEmbeddedSubblockRgb(
                                          file, embedded_base, sb));
  Image img(ImageDimensions{sb.w, sb.h}, ImageFormat::kRGB, rgb.data_type);
  std::memcpy(img.GetData(), rgb.bytes.data(), rgb.bytes.size());
  return img;
}

Metadata CziReader::GetMetadata() const {
  Metadata meta;
  meta[std::string(MetadataKeys::kFormat)] = std::string("CZI");
  meta[std::string(MetadataKeys::kLevels)] =
      static_cast<size_t>(Primary().GetLevelCount());
  if (metadata_mpp_) {
    meta[std::string(MetadataKeys::kMppX)] = metadata_mpp_->first;
    meta[std::string(MetadataKeys::kMppY)] = metadata_mpp_->second;
  }
  if (metadata_objective_magnification_) {
    meta[std::string(MetadataKeys::kMagnification)] =
        *metadata_objective_magnification_;
  }
  meta[std::string(MetadataKeys::kScannerModel)] = std::string("Zeiss");
  meta[std::string(MetadataKeys::kChannels)] =
      Primary().GetChannelMetadata().size();

  // Focal/time stack extent of the primary scene, plus optional spacings.
  const StackInfo stack = Primary().GetStackInfo();
  meta["czi.z-count"] = static_cast<size_t>(stack.z_count);
  meta["czi.t-count"] = static_cast<size_t>(stack.t_count);
  if (stack.z_spacing_um) {
    meta["czi.z-spacing-um"] = *stack.z_spacing_um;
  }
  if (stack.t_interval_s) {
    meta["czi.t-interval-s"] = *stack.t_interval_s;
  }
  return meta;
}

}  // namespace fastslide
