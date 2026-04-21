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

#include "fastslide/readers/ometiff/ometiff_metadata_loader.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/ometiff/metadata_parser.h"

namespace fastslide {
namespace {

std::vector<uint32_t> RootPagesInOrder(const simpletiff::TiffIndex& index) {
  std::vector<uint32_t> roots;
  roots.reserve(index.NumPages());
  for (uint32_t i = 0; i < index.NumPages(); ++i) {
    const auto& page = index.Page(i);
    if (!page.parent_page_index.has_value() &&
        (page.new_subfile_type & 0x1u) == 0) {
      roots.push_back(i);
    }
  }
  return roots;
}

}  // namespace

aifocore::Status OmetiffMetadataLoader::LoadMetadata(
    const simpletiff::TiffIndex& tiff_index, OmeSlideMetadata& metadata,
    std::vector<OmeTiffChannelInfo>& channels,
    std::vector<OmeTiffLevelInfo>& pyramid,
    std::map<std::string, uint32_t>& associated_images) {
  (void)associated_images;
  channels.clear();
  pyramid.clear();

  if (tiff_index.NumPages() == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-TIFF: empty TIFF index");
  }

  // OME-XML is usually stored in ImageDescription on the first page.
  const std::string& xml = tiff_index.Page(0).description;
  if (xml.empty() ||
      !formats::ometiff::OmeMetadataParser::LooksLikeOmeXml(xml)) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "OME-TIFF: missing OME-XML in ImageDescription");
  }

  AIFOCORE_ASSIGN_OR_RETURN(const auto ome,
                            formats::ometiff::OmeMetadataParser::Parse(xml));

  if (ome.pixels.size_c == 0 || ome.pixels.size_x == 0 ||
      ome.pixels.size_y == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-TIFF: invalid Pixels dimensions");
  }

  metadata.mpp_x = ome.pixels.physical_size_x.value_or(0.0);
  metadata.mpp_y = ome.pixels.physical_size_y.value_or(0.0);

  // Root pages represent full-resolution planes.
  const std::vector<uint32_t> roots = RootPagesInOrder(tiff_index);
  if (roots.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-TIFF: no root IFDs found");
  }

  // OME-TIFF brightfield is commonly represented as SizeC=3 + Interleaved=true,
  // but stored as a single IFD with SamplesPerPixel=3 (RGB). In that case,
  // there is only ONE plane at level 0, not three separate planes.
  const auto& root0 = tiff_index.Page(roots[0]);
  const bool is_interleaved_rgb_single_plane =
      (roots.size() == 1) && (ome.pixels.size_c == 3) &&
      (ome.pixels.interleaved || root0.samples_per_pixel == 3) &&
      (root0.samples_per_pixel == 3);

  const uint32_t effective_channels =
      is_interleaved_rgb_single_plane ? 1u : ome.pixels.size_c;

  if (roots.size() < effective_channels) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format(
            "OME-TIFF: expected at least {} root planes, got {}",
            effective_channels, roots.size()));
  }

  // Build channel list (prefer OME Channel list; fallback to indices).
  channels.reserve(effective_channels);
  if (is_interleaved_rgb_single_plane) {
    OmeTiffChannelInfo ch;
    ch.page = roots[0];
    ch.name = "RGB";
    ch.biomarker = "RGB Brightfield";
    ch.color = ColorRGB(255, 255, 255);
    channels.push_back(std::move(ch));
  } else {
    for (uint32_t c = 0; c < effective_channels; ++c) {
      OmeTiffChannelInfo ch;
      ch.page = roots[c];
      if (c < ome.channels.size() && !ome.channels[c].name.empty()) {
        ch.name = ome.channels[c].name;
        ch.biomarker = ome.channels[c].name;
        ch.color = formats::ometiff::OmeMetadataParser::ColorFromOmeArgb(
            ome.channels[c].color_argb, ColorRGB(255, 255, 255));
      } else {
        ch.name = aifocore::fmt::format("C{}", c);
        ch.biomarker = ch.name;
        ch.color = ColorRGB(255, 255, 255);
      }

      channels.push_back(std::move(ch));
    }
  }

  // Determine pyramid levels from OME PyramidResolution.
  // Level 0 is full-resolution, then K=1..N are reduced levels.
  std::vector<std::pair<uint32_t, uint32_t>> level_sizes;
  level_sizes.push_back({ome.pixels.size_x, ome.pixels.size_y});
  for (const auto& [k, wh] : ome.pyramid_resolutions) {
    level_sizes.push_back({wh.first, wh.second});
  }

  // Level 0 mapping.
  OmeTiffLevelInfo level0;
  level0.Reserve(effective_channels);
  for (uint32_t c = 0; c < effective_channels; ++c) {
    level0.pages.push_back(roots[c]);
  }
  level0.size = {level_sizes[0].first, level_sizes[0].second};
  {
    const auto& page0 = tiff_index.Page(level0.pages[0]);
    level0.tiled = (page0.storage == simpletiff::Storage::kTiles);
    level0.allow_random_access = level0.tiled;
  }
  pyramid.push_back(std::move(level0));

  const size_t num_reduced_levels = level_sizes.size() - 1;
  if (num_reduced_levels == 0) {
    return aifocore::Status::OkStatus();
  }

  // Strategy A: per-plane SubIFDs (each root has N sub pages).
  bool per_plane_ok = true;
  for (uint32_t c = 0; c < effective_channels; ++c) {
    if (tiff_index.ChildPagesForPage(roots[c]).size() < num_reduced_levels) {
      per_plane_ok = false;
      break;
    }
  }

  if (per_plane_ok) {
    for (size_t level = 1; level < level_sizes.size(); ++level) {
      OmeTiffLevelInfo lvl;
      lvl.Reserve(effective_channels);
      for (uint32_t c = 0; c < effective_channels; ++c) {
        const auto child = tiff_index.ChildPagesForPage(roots[c])[level - 1];
        lvl.pages.push_back(child);
      }
      lvl.size = {level_sizes[level].first, level_sizes[level].second};
      const auto& ph = tiff_index.Page(lvl.pages[0]);
      lvl.tiled = (ph.storage == simpletiff::Storage::kTiles);
      lvl.allow_random_access = lvl.tiled;
      pyramid.push_back(std::move(lvl));
    }
    return aifocore::Status::OkStatus();
  }

  // Strategy B: a single root contains all reduced planes for all channels.
  // This matches Bio-Formats CODEX layouts often seen in the wild.
  std::optional<uint32_t> aggregator_root;
  for (uint32_t r : roots) {
    const size_t n = tiff_index.ChildPagesForPage(r).size();
    if (n == num_reduced_levels * effective_channels) {
      aggregator_root = r;
      break;
    }
  }

  if (aggregator_root.has_value()) {
    const auto children = tiff_index.ChildPagesForPage(*aggregator_root);
    for (size_t level = 1; level < level_sizes.size(); ++level) {
      OmeTiffLevelInfo lvl;
      lvl.Reserve(effective_channels);
      const size_t base = (level - 1) * effective_channels;
      for (uint32_t c = 0; c < effective_channels; ++c) {
        lvl.pages.push_back(children[base + c]);
      }
      lvl.size = {level_sizes[level].first, level_sizes[level].second};
      const auto& ph = tiff_index.Page(lvl.pages[0]);
      lvl.tiled = (ph.storage == simpletiff::Storage::kTiles);
      lvl.allow_random_access = lvl.tiled;
      pyramid.push_back(std::move(lvl));
    }
    return aifocore::Status::OkStatus();
  }

  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kInvalidArgument,
      "OME-TIFF: unable to map pyramid levels to SubIFDs (unsupported layout)");
}

}  // namespace fastslide
