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

/// @file czi_scene_image.cpp
/// @brief One navigable CZI scene exposed as a `SlideImage`.
///
/// Based on the BSD-3-Clause `czifile` library (Christoph Gohlke).

#include "fastslide/readers/czi/czi_scene_image.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/czi/czi.h"
#include "fastslide/readers/czi/czi_exec_context.h"
#include "fastslide/readers/czi/czi_level_info.h"
#include "fastslide/readers/czi/czi_parse.h"
#include "fastslide/readers/czi/czi_plan_builder.h"
#include "fastslide/readers/czi/czi_plan_context.h"
#include "fastslide/readers/czi/czi_tile_executor.h"
#include "fastslide/utilities/colors.h"

namespace fastslide {

namespace {

constexpr uint32_t kDefaultTileEdge = 512;

}  // namespace

CziSceneImage::CziSceneImage(
    const CziReader& container, int32_t scene_id, std::string name,
    std::vector<uint32_t> subblock_indices, double mpp_x, double mpp_y,
    double objective_magnification, std::string scanner_model,
    std::optional<double> z_spacing_um, std::optional<double> t_interval_s)
    : container_(container),
      scene_id_(scene_id),
      name_(std::move(name)),
      subblock_indices_(std::move(subblock_indices)),
      z_spacing_um_(z_spacing_um),
      t_interval_s_(t_interval_s) {
  const std::span<const CziSubblockInfo> subblocks = container_.SubblockSpan();

  // Scene origin: the top-left of this scene's tiles, so the scene's own
  // pyramid starts at (0, 0) regardless of where it sits on the slide.
  origin_x_ = std::numeric_limits<int32_t>::max();
  origin_y_ = std::numeric_limits<int32_t>::max();
  for (uint32_t idx : subblock_indices_) {
    const auto& sb = subblocks[idx];
    origin_x_ = std::min(origin_x_, sb.x);
    origin_y_ = std::min(origin_y_, sb.y);
  }
  if (subblock_indices_.empty()) {
    origin_x_ = 0;
    origin_y_ = 0;
  }

  // Collect the distinct downsample factors; each becomes a pyramid level.
  std::set<int32_t> ds_set;
  for (uint32_t idx : subblock_indices_) {
    const int32_t ds = subblocks[idx].downsample;
    if (ds > 0) {
      ds_set.insert(ds);
    }
  }
  downsamples_.assign(ds_set.begin(), ds_set.end());
  if (downsamples_.empty()) {
    downsamples_.push_back(1);
  }

  level_subblocks_.assign(downsamples_.size(), {});
  for (uint32_t idx : subblock_indices_) {
    const int32_t ds = subblocks[idx].downsample;
    const auto it =
        std::lower_bound(downsamples_.begin(), downsamples_.end(), ds);
    if (it == downsamples_.end() || *it != ds) {
      continue;
    }
    const size_t level = static_cast<size_t>(it - downsamples_.begin());
    level_subblocks_[level].push_back(idx);
  }

  // Distinct focal-plane (Z) and time-point (T) starts present in this scene.
  // These collapse to {0} for plain 2D scenes, so a single-plane scene keeps a
  // 1x1 stack and behaves exactly as before. A plane selector indexes them.
  {
    std::vector<int32_t> z_starts;
    std::vector<int32_t> t_starts;
    z_starts.reserve(subblock_indices_.size());
    t_starts.reserve(subblock_indices_.size());
    for (uint32_t idx : subblock_indices_) {
      z_starts.push_back(subblocks[idx].z);
      t_starts.push_back(subblocks[idx].t);
    }
    z_values_ = czi::SortedUniqueAxis(z_starts);
    t_values_ = czi::SortedUniqueAxis(t_starts);
  }
  if (z_values_.empty()) {
    z_values_.push_back(0);
  }
  if (t_values_.empty()) {
    t_values_.push_back(0);
  }

  // Distinct channel (C) starts present in this scene. More than one channel
  // means the scene is a multi-channel fluorescence stack: each channel is an
  // independent plane and must not be collapsed into an RGB triplet.
  {
    std::vector<int32_t> c_starts;
    c_starts.reserve(subblock_indices_.size());
    for (uint32_t idx : subblock_indices_) {
      c_starts.push_back(subblocks[idx].c);
    }
    channel_values_ = czi::SortedUniqueAxis(c_starts);
  }
  if (channel_values_.empty()) {
    channel_values_.push_back(0);
  }
  is_spectral_ = channel_values_.size() > 1;

  // Native output dtype: any 16-bit pixel type (GRAY16 = 1, BGR48 = 4) makes
  // the whole scene 16-bit. Pixel type is uniform per scene in practice.
  for (uint32_t idx : subblock_indices_) {
    const int32_t pt = subblocks[idx].pixel_type;
    if (pt == 1 || pt == 4) {
      data_type_ = DataType::kUInt16;
      break;
    }
  }

  // Level-0 content extent (scene-local). The extent is taken from the
  // full-resolution (downsample == 1) tiles only, which bound the real scene
  // footprint; coarser pyramid tiles can extend past it. A fallback covers the
  // rare scene that ships no native-resolution tiles. (The "bound the scene by
  // its bottom-level tiles" idea is inherent to the CZI mosaic layout; here it
  // is computed per scene rather than mutating a shared subblock array.)
  int32_t right = 0;
  int32_t bottom = 0;
  bool have_full_res = false;
  for (uint32_t idx : subblock_indices_) {
    const auto& sb = subblocks[idx];
    if (sb.downsample != 1) {
      continue;
    }
    have_full_res = true;
    right = std::max<int32_t>(right,
                              (sb.x - origin_x_) + static_cast<int32_t>(sb.w));
    bottom = std::max<int32_t>(bottom,
                               (sb.y - origin_y_) + static_cast<int32_t>(sb.h));
  }
  if (!have_full_res) {
    for (uint32_t idx : subblock_indices_) {
      const auto& sb = subblocks[idx];
      right = std::max<int32_t>(
          right, (sb.x - origin_x_) + static_cast<int32_t>(sb.w));
      bottom = std::max<int32_t>(
          bottom, (sb.y - origin_y_) + static_cast<int32_t>(sb.h));
    }
  }
  base_size_ = ImageDimensions{static_cast<uint32_t>(std::max(0, right)),
                               static_cast<uint32_t>(std::max(0, bottom))};

  properties_.scanner_model = std::move(scanner_model);
  if (mpp_x > 0.0 && mpp_y > 0.0) {
    properties_.mpp = {mpp_x, mpp_y};
  }
  if (objective_magnification > 0.0) {
    properties_.objective_magnification = objective_magnification;
  }
  properties_.bounds = SlideBounds(0, 0, base_size_[0], base_size_[1]);
}

int CziSceneImage::GetLevelCount() const {
  return static_cast<int>(downsamples_.size());
}

aifocore::Result<LevelInfo> CziSceneImage::GetLevelInfo(int level) const {
  if (level < 0 || level >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("Invalid level: {}", level));
  }
  const auto ds =
      static_cast<uint32_t>(downsamples_[static_cast<size_t>(level)]);
  LevelInfo info{};
  info.dimensions = {std::max<uint32_t>(1, base_size_[0] / ds),
                     std::max<uint32_t>(1, base_size_[1] / ds)};
  info.downsample_factor = static_cast<double>(ds);
  return info;
}

uint32_t CziSceneImage::ChannelIndexOf(int32_t c) const {
  const auto it =
      std::lower_bound(channel_values_.begin(), channel_values_.end(), c);
  if (it == channel_values_.end() || *it != c) {
    return 0;
  }
  return static_cast<uint32_t>(it - channel_values_.begin());
}

ImageFormat CziSceneImage::GetImageFormat() const {
  return is_spectral_ ? ImageFormat::kSpectral : ImageFormat::kRGB;
}

std::vector<ChannelMetadata> CziSceneImage::GetChannelMetadata() const {
  // Multi-channel fluorescence: surface one entry per channel, preferring the
  // names/colors parsed from the CZI XML and falling back to generated names.
  if (is_spectral_) {
    const auto& parsed = container_.MetadataChannels();
    std::vector<ChannelMetadata> out;
    out.reserve(channel_values_.size());
    for (size_t i = 0; i < channel_values_.size(); ++i) {
      if (i < parsed.size() && !parsed[i].name.empty()) {
        out.push_back(parsed[i]);
      } else {
        ChannelMetadata md;
        md.name = aifocore::fmt::format("Channel {}", i);
        out.push_back(std::move(md));
      }
    }
    return out;
  }

  // Single-channel (grayscale broadcast or native BGR): legacy RGB surface.
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

ImageDimensions CziSceneImage::GetTileSize() const {
  if (level_subblocks_.empty() || level_subblocks_[0].empty()) {
    return {kDefaultTileEdge, kDefaultTileEdge};
  }
  const auto& sb = container_.SubblockSpan()[level_subblocks_[0][0]];
  return {sb.w, sb.h};
}

StackInfo CziSceneImage::GetStackInfo() const {
  StackInfo info;
  info.z_count = static_cast<uint32_t>(z_values_.size());
  info.t_count = static_cast<uint32_t>(t_values_.size());
  info.z_spacing_um = z_spacing_um_;
  info.t_interval_s = t_interval_s_;
  return info;
}

aifocore::Result<core::TilePlan> CziSceneImage::PrepareRequest(
    const core::TileRequest& request) const {
  AIFOCORE_ASSIGN_OR_RETURN(const auto level_info, GetLevelInfo(request.level));

  // Validate the focal/time plane selector against this scene's stack extent,
  // mirroring the level check above.
  if (request.plane.z >= z_values_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format("Z plane {} out of range [0, {})",
                              request.plane.z, z_values_.size()));
  }
  if (request.plane.t >= t_values_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kOutOfRange,
        aifocore::fmt::format("T plane {} out of range [0, {})",
                              request.plane.t, t_values_.size()));
  }

  AIFOCORE_ASSIGN_OR_RETURN(
      const auto spatial_index,
      GetSpatialIndex(request.level, request.plane.z, request.plane.t));
  const CziPlanContext context{
      .level_count = GetLevelCount(),
      .level_info = level_info,
      .spatial_index = spatial_index,
      .data_type = data_type_,
      .channel_count = static_cast<uint32_t>(channel_values_.size()),
      .is_spectral = is_spectral_,
  };
  return CziPlanBuilder::BuildPlan(request, context);
}

aifocore::Status CziSceneImage::ExecutePlan(const core::TilePlan& plan,
                                            runtime::Canvas& canvas) const {
  const CziExecContext context(container_.GetFilename(),
                               container_.SubblockSpan(), container_.GetCache(),
                               is_spectral_);
  return CziTileExecutor::ExecutePlan(plan, context, canvas);
}

aifocore::Result<std::shared_ptr<czi::CziSpatialIndex>>
CziSceneImage::GetSpatialIndex(int level, uint32_t z, uint32_t t) const {
  std::lock_guard<std::mutex> lock(spatial_index_mutex_);
  if (level < 0 || level >= GetLevelCount()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid level for spatial index");
  }
  if (z >= z_values_.size() || t >= t_values_.size()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Invalid plane for spatial index");
  }
  const auto level_idx = static_cast<size_t>(level);
  const std::array<uint32_t, 3> key{static_cast<uint32_t>(level), z, t};
  if (const auto it = spatial_indices_.find(key);
      it != spatial_indices_.end()) {
    return it->second;
  }

  const std::span<const CziSubblockInfo> subblocks = container_.SubblockSpan();
  const int32_t z_value = z_values_[z];
  const int32_t t_value = t_values_[t];

  // Keep only the subblocks of this focal plane and time point. Without this
  // filter a multi-Z/T level would composite every plane into one canvas.
  std::vector<uint32_t> sb_indices;
  sb_indices.reserve(level_subblocks_[level_idx].size());
  for (uint32_t idx : level_subblocks_[level_idx]) {
    const auto& sb = subblocks[idx];
    if (sb.z == z_value && sb.t == t_value) {
      sb_indices.push_back(idx);
    }
  }
  if (sb_indices.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "No tiles at this level/plane");
  }

  const auto ds = static_cast<double>(downsamples_[level_idx]);

  // Cell step from the largest tile in this level (level coordinates).
  uint32_t max_dim = 1;
  for (uint32_t idx : sb_indices) {
    const auto& sb = subblocks[idx];
    max_dim = std::max(max_dim, std::max(sb.w, sb.h));
  }

  std::vector<czi::SpatialTile> tiles;
  tiles.reserve(sb_indices.size());
  for (uint32_t idx : sb_indices) {
    const auto& sb = subblocks[idx];
    // Translate to scene-local origin, then to this level's coordinates.
    const double tx = static_cast<double>(sb.x - origin_x_) / ds;
    const double ty = static_cast<double>(sb.y - origin_y_) / ds;
    czi::SpatialTile st{};
    st.info.subblock_index = idx;
    st.info.width = sb.w;
    st.info.height = sb.h;
    st.info.channel = ChannelIndexOf(sb.c);
    st.bbox.min = {tx, ty};
    st.bbox.max = {tx + sb.w, ty + sb.h};
    tiles.push_back(st);
  }

  AIFOCORE_ASSIGN_OR_RETURN(
      auto index, czi::CziSpatialIndex::Build(std::move(tiles),
                                              static_cast<double>(max_dim)));
  spatial_indices_[key] = index;
  return index;
}

}  // namespace fastslide
