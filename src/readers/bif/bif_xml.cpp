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

#include "fastslide/readers/bif/bif_xml.h"

#include <pugixml.hpp>

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide {
namespace bif {

namespace {

// Recursively visit every element named `name`, invoking `fn(node)`.
template <typename Fn>
void ForEachNamed(const pugi::xml_node& root, std::string_view name, Fn&& fn) {
  for (pugi::xml_node child : root.children()) {
    if (child.type() != pugi::node_element) {
      continue;
    }
    if (name == child.name()) {
      fn(child);
    }
    ForEachNamed(child, name, fn);
  }
}

// Find the first element named `name` anywhere under `root` (depth-first).
pugi::xml_node FindFirstNamed(const pugi::xml_node& root,
                              std::string_view name) {
  for (pugi::xml_node child : root.children()) {
    if (child.type() != pugi::node_element) {
      continue;
    }
    if (name == child.name()) {
      return child;
    }
    if (pugi::xml_node found = FindFirstNamed(child, name)) {
      return found;
    }
  }
  return pugi::xml_node();
}

JointDirection ParseDirection(std::string_view dir) {
  if (dir == "RIGHT") {
    return JointDirection::kRight;
  }
  if (dir == "LEFT") {
    return JointDirection::kLeft;
  }
  if (dir == "UP") {
    return JointDirection::kUp;
  }
  if (dir == "DOWN") {
    return JointDirection::kDown;
  }
  return JointDirection::kUnknown;
}

}  // namespace

bool LooksLikeBif(std::string_view xmp_packet) {
  // IFD 0 of a DP 200 BIF always carries an <iScan ...> element. This is a
  // cheap substring test used for content-based format detection.
  return xmp_packet.find("iScan") != std::string_view::npos;
}

aifocore::Result<ScannerInfo> ParseScannerInfo(std::string_view xmp_packet) {
  pugi::xml_document doc;
  const pugi::xml_parse_result result =
      doc.load_buffer(xmp_packet.data(), xmp_packet.size());
  if (!result) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Failed to parse BIF iScan XML: {}",
                              result.description()));
  }

  const pugi::xml_node iscan = FindFirstNamed(doc, "iScan");
  if (!iscan) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF IFD0 XMP has no <iScan> element");
  }

  ScannerInfo info;
  info.scanner_model = iscan.attribute("ScannerModel").as_string("");
  info.magnification = iscan.attribute("Magnification").as_double(0.0);
  info.scan_res = iscan.attribute("ScanRes").as_double(0.0);
  info.scan_white_point = iscan.attribute("ScanWhitePoint").as_int(255);
  info.barcode_1d = iscan.attribute("Barcode1D").as_string("");
  info.barcode_2d = iscan.attribute("Barcode2D").as_string("");
  info.z_layers = iscan.attribute("Z-layers").as_int(1);
  return info;
}

aifocore::Result<EncodeInfo> ParseEncodeInfo(std::string_view xmp_packet) {
  pugi::xml_document doc;
  const pugi::xml_parse_result result =
      doc.load_buffer(xmp_packet.data(), xmp_packet.size());
  if (!result) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Failed to parse BIF EncodeInfo XML: {}",
                              result.description()));
  }

  EncodeInfo encode;

  // Each <ImageInfo> (under <SlideStitchInfo>) describes one AOI's stitch grid
  // and carries the <TileJointInfo> overlap records.
  ForEachNamed(doc, "ImageInfo", [&](const pugi::xml_node& image_info) {
    // Skip AOIs that failed to scan: their tiles are not present on disk, so
    // placing them would emit phantom geometry. The attribute defaults to 1
    // (scanned) when absent so files that omit it are not silently dropped.
    if (image_info.attribute("AOIScanned").as_int(1) == 0) {
      return;
    }

    AoiInfo aoi;
    aoi.aoi_index = image_info.attribute("AOIIndex").as_int(0);
    aoi.num_rows = image_info.attribute("NumRows").as_int(0);
    aoi.num_cols = image_info.attribute("NumCols").as_int(0);
    aoi.tile_width = image_info.attribute("Width").as_double(0.0);
    aoi.tile_height = image_info.attribute("Height").as_double(0.0);
    aoi.pos_x = image_info.attribute("Pos-X").as_double(0.0);
    aoi.pos_y = image_info.attribute("Pos-Y").as_double(0.0);

    for (pugi::xml_node joint_node : image_info.children("TileJointInfo")) {
      TileJoint joint;
      joint.direction =
          ParseDirection(joint_node.attribute("Direction").as_string(""));
      joint.tile1 = joint_node.attribute("Tile1").as_int(0);
      joint.tile2 = joint_node.attribute("Tile2").as_int(0);
      joint.overlap_x = joint_node.attribute("OverlapX").as_double(0.0);
      joint.overlap_y = joint_node.attribute("OverlapY").as_double(0.0);
      joint.confidence = joint_node.attribute("Confidence").as_int(0);
      joint.flag_joined = joint_node.attribute("FlagJoined").as_int(0) != 0;
      aoi.joints.push_back(joint);
    }
    encode.aois.push_back(std::move(aoi));
  });

  if (encode.aois.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "BIF EncodeInfo has no <ImageInfo> AOIs");
  }

  // The <AoiOrigin> block lists image-space origins as <AOI0 .../>,
  // <AOI1 .../>, ... Merge those into the AOIs matched by index.
  const pugi::xml_node aoi_origin = FindFirstNamed(doc, "AoiOrigin");
  if (aoi_origin) {
    for (pugi::xml_node child : aoi_origin.children()) {
      if (child.type() != pugi::node_element) {
        continue;
      }
      // Element name is "AOI<n>"; extract the trailing index.
      std::string_view elem_name(child.name());
      if (elem_name.rfind("AOI", 0) != 0) {
        continue;
      }
      int idx = 0;
      const std::string digits(elem_name.substr(3));
      if (!digits.empty()) {
        idx = std::atoi(digits.c_str());
      }
      const double ox = child.attribute("OriginX").as_double(0.0);
      const double oy = child.attribute("OriginY").as_double(0.0);
      for (AoiInfo& aoi : encode.aois) {
        if (aoi.aoi_index == idx) {
          aoi.origin_x = ox;
          aoi.origin_y = oy;
          break;
        }
      }
    }
  }

  return encode;
}

}  // namespace bif
}  // namespace fastslide
