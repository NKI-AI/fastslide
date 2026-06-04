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

#include "fastslide/readers/ometiff/metadata_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <pugixml.hpp>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide::formats::ometiff {
namespace {

std::string ToLower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

}  // namespace

bool OmeMetadataParser::LooksLikeOmeXml(std::string_view xml) {
  const std::string lower = ToLower(xml);
  return lower.find("<ome") != std::string::npos &&
         lower.find("openmicroscopy.org/schemas/ome") != std::string::npos;
}

aifocore::Result<OmeMetadata> OmeMetadataParser::Parse(std::string_view xml) {
  OmeMetadata out;
  pugi::xml_document doc;
  if (!doc.load_string(std::string(xml).c_str())) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Failed to parse OME-XML");
  }

  // OME root (namespace-aware documents often have it as "OME" without prefix).
  pugi::xml_node ome_root = doc.child("OME");
  if (ome_root.empty()) {
    // Fallback: search by local name if namespaces are used with prefixes.
    for (auto n : doc.children()) {
      if (std::string_view(n.name()).ends_with("OME")) {
        ome_root = n;
        break;
      }
    }
  }
  if (ome_root.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-XML: missing <OME> root");
  }

  // The root UUID identifies *this* file within a multi-file dataset; planes
  // whose <TiffData> references a different UUID live in sibling files.
  out.self_uuid = ome_root.attribute("UUID").as_string();

  // Find first Image/Pixels.
  pugi::xml_node image = ome_root.child("Image");
  if (image.empty()) {
    // Some writers nest differently; keep it simple for now.
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-XML: missing <Image>");
  }

  pugi::xml_node pixels = image.child("Pixels");
  if (pixels.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-XML: missing <Pixels>");
  }

  AIFOCORE_RETURN_IF_ERROR(ParsePixels(pixels, out));
  AIFOCORE_RETURN_IF_ERROR(ParseChannels(pixels, out));
  AIFOCORE_RETURN_IF_ERROR(ParseTiffData(pixels, out));
  AIFOCORE_RETURN_IF_ERROR(ParsePyramidResolutions(ome_root, out));

  return out;
}

aifocore::Status OmeMetadataParser::ParsePixels(
    const pugi::xml_node& pixels_node, OmeMetadata& out) {
  auto attr_u32 = [&](const char* name, uint32_t& dst) -> aifocore::Status {
    auto a = pixels_node.attribute(name);
    if (a.empty()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("OME-XML: Pixels missing {}", name));
    }
    dst = static_cast<uint32_t>(a.as_uint());
    return aifocore::Status::OkStatus();
  };

  AIFOCORE_RETURN_IF_ERROR(attr_u32("SizeX", out.pixels.size_x));
  AIFOCORE_RETURN_IF_ERROR(attr_u32("SizeY", out.pixels.size_y));
  AIFOCORE_RETURN_IF_ERROR(attr_u32("SizeC", out.pixels.size_c));
  AIFOCORE_RETURN_IF_ERROR(attr_u32("SizeZ", out.pixels.size_z));
  AIFOCORE_RETURN_IF_ERROR(attr_u32("SizeT", out.pixels.size_t));

  out.pixels.dimension_order =
      pixels_node.attribute("DimensionOrder").as_string();
  out.pixels.interleaved = pixels_node.attribute("Interleaved").as_bool(false);

  if (auto psx = pixels_node.attribute("PhysicalSizeX"); !psx.empty()) {
    out.pixels.physical_size_x = psx.as_double();
  }
  if (auto psy = pixels_node.attribute("PhysicalSizeY"); !psy.empty()) {
    out.pixels.physical_size_y = psy.as_double();
  }
  if (auto psz = pixels_node.attribute("PhysicalSizeZ"); !psz.empty()) {
    out.pixels.physical_size_z = psz.as_double();
  }
  if (auto ti = pixels_node.attribute("TimeIncrement"); !ti.empty()) {
    out.pixels.time_increment = ti.as_double();
  }

  return aifocore::Status::OkStatus();
}

aifocore::Status OmeMetadataParser::ParseChannels(
    const pugi::xml_node& pixels_node, OmeMetadata& out) {
  out.channels.clear();
  out.channels.reserve(out.pixels.size_c);

  uint32_t idx = 0;
  for (pugi::xml_node ch = pixels_node.child("Channel"); ch;
       ch = ch.next_sibling("Channel")) {
    OmeChannel c;
    c.index = idx++;
    c.name = ch.attribute("Name").as_string();
    if (auto color = ch.attribute("Color"); !color.empty()) {
      // OME stores ARGB as a signed integer in some writers (Bio-Formats).
      c.color_argb = static_cast<int32_t>(color.as_int());
    }
    out.channels.push_back(std::move(c));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status OmeMetadataParser::ParseTiffData(
    const pugi::xml_node& pixels_node, OmeMetadata& out) {
  out.tiff_data.clear();
  for (pugi::xml_node td = pixels_node.child("TiffData"); td;
       td = td.next_sibling("TiffData")) {
    OmeTiffData d;
    d.first_c = td.attribute("FirstC").as_uint();
    d.first_z = td.attribute("FirstZ").as_uint();
    d.first_t = td.attribute("FirstT").as_uint();
    d.ifd = td.attribute("IFD").as_uint();
    d.plane_count = td.attribute("PlaneCount").as_uint(1);
    if (pugi::xml_node uuid = td.child("UUID"); uuid) {
      d.uuid = uuid.text().as_string();
      d.file_name = uuid.attribute("FileName").as_string();
    }
    out.tiff_data.push_back(d);
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status OmeMetadataParser::ParsePyramidResolutions(
    const pugi::xml_node& ome_root, OmeMetadata& out) {
  out.pyramid_resolutions.clear();

  // Bio-Formats encodes pyramid levels in StructuredAnnotations MapAnnotation
  // with Namespace="openmicroscopy.org/PyramidResolution".
  pugi::xml_node structured = ome_root.child("StructuredAnnotations");
  if (structured.empty()) {
    return aifocore::Status::OkStatus();  // Not pyramidal.
  }

  for (pugi::xml_node ann = structured.child("MapAnnotation"); ann;
       ann = ann.next_sibling("MapAnnotation")) {
    const std::string ns = ann.attribute("Namespace").as_string();
    if (ns != "openmicroscopy.org/PyramidResolution") {
      continue;
    }
    pugi::xml_node value = ann.child("Value");
    for (pugi::xml_node m = value.child("M"); m; m = m.next_sibling("M")) {
      const int k = m.attribute("K").as_int();
      const std::string txt = m.text().as_string();
      uint32_t w = 0, h = 0;
      if (std::sscanf(txt.c_str(), "%u %u", &w, &h) == 2 && k > 0) {
        out.pyramid_resolutions[k] = {w, h};
      }
    }
    break;
  }

  return aifocore::Status::OkStatus();
}

ColorRGB OmeMetadataParser::ColorFromOmeArgb(std::optional<int32_t> argb,
                                             const ColorRGB& fallback) {
  if (!argb.has_value()) {
    return fallback;
  }
  // Bio-Formats commonly serializes Java's 32-bit int color values, but in
  // practice we observe these stored as 0xRRGGBBAA (RGBA, alpha in LSB) in the
  // wild. Decode as RGBA and ignore alpha.
  //
  // Example: 65280 == 0x0000FF00 => RGB(0,0,255) (blue).
  const uint32_t u = static_cast<uint32_t>(*argb);
  const uint8_t r = static_cast<uint8_t>((u >> 24) & 0xFF);
  const uint8_t g = static_cast<uint8_t>((u >> 16) & 0xFF);
  const uint8_t b = static_cast<uint8_t>((u >> 8) & 0xFF);
  return ColorRGB(r, g, b);
}

}  // namespace fastslide::formats::ometiff
