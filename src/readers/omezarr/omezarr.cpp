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

#include "fastslide/readers/omezarr/omezarr.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <iostream>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/omezarr/omezarr_codec.h"
#include "fastslide/readers/omezarr/omezarr_exec_context.h"
#include "fastslide/readers/omezarr/omezarr_metadata.h"
#include "fastslide/readers/omezarr/omezarr_plan_builder.h"
#include "fastslide/readers/omezarr/omezarr_tile_executor.h"

namespace fs = std::filesystem;

namespace fastslide {

namespace {

aifocore::Result<std::string> ReadFileToString(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Failed to open '{}'", path.string()));
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

/// @brief Indices of the y, x, and (optional) channel axis within a Zarr
/// array's `shape` / `chunk_shape` arrays.
struct AxisLayout {
  size_t y_axis = 0;
  size_t x_axis = 0;
  size_t c_axis = static_cast<size_t>(-1);  ///< SIZE_MAX = no channel axis
};

/// @brief Verify that `layout` is consistent with the actual `shape`.
///
/// "Consistent" means y/x/c map to distinct axes, the channel axis (if any)
/// has a plausibly small size, and any axes not used by y/x/c are singletons.
bool LayoutIsConsistent(const AxisLayout& layout,
                        const std::vector<uint64_t>& shape) {
  // Reasonable upper bound for any real channel count (CODEX/CyCIF tops out
  // around 60-70; spectral imaging can reach a few hundred). Anything bigger
  // is almost certainly a misdeclared spatial axis.
  constexpr uint64_t kMaxPlausibleChannels = 1024;
  const bool has_c = layout.c_axis != static_cast<size_t>(-1);

  if (layout.y_axis == layout.x_axis)
    return false;
  if (has_c &&
      (layout.c_axis == layout.y_axis || layout.c_axis == layout.x_axis)) {
    return false;
  }
  if (layout.y_axis >= shape.size() || layout.x_axis >= shape.size()) {
    return false;
  }
  if (has_c && layout.c_axis >= shape.size())
    return false;
  if (has_c && shape[layout.c_axis] > kMaxPlausibleChannels)
    return false;

  for (size_t i = 0; i < shape.size(); ++i) {
    if (i == layout.y_axis || i == layout.x_axis)
      continue;
    if (has_c && i == layout.c_axis)
      continue;
    if (shape[i] != 1)
      return false;
  }
  return true;
}

/// @brief Resolve the y/x/c axis indices, repairing common writer bugs.
///
/// Some OME-Zarr writers (notably for RGB pyramidal slides converted from
/// TIFF) declare `axes: [c, y, x]` while actually storing the array as
/// `(y, x, c)`. When the declared layout is inconsistent with the shape (e.g.
/// `c_axis` with size > 1024), we search permutations of the three involved
/// axis indices to find one that matches.
aifocore::Result<AxisLayout> ResolveAxisLayout(
    AxisLayout declared, const std::vector<uint64_t>& shape,
    const std::string& dataset_path) {
  if (LayoutIsConsistent(declared, shape))
    return declared;

  std::vector<size_t> indices = {declared.y_axis, declared.x_axis};
  const bool has_c = declared.c_axis != static_cast<size_t>(-1);
  if (has_c)
    indices.push_back(declared.c_axis);
  std::sort(indices.begin(), indices.end());
  do {
    AxisLayout cand;
    cand.y_axis = indices[0];
    cand.x_axis = indices[1];
    cand.c_axis = has_c ? indices[2] : static_cast<size_t>(-1);
    if (LayoutIsConsistent(cand, shape)) {
      std::cerr << "[OME-Zarr] dataset '" << dataset_path
                << "' has inconsistent axis metadata; using inferred layout "
                << "(y=" << cand.y_axis << ", x=" << cand.x_axis
                << ", c=" << (has_c ? std::to_string(cand.c_axis) : "none")
                << ") for shape [";
      for (size_t i = 0; i < shape.size(); ++i) {
        if (i)
          std::cerr << ", ";
        std::cerr << shape[i];
      }
      std::cerr << "]\n";
      return cand;
    }
  } while (std::next_permutation(indices.begin(), indices.end()));

  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kInvalidArgument,
      aifocore::fmt::format(
          "OME-Zarr dataset '{}' axis layout is inconsistent with its shape "
          "and no plausible permutation was found",
          dataset_path));
}

DataType DataTypeFromZarr(formats::omezarr::ZarrDtype dtype) {
  using formats::omezarr::ZarrDtypeKind;
  if (dtype.kind == ZarrDtypeKind::kFloat) {
    if (dtype.bits == 32)
      return DataType::kFloat32;
    if (dtype.bits == 64)
      return DataType::kFloat32;
    return DataType::kFloat32;
  }
  if (dtype.kind == ZarrDtypeKind::kUInt ||
      dtype.kind == ZarrDtypeKind::kBool) {
    if (dtype.bits == 8)
      return DataType::kUInt8;
    if (dtype.bits == 16)
      return DataType::kUInt16;
  }
  return DataType::kUInt8;
}

}  // namespace

aifocore::Result<std::unique_ptr<OmeZarrReader>> OmeZarrReader::Create(
    std::string_view path) {
  std::unique_ptr<OmeZarrReader> reader(new OmeZarrReader(std::string(path)));
  AIFOCORE_RETURN_IF_ERROR(reader->LoadMetadata());
  return reader;
}

OmeZarrReader::OmeZarrReader(std::string path)
    : filename_(std::move(path)), root_dir_(filename_) {}

aifocore::Status OmeZarrReader::LoadMetadata() {
  if (!fs::exists(root_dir_)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("OME-Zarr root '{}' not found", filename_));
  }
  if (!fs::is_directory(root_dir_)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("OME-Zarr root '{}' is not a directory",
                              filename_));
  }

  const fs::path root_json = root_dir_ / "zarr.json";
  if (!fs::exists(root_json)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Missing zarr.json in '{}'", filename_));
  }

  AIFOCORE_ASSIGN_OR_RETURN(const std::string root_text,
                            ReadFileToString(root_json));
  AIFOCORE_ASSIGN_OR_RETURN(
      ngff_, formats::omezarr::OmeZarrMetadataParser::ParseRootJson(root_text));

  const auto y_axis_opt = ngff_.YAxis();
  const auto x_axis_opt = ngff_.XAxis();
  if (!y_axis_opt.has_value() || !x_axis_opt.has_value()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-Zarr requires 'y' and 'x' axes");
  }
  const auto c_axis_opt = ngff_.ChannelAxis();

  pyramid_.clear();
  pyramid_.reserve(ngff_.datasets.size());
  for (const auto& dataset : ngff_.datasets) {
    OmeZarrLevelInfo level;
    level.array_dir = (root_dir_ / dataset.path).string();
    const fs::path array_json_path = fs::path(level.array_dir) / "zarr.json";
    AIFOCORE_ASSIGN_OR_RETURN(const std::string array_text,
                              ReadFileToString(array_json_path));
    AIFOCORE_ASSIGN_OR_RETURN(
        level.array_metadata,
        formats::omezarr::OmeZarrMetadataParser::ParseArrayJson(array_text));
    if (level.array_metadata.shape.size() != ngff_.axes.size()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "OME-Zarr dataset '{}' rank {} does not match axes count {}",
              dataset.path, level.array_metadata.shape.size(),
              ngff_.axes.size()));
    }
    AxisLayout declared{*y_axis_opt, *x_axis_opt,
                        c_axis_opt.value_or(static_cast<size_t>(-1))};
    AIFOCORE_ASSIGN_OR_RETURN(
        const AxisLayout resolved,
        ResolveAxisLayout(declared, level.array_metadata.shape, dataset.path));
    level.y_axis = resolved.y_axis;
    level.x_axis = resolved.x_axis;
    level.c_axis = resolved.c_axis;
    level.y_size = level.array_metadata.shape[level.y_axis];
    level.x_size = level.array_metadata.shape[level.x_axis];
    level.chunk_y = level.array_metadata.chunk_shape[level.y_axis];
    level.chunk_x = level.array_metadata.chunk_shape[level.x_axis];
    if (level.c_axis != static_cast<size_t>(-1)) {
      level.c_size = level.array_metadata.shape[level.c_axis];
      level.chunk_c = level.array_metadata.chunk_shape[level.c_axis];
    } else {
      level.c_size = 1;
      level.chunk_c = 1;
    }
    if (level.x_size > std::numeric_limits<uint32_t>::max() ||
        level.y_size > std::numeric_limits<uint32_t>::max()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          "OME-Zarr level dimensions exceed uint32_t range");
    }
    level.size = {static_cast<uint32_t>(level.x_size),
                  static_cast<uint32_t>(level.y_size)};
    AIFOCORE_ASSIGN_OR_RETURN(
        level.codec_chain,
        formats::omezarr::ZarrCodecChain::Build(
            level.array_metadata.codecs, level.array_metadata.chunk_shape,
            level.array_metadata.dtype));
    pyramid_.push_back(std::move(level));
  }

  if (pyramid_.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-Zarr has no pyramid levels");
  }

  data_type_ = DataTypeFromZarr(pyramid_[0].array_metadata.dtype);

  channels_.clear();
  const size_t channel_count = pyramid_[0].c_size;
  channels_.reserve(channel_count);
  for (size_t i = 0; i < channel_count; ++i) {
    ChannelMetadata md;
    if (i < ngff_.channels.size()) {
      md.name = ngff_.channels[i].label;
      md.biomarker = md.name;
      if (ngff_.channels[i].color.has_value()) {
        md.color = *ngff_.channels[i].color;
      }
    }
    if (md.name.empty()) {
      md.name = aifocore::fmt::format("Channel {}", i);
      md.biomarker = md.name;
    }
    channels_.push_back(std::move(md));
  }

  if (channel_count == 3 && pyramid_[0].c_axis != static_cast<size_t>(-1)) {
    format_ = ImageFormat::kRGB;
    planar_config_ = PlanarConfig::kSeparate;
  } else if (channel_count == 1) {
    format_ = ImageFormat::kGray;
    planar_config_ = PlanarConfig::kSeparate;
  } else {
    format_ = ImageFormat::kSpectral;
    planar_config_ = PlanarConfig::kSeparate;
  }

  PopulateSlideProperties();
  return aifocore::Status::OkStatus();
}

void OmeZarrReader::PopulateSlideProperties() {
  properties_ = SlideProperties{};
  if (pyramid_.empty())
    return;
  // Try to derive mpp (µm/px) from the first multiscale dataset's coordinate
  // transformation scale. OME-NGFF axis units are typically micrometers when
  // the axis type is 'space', but we don't enforce unit conversion here.
  const auto& dataset0 = ngff_.datasets.front();
  const auto& level0 = pyramid_.front();
  if (dataset0.scale.size() == ngff_.axes.size()) {
    properties_.mpp[0] = dataset0.scale[level0.x_axis];
    properties_.mpp[1] = dataset0.scale[level0.y_axis];
  }
  properties_.scanner_model = "OME-ZARR";
  properties_.bounds = SlideBounds(0, 0, level0.size[0], level0.size[1]);
}

int OmeZarrReader::GetLevelCount() const {
  return static_cast<int>(pyramid_.size());
}

aifocore::Result<LevelInfo> OmeZarrReader::GetLevelInfo(int level) const {
  if (level < 0 || static_cast<size_t>(level) >= pyramid_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }
  const auto& pyr = pyramid_[level];
  LevelInfo info;
  info.dimensions = pyr.size;
  if (level == 0) {
    info.downsample_factor = 1.0;
  } else {
    const double sx = static_cast<double>(pyramid_[0].size[0]) /
                      static_cast<double>(pyr.size[0]);
    const double sy = static_cast<double>(pyramid_[0].size[1]) /
                      static_cast<double>(pyr.size[1]);
    info.downsample_factor = (sx + sy) / 2.0;
  }
  return info;
}

std::vector<ChannelMetadata> OmeZarrReader::GetChannelMetadata() const {
  return channels_;
}

aifocore::Result<ImageDimensions> OmeZarrReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  (void)name;
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                              "OME-Zarr does not expose associated images");
}

aifocore::Result<RGBImage> OmeZarrReader::ReadAssociatedImage(
    std::string_view name) const {
  (void)name;
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                              "OME-Zarr does not expose associated images");
}

Metadata OmeZarrReader::GetMetadata() const {
  Metadata md;
  md[std::string(MetadataKeys::kFormat)] = std::string("OME-ZARR");
  md[std::string(MetadataKeys::kLevels)] = pyramid_.size();
  md[std::string(MetadataKeys::kChannels)] = channels_.size();
  md[std::string(MetadataKeys::kMppX)] = properties_.mpp[0];
  md[std::string(MetadataKeys::kMppY)] = properties_.mpp[1];
  return md;
}

ImageDimensions OmeZarrReader::GetTileSize() const {
  if (pyramid_.empty())
    return ImageDimensions{512, 512};
  const auto& level0 = pyramid_[0];
  return ImageDimensions{static_cast<uint32_t>(level0.chunk_x),
                         static_cast<uint32_t>(level0.chunk_y)};
}

aifocore::Result<core::TilePlan> OmeZarrReader::PrepareRequest(
    const core::TileRequest& request) const {
  return OmeZarrPlanBuilder::BuildPlan(request, pyramid_, planar_config_,
                                       data_type_, visible_channels_);
}

aifocore::Status OmeZarrReader::ExecutePlan(const core::TilePlan& plan,
                                            runtime::Canvas& canvas) const {
  const OmeZarrExecContext context{
      .pyramid = pyramid_,
      .cache = GetCache(),
      .filename = filename_,
  };
  return OmeZarrTileExecutor::ExecutePlan(plan, context, canvas);
}

}  // namespace fastslide
