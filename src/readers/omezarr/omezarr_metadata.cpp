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

#include "fastslide/readers/omezarr/omezarr_metadata.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"

namespace fastslide::formats::omezarr {

namespace {

using json = nlohmann::json;

aifocore::Status MakeError(std::string message) {
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                              std::move(message));
}

std::optional<size_t> FindAxis(const std::vector<OmeAxis>& axes,
                               std::string_view name) {
  for (size_t i = 0; i < axes.size(); ++i) {
    if (axes[i].name == name)
      return i;
  }
  return std::nullopt;
}

aifocore::Status ParseAxesField(const json& axes_array,
                                std::vector<OmeAxis>* out) {
  if (!axes_array.is_array()) {
    return MakeError("OME-NGFF 'axes' must be an array");
  }
  out->clear();
  out->reserve(axes_array.size());
  for (const auto& entry : axes_array) {
    OmeAxis axis;
    if (entry.is_string()) {
      axis.name = entry.get<std::string>();
    } else if (entry.is_object()) {
      if (entry.contains("name") && entry["name"].is_string()) {
        axis.name = entry["name"].get<std::string>();
      } else {
        return MakeError("OME-NGFF axis missing required 'name'");
      }
      if (entry.contains("type") && entry["type"].is_string()) {
        axis.type = entry["type"].get<std::string>();
      }
    } else {
      return MakeError("OME-NGFF axis entry must be string or object");
    }
    out->push_back(std::move(axis));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status ParseDatasetsField(const json& datasets_array,
                                    std::vector<OmeMultiscaleDataset>* out) {
  if (!datasets_array.is_array() || datasets_array.empty()) {
    return MakeError("OME-NGFF 'datasets' must be a non-empty array");
  }
  out->clear();
  out->reserve(datasets_array.size());
  for (const auto& entry : datasets_array) {
    if (!entry.is_object()) {
      return MakeError("OME-NGFF dataset entry must be an object");
    }
    OmeMultiscaleDataset dataset;
    if (!entry.contains("path") || !entry["path"].is_string()) {
      return MakeError("OME-NGFF dataset entry missing 'path'");
    }
    dataset.path = entry["path"].get<std::string>();
    if (entry.contains("coordinateTransformations")) {
      const auto& transforms = entry["coordinateTransformations"];
      if (transforms.is_array()) {
        for (const auto& tf : transforms) {
          if (tf.is_object() && tf.value("type", "") == "scale" &&
              tf.contains("scale") && tf["scale"].is_array()) {
            dataset.scale.clear();
            dataset.scale.reserve(tf["scale"].size());
            for (const auto& v : tf["scale"]) {
              if (v.is_number())
                dataset.scale.push_back(v.get<double>());
            }
          }
        }
      }
    }
    out->push_back(std::move(dataset));
  }
  return aifocore::Status::OkStatus();
}

void ParseOmeroChannels(const json& omero, std::vector<OmeroChannel>* out) {
  if (!omero.is_object())
    return;
  if (!omero.contains("channels"))
    return;
  const auto& channels = omero["channels"];
  if (!channels.is_array())
    return;
  out->clear();
  out->reserve(channels.size());
  for (const auto& entry : channels) {
    if (!entry.is_object())
      continue;
    OmeroChannel channel;
    if (entry.contains("label") && entry["label"].is_string()) {
      channel.label = entry["label"].get<std::string>();
    }
    if (entry.contains("color") && entry["color"].is_string()) {
      channel.color = OmeZarrMetadataParser::ParseOmeroColor(
          entry["color"].get<std::string>());
    }
    if (entry.contains("active") && entry["active"].is_boolean()) {
      channel.active = entry["active"].get<bool>();
    }
    if (entry.contains("window") && entry["window"].is_object()) {
      const auto& window = entry["window"];
      if (window.contains("start") && window["start"].is_number()) {
        channel.window_start = window["start"].get<double>();
      }
      if (window.contains("end") && window["end"].is_number()) {
        channel.window_end = window["end"].get<double>();
      }
    }
    out->push_back(std::move(channel));
  }
}

aifocore::Status ParseOmeBlock(const json& ome, OmeNgffMetadata* metadata) {
  if (!ome.is_object()) {
    return MakeError("OME-NGFF root: 'ome' attribute must be an object");
  }
  if (ome.contains("version") && ome["version"].is_string()) {
    metadata->version = ome["version"].get<std::string>();
  }
  if (!ome.contains("multiscales")) {
    return MakeError("OME-NGFF metadata missing 'multiscales'");
  }
  const auto& multiscales = ome["multiscales"];
  if (!multiscales.is_array() || multiscales.empty()) {
    return MakeError("OME-NGFF 'multiscales' must be a non-empty array");
  }
  const auto& first_ms = multiscales[0];
  if (!first_ms.is_object()) {
    return MakeError("OME-NGFF multiscales[0] must be an object");
  }
  if (!first_ms.contains("axes")) {
    return MakeError("OME-NGFF multiscales[0] missing 'axes'");
  }
  AIFOCORE_RETURN_IF_ERROR(ParseAxesField(first_ms["axes"], &metadata->axes));
  if (!first_ms.contains("datasets")) {
    return MakeError("OME-NGFF multiscales[0] missing 'datasets'");
  }
  AIFOCORE_RETURN_IF_ERROR(
      ParseDatasetsField(first_ms["datasets"], &metadata->datasets));
  if (ome.contains("omero")) {
    ParseOmeroChannels(ome["omero"], &metadata->channels);
  }
  return aifocore::Status::OkStatus();
}

uint8_t HexNibble(char c) {
  if (c >= '0' && c <= '9')
    return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f')
    return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F')
    return static_cast<uint8_t>(c - 'A' + 10);
  return 0xFF;
}

}  // namespace

std::optional<size_t> OmeNgffMetadata::ChannelAxis() const noexcept {
  return FindAxis(axes, "c");
}

std::optional<size_t> OmeNgffMetadata::YAxis() const noexcept {
  return FindAxis(axes, "y");
}

std::optional<size_t> OmeNgffMetadata::XAxis() const noexcept {
  return FindAxis(axes, "x");
}

aifocore::Result<OmeNgffMetadata> OmeZarrMetadataParser::ParseRootJson(
    std::string_view json_text) {
  json doc;
  try {
    doc = json::parse(json_text);
  } catch (const json::parse_error& e) {
    return MakeError(
        aifocore::fmt::format("Invalid JSON in root zarr.json: {}", e.what()));
  }
  OmeNgffMetadata metadata;
  if (doc.contains("attributes") && doc["attributes"].is_object() &&
      doc["attributes"].contains("ome")) {
    AIFOCORE_RETURN_IF_ERROR(
        ParseOmeBlock(doc["attributes"]["ome"], &metadata));
  } else if (doc.contains("multiscales")) {
    AIFOCORE_RETURN_IF_ERROR(ParseOmeBlock(doc, &metadata));
  } else {
    return MakeError(
        "OME-NGFF root document must contain 'attributes.ome' or "
        "'multiscales'");
  }
  if (metadata.YAxis() == std::nullopt || metadata.XAxis() == std::nullopt) {
    return MakeError("OME-NGFF axes must include both 'y' and 'x'");
  }
  return metadata;
}

aifocore::Result<ZarrArrayMetadata> OmeZarrMetadataParser::ParseArrayJson(
    std::string_view json_text) {
  json doc;
  try {
    doc = json::parse(json_text);
  } catch (const json::parse_error& e) {
    return MakeError(
        aifocore::fmt::format("Invalid JSON in array zarr.json: {}", e.what()));
  }
  if (!doc.is_object()) {
    return MakeError("Array zarr.json must be a JSON object");
  }
  if (!doc.contains("zarr_format")) {
    return MakeError("Array zarr.json missing 'zarr_format'");
  }
  if (doc.contains("node_type") &&
      doc["node_type"].get<std::string>() != "array") {
    return MakeError("Array zarr.json: 'node_type' is not 'array'");
  }
  ZarrArrayMetadata metadata;
  if (!doc.contains("shape") || !doc["shape"].is_array()) {
    return MakeError("Array zarr.json missing 'shape'");
  }
  metadata.shape.reserve(doc["shape"].size());
  for (const auto& v : doc["shape"]) {
    metadata.shape.push_back(v.get<uint64_t>());
  }
  if (!doc.contains("chunk_grid") || !doc["chunk_grid"].is_object()) {
    return MakeError("Array zarr.json missing 'chunk_grid'");
  }
  const auto& chunk_grid = doc["chunk_grid"];
  if (!chunk_grid.contains("configuration") ||
      !chunk_grid["configuration"].is_object()) {
    return MakeError("Array zarr.json missing chunk_grid.configuration");
  }
  const auto& chunk_cfg = chunk_grid["configuration"];
  if (!chunk_cfg.contains("chunk_shape") ||
      !chunk_cfg["chunk_shape"].is_array()) {
    return MakeError(
        "Array zarr.json missing chunk_grid.configuration.chunk_shape");
  }
  metadata.chunk_shape.reserve(chunk_cfg["chunk_shape"].size());
  for (const auto& v : chunk_cfg["chunk_shape"]) {
    metadata.chunk_shape.push_back(v.get<uint64_t>());
  }
  if (metadata.chunk_shape.size() != metadata.shape.size()) {
    return MakeError(
        "Array zarr.json: shape and chunk_shape must have same rank");
  }
  if (doc.contains("dimension_names") && doc["dimension_names"].is_array()) {
    metadata.dimension_names.reserve(doc["dimension_names"].size());
    for (const auto& v : doc["dimension_names"]) {
      metadata.dimension_names.push_back(v.is_string() ? v.get<std::string>()
                                                       : "");
    }
  }
  if (!doc.contains("data_type")) {
    return MakeError("Array zarr.json missing 'data_type'");
  }
  if (!doc["data_type"].is_string()) {
    return MakeError("Array zarr.json: 'data_type' must be a string");
  }
  AIFOCORE_ASSIGN_OR_RETURN(metadata.dtype,
                            ParseDtype(doc["data_type"].get<std::string>()));
  if (doc.contains("chunk_key_encoding") &&
      doc["chunk_key_encoding"].is_object()) {
    const auto& enc = doc["chunk_key_encoding"];
    if (enc.contains("configuration") && enc["configuration"].is_object()) {
      const auto& cfg = enc["configuration"];
      if (cfg.contains("separator") && cfg["separator"].is_string()) {
        const auto sep = cfg["separator"].get<std::string>();
        if (!sep.empty())
          metadata.chunk_key_separator = sep[0];
      }
    }
  }
  if (doc.contains("fill_value") && doc["fill_value"].is_number()) {
    metadata.fill_value = doc["fill_value"].get<double>();
  }
  if (!doc.contains("codecs") || !doc["codecs"].is_array()) {
    return MakeError("Array zarr.json missing 'codecs'");
  }
  metadata.codecs.reserve(doc["codecs"].size());
  for (const auto& codec_entry : doc["codecs"]) {
    if (!codec_entry.is_object() || !codec_entry.contains("name")) {
      return MakeError("Array zarr.json: codec entry missing 'name'");
    }
    ZarrCodec codec;
    codec.name = codec_entry["name"].get<std::string>();
    if (codec_entry.contains("configuration")) {
      codec.configuration = codec_entry["configuration"].dump();
    } else {
      codec.configuration = "{}";
    }
    metadata.codecs.push_back(std::move(codec));
  }
  return metadata;
}

aifocore::Result<ZarrDtype> OmeZarrMetadataParser::ParseDtype(
    std::string_view dtype) {
  ZarrDtype out;
  std::string lower(dtype);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::string_view view(lower);
  size_t digits_start = 0;
  if (view.starts_with("uint")) {
    out.kind = ZarrDtypeKind::kUInt;
    digits_start = 4;
  } else if (view.starts_with("int")) {
    out.kind = ZarrDtypeKind::kInt;
    digits_start = 3;
  } else if (view.starts_with("float")) {
    out.kind = ZarrDtypeKind::kFloat;
    digits_start = 5;
  } else if (view == "bool") {
    out.kind = ZarrDtypeKind::kBool;
    out.bits = 8;
    return out;
  } else {
    return MakeError(aifocore::fmt::format("Unsupported Zarr data_type '{}'",
                                           std::string(dtype)));
  }
  uint32_t bits = 0;
  auto digits_view = view.substr(digits_start);
  auto [ptr, ec] = std::from_chars(
      digits_view.data(), digits_view.data() + digits_view.size(), bits);
  if (ec != std::errc{} || bits == 0 || (bits % 8) != 0) {
    return MakeError(aifocore::fmt::format("Unsupported Zarr data_type '{}'",
                                           std::string(dtype)));
  }
  out.bits = bits;
  if (out.kind == ZarrDtypeKind::kFloat && bits != 16 && bits != 32 &&
      bits != 64) {
    return MakeError(aifocore::fmt::format(
        "Unsupported float width for Zarr data_type '{}'", std::string(dtype)));
  }
  return out;
}

std::optional<ColorRGB> OmeZarrMetadataParser::ParseOmeroColor(
    std::string_view hex) {
  if (hex.size() != 6 && hex.size() != 8)
    return std::nullopt;
  ColorRGB color;
  for (size_t i = 0; i < 3; ++i) {
    uint8_t hi = HexNibble(hex[i * 2]);
    uint8_t lo = HexNibble(hex[i * 2 + 1]);
    if (hi == 0xFF || lo == 0xFF)
      return std::nullopt;
    color[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return color;
}

}  // namespace fastslide::formats::omezarr
