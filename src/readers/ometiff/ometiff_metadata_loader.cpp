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
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/ometiff/metadata_parser.h"

namespace fastslide {
namespace {

using formats::ometiff::OmeChannel;
using formats::ometiff::OmeMetadata;
using formats::ometiff::OmeTiffData;

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

/// @brief Linear plane index for (c, z, t) under an OME `DimensionOrder`.
///
/// The order string starts with "XY"; the remaining letters list the plane
/// axes from fastest- to slowest-varying. This matches how OME-TIFF lays out
/// IFDs and how `<TiffData>` `PlaneCount` blocks advance. Defaults to the OME
/// default "XYCZT" when the attribute is missing.
uint32_t LinearPlaneIndex(std::string_view order, uint32_t nc, uint32_t nz,
                          uint32_t nt, uint32_t c, uint32_t z, uint32_t t) {
  if (order.size() < 5) {
    order = "XYCZT";
  }
  uint32_t stride = 1;
  uint32_t idx = 0;
  for (char ch : order) {
    switch (ch) {
      case 'Z':
        idx += z * stride;
        stride *= nz;
        break;
      case 'T':
        idx += t * stride;
        stride *= nt;
        break;
      case 'C':
        idx += c * stride;
        stride *= nc;
        break;
      default:  // X, Y (and any unexpected letter) carry no plane stride.
        break;
    }
  }
  return idx;
}

/// @brief Restrict a multi-file OME dataset to the planes physically present
/// in the opened file.
///
/// Companion/multi-file OME-TIFFs (e.g. one file per channel/time point) embed
/// the *whole* acquisition's dimensions in `<Pixels>` and use a `<UUID
/// FileName>` on every `<TiffData>` to point each plane at its host file. When
/// the sibling files are not opened, only the `<TiffData>` whose UUID matches
/// this file's root UUID (or that carry no UUID at all) are readable. This
/// recomputes the effective C/Z/T from that self-owned subset and remaps the
/// surviving planes onto a compact 0-based index space, matching Bio-Formats'
/// behaviour when companion files are absent.
///
/// Returns `ome` unchanged for ordinary single-file datasets, or when the
/// self-owned planes do not form a clean (C x Z x T) sub-box.
OmeMetadata RestrictToSelfFile(const OmeMetadata& ome) {
  if (ome.tiff_data.empty()) {
    return ome;
  }

  const auto is_self = [&](const OmeTiffData& td) {
    return td.uuid.empty() ||
           (!ome.self_uuid.empty() && td.uuid == ome.self_uuid);
  };
  const bool any_external =
      std::any_of(ome.tiff_data.begin(), ome.tiff_data.end(),
                  [&](const OmeTiffData& td) { return !is_self(td); });
  if (!any_external) {
    return ome;  // Single-file dataset: nothing to restrict.
  }

  const uint32_t fc = std::max(1u, ome.pixels.size_c);
  const uint32_t fz = std::max(1u, ome.pixels.size_z);
  const uint32_t ft = std::max(1u, ome.pixels.size_t);
  const std::string_view order = ome.pixels.dimension_order;

  // Full-space linear plane index -> absolute IFD, for self-owned planes only.
  std::map<uint32_t, uint32_t> linear_to_ifd;
  for (const auto& td : ome.tiff_data) {
    if (!is_self(td)) {
      continue;
    }
    const uint32_t base =
        LinearPlaneIndex(order, fc, fz, ft, td.first_c, td.first_z, td.first_t);
    const uint32_t count = std::max(1u, td.plane_count);
    for (uint32_t k = 0; k < count; ++k) {
      linear_to_ifd[base + k] = td.ifd + k;
    }
  }
  if (linear_to_ifd.empty()) {
    return ome;
  }

  // Collect the (c, z, t) triples that resolve to this file.
  std::set<uint32_t> cs, zs, ts;
  std::vector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> present;
  for (uint32_t c = 0; c < fc; ++c) {
    for (uint32_t z = 0; z < fz; ++z) {
      for (uint32_t t = 0; t < ft; ++t) {
        const uint32_t lin = LinearPlaneIndex(order, fc, fz, ft, c, z, t);
        const auto it = linear_to_ifd.find(lin);
        if (it == linear_to_ifd.end()) {
          continue;
        }
        cs.insert(c);
        zs.insert(z);
        ts.insert(t);
        present.emplace_back(c, z, t, it->second);
      }
    }
  }

  const uint32_t nc = static_cast<uint32_t>(cs.size());
  const uint32_t nz = static_cast<uint32_t>(zs.size());
  const uint32_t nt = static_cast<uint32_t>(ts.size());
  if (present.size() != static_cast<size_t>(nc) * nz * nt) {
    return ome;  // Sparse / irregular layout: leave dimensions untouched.
  }

  const auto compact = [](const std::set<uint32_t>& values) {
    std::map<uint32_t, uint32_t> remap;
    uint32_t i = 0;
    for (uint32_t v : values) {
      remap[v] = i++;
    }
    return remap;
  };
  const auto cmap = compact(cs);
  const auto zmap = compact(zs);
  const auto tmap = compact(ts);

  OmeMetadata out = ome;
  out.pixels.size_c = nc;
  out.pixels.size_z = nz;
  out.pixels.size_t = nt;
  out.tiff_data.clear();
  out.tiff_data.reserve(present.size());
  for (const auto& [c, z, t, ifd] : present) {
    OmeTiffData d;
    d.first_c = cmap.at(c);
    d.first_z = zmap.at(z);
    d.first_t = tmap.at(t);
    d.ifd = ifd;
    d.plane_count = 1;
    out.tiff_data.push_back(d);
  }
  // Keep only the channels that survive, in compact order.
  out.channels.clear();
  out.channels.reserve(nc);
  for (uint32_t orig : cs) {
    OmeChannel ch;
    if (orig < ome.channels.size()) {
      ch = ome.channels[orig];
    }
    ch.index = static_cast<uint32_t>(out.channels.size());
    out.channels.push_back(std::move(ch));
  }
  return out;
}

void SetSinglePlaneFields(OmeTiffLevelInfo& level, uint32_t channel_count) {
  level.channel_count = channel_count;
  level.z_count = 1;
  level.t_count = 1;
  level.plane_pages = level.pages;
}

/// @brief Build channel metadata from OME `<Channel>` entries (index fallback).
void BuildChannelList(const OmeMetadata& ome, uint32_t channel_count,
                      std::vector<OmeTiffChannelInfo>& channels) {
  channels.reserve(channel_count);
  for (uint32_t c = 0; c < channel_count; ++c) {
    OmeTiffChannelInfo ch;
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

/// @brief Map each linear plane index to an absolute TIFF page (IFD).
///
/// Uses `<TiffData>` when present (treating the `IFD` attribute as the absolute
/// page index and advancing `PlaneCount` planes in `DimensionOrder`); otherwise
/// falls back to the root pages in file order.
aifocore::Status BuildPlaneToPage(const OmeMetadata& ome, uint32_t nc,
                                  uint32_t nz, uint32_t nt,
                                  const std::vector<uint32_t>& roots,
                                  std::vector<uint32_t>& plane_to_page) {
  const uint32_t num_planes = nc * nz * nt;
  plane_to_page.assign(num_planes, UINT32_MAX);

  if (!ome.tiff_data.empty()) {
    for (const auto& td : ome.tiff_data) {
      const uint32_t base =
          LinearPlaneIndex(ome.pixels.dimension_order, nc, nz, nt, td.first_c,
                           td.first_z, td.first_t);
      const uint32_t count = std::max(1u, td.plane_count);
      for (uint32_t k = 0; k < count; ++k) {
        const uint32_t linear = base + k;
        if (linear < num_planes) {
          plane_to_page[linear] = td.ifd + k;
        }
      }
    }
  } else {
    if (roots.size() < num_planes) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format(
              "OME-TIFF: expected {} planes (C*Z*T) but only {} root IFDs",
              num_planes, roots.size()));
    }
    for (uint32_t i = 0; i < num_planes; ++i) {
      plane_to_page[i] = roots[i];
    }
  }

  for (uint32_t i = 0; i < num_planes; ++i) {
    if (plane_to_page[i] == UINT32_MAX) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          aifocore::fmt::format("OME-TIFF: plane {} has no IFD mapping", i));
    }
  }
  return aifocore::Status::OkStatus();
}

/// @brief Build a single level-0 plane table for a multi-dimensional stack.
///
/// Pyramid sub-resolutions are not mapped in the multi-plane case (uncommon);
/// the stack is exposed at full resolution only.
aifocore::Status LoadMultiPlane(const simpletiff::TiffIndex& tiff_index,
                                const OmeMetadata& ome, uint32_t nc,
                                uint32_t nz, uint32_t nt,
                                const std::vector<uint32_t>& roots,
                                std::vector<OmeTiffChannelInfo>& channels,
                                std::vector<OmeTiffLevelInfo>& pyramid) {
  std::vector<uint32_t> plane_to_page;
  AIFOCORE_RETURN_IF_ERROR(
      BuildPlaneToPage(ome, nc, nz, nt, roots, plane_to_page));

  BuildChannelList(ome, nc, channels);
  for (uint32_t c = 0; c < nc; ++c) {
    channels[c].page = plane_to_page[LinearPlaneIndex(
        ome.pixels.dimension_order, nc, nz, nt, c, 0, 0)];
  }

  OmeTiffLevelInfo level0;
  level0.z_count = nz;
  level0.t_count = nt;
  level0.channel_count = nc;
  level0.plane_pages.resize(static_cast<size_t>(nz) * nt * nc);
  for (uint32_t z = 0; z < nz; ++z) {
    for (uint32_t t = 0; t < nt; ++t) {
      for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t linear =
            LinearPlaneIndex(ome.pixels.dimension_order, nc, nz, nt, c, z, t);
        const size_t dst = (static_cast<size_t>(z) * nt + t) * nc + c;
        level0.plane_pages[dst] = plane_to_page[linear];
      }
    }
  }
  // Default plane (z=0, t=0) seeds `pages` so the plan builder has a valid
  // channel list before PrepareRequest selects a specific plane.
  const auto first_plane = level0.PagesForPlane(0, 0);
  level0.pages.assign(first_plane.begin(), first_plane.end());
  level0.size = {ome.pixels.size_x, ome.pixels.size_y};
  const auto& page0 = tiff_index.Page(level0.pages[0]);
  level0.tiled = (page0.storage == simpletiff::Storage::kTiles);
  level0.allow_random_access = level0.tiled;
  pyramid.push_back(std::move(level0));
  return aifocore::Status::OkStatus();
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

  AIFOCORE_ASSIGN_OR_RETURN(const auto ome_parsed,
                            formats::ometiff::OmeMetadataParser::Parse(xml));

  // Companion/multi-file datasets describe the whole acquisition but only ship
  // a subset of planes per file; scope the metadata to what this file holds.
  const OmeMetadata ome = RestrictToSelfFile(ome_parsed);

  if (ome.pixels.size_c == 0 || ome.pixels.size_x == 0 ||
      ome.pixels.size_y == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-TIFF: invalid Pixels dimensions");
  }

  metadata.mpp_x = ome.pixels.physical_size_x.value_or(0.0);
  metadata.mpp_y = ome.pixels.physical_size_y.value_or(0.0);
  metadata.z_count = std::max(1u, ome.pixels.size_z);
  metadata.t_count = std::max(1u, ome.pixels.size_t);
  metadata.z_spacing_um = ome.pixels.physical_size_z;
  metadata.t_interval_s = ome.pixels.time_increment;

  // Root pages represent full-resolution planes.
  const std::vector<uint32_t> roots = RootPagesInOrder(tiff_index);
  if (roots.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-TIFF: no root IFDs found");
  }

  // Z/T stacks take a dedicated, plane-aware path; plain 2D OME-TIFFs keep the
  // existing (pyramid-capable) single-plane logic below.
  const uint32_t nz = std::max(1u, ome.pixels.size_z);
  const uint32_t nt = std::max(1u, ome.pixels.size_t);
  if (nz > 1 || nt > 1) {
    return LoadMultiPlane(tiff_index, ome, ome.pixels.size_c, nz, nt, roots,
                          channels, pyramid);
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
  SetSinglePlaneFields(level0, effective_channels);
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
      SetSinglePlaneFields(lvl, effective_channels);
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
      SetSinglePlaneFields(lvl, effective_channels);
      pyramid.push_back(std::move(lvl));
    }
    return aifocore::Status::OkStatus();
  }

  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kInvalidArgument,
      "OME-TIFF: unable to map pyramid levels to SubIFDs (unsupported layout)");
}

}  // namespace fastslide
