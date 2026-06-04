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

#include "fastslide/readers/olympusvsi/olympusvsi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/core/metadata.h"
#include "fastslide/readers/olympusvsi/olympusvsi_exec_context.h"
#include "fastslide/readers/olympusvsi/olympusvsi_plan_builder.h"
#include "fastslide/readers/olympusvsi/olympusvsi_tile_executor.h"
#include "fastslide/readers/simpletiff_decode_utils.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fs = std::filesystem;

namespace fastslide::formats::olympusvsi {

namespace {

/// @brief Olympus VS scanners number stack folders so that 10000+ are the
/// high-resolution scans (one per channel/region in fluorescence), and
/// lower numbers are navigator/preview pyramids.
constexpr int kMainStackMin = 10000;

std::optional<int> StackNumber(std::string_view name) {
  std::string digits;
  digits.reserve(name.size());
  for (char c : name) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      digits.push_back(c);
    }
  }
  if (digits.empty()) {
    return std::nullopt;
  }
  try {
    return std::stoi(digits);
  } catch (...) {
    return std::nullopt;
  }
}

std::string LowerExt(const fs::path& p) {
  std::string ext = p.extension().string();
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext;
}

/// @brief True for ETS filenames that hold a tiled image pyramid.
///
/// Olympus stores pixel pyramids as ``frame_*.ets`` (``frame_t.ets``, plus
/// ``frame_t_<index>.ets`` for time-lapse / multi-position acquisitions).
/// Non-image payloads (vector layers, masks, serialized blobs) live in the
/// same ``stack*`` folders as ``blob_*.ets``; those legitimately declare a
/// non-JPEG/JP2 compression (e.g. RAW) and are not tile pyramids, so they
/// must not be treated as stacks. The classification is by the ``frame_``
/// filename prefix, matching the on-disk Olympus naming convention.
bool IsPixelEtsName(const fs::path& file) {
  std::string name = file.filename().string();
  for (char& c : name) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return name.rfind("frame_", 0) == 0;
}

/// @brief Locate every pixel-pyramid `.ets` file under the slide's data
///        directory.
///
/// Olympus VS scanners do not commit to a single filename inside each
/// stack: brightfield mosaics typically write a single ``frame_t.ets``,
/// but time-lapse / multi-position acquisitions emit one or more
/// ``frame_t_<index>.ets`` files (the ``_<index>`` suffix is a
/// time-point / position index). Multi-channel fluorescence VSIs add
/// per-channel sub-stacks with the same filename. We enumerate every
/// ``frame_*.ets`` file in every ``stack*`` subdirectory and let the
/// caller decide which one(s) to expose as `SlideImage`s; ``blob_*.ets``
/// non-image payloads are skipped (see @ref IsPixelEtsName).
std::vector<fs::path> DiscoverStacks(const fs::path& path) {
  const std::string ext = LowerExt(path);
  if (ext == ".ets") {
    return {path};
  }

  fs::path data_dir;
  if (ext == ".vsi") {
    data_dir = path.parent_path() / ("_" + path.stem().string() + "_");
  } else if (fs::is_directory(path)) {
    data_dir = path;
  } else {
    return {};
  }

  std::vector<fs::path> stacks;
  std::error_code ec;
  if (!fs::is_directory(data_dir, ec) || ec) {
    return {};
  }
  for (const auto& entry : fs::directory_iterator(data_dir, ec)) {
    if (ec)
      break;
    if (!entry.is_directory()) {
      continue;
    }
    const std::string folder = entry.path().filename().string();
    if (folder.rfind("stack", 0) != 0) {
      continue;
    }
    std::error_code dec;
    for (const auto& file : fs::directory_iterator(entry.path(), dec)) {
      if (dec)
        break;
      if (!file.is_regular_file()) {
        continue;
      }
      if (LowerExt(file.path()) == ".ets" && IsPixelEtsName(file.path())) {
        stacks.push_back(file.path());
      }
    }
  }
  // Sort lexicographically so that `stack1` (navigator) comes before
  // `stack10000` (main scan), and within a stack `frame_t.ets` sorts
  // before `frame_t_0.ets`. Tests and downstream code that select by
  // index see a stable order.
  std::sort(stacks.begin(), stacks.end());
  return stacks;
}

uint32_t CeilHalf(uint32_t value) {
  return static_cast<uint32_t>(std::ceil(static_cast<double>(value) / 2.0));
}

/// @brief Build per-level pyramid info by grouping tiles in the ETS table.
///
/// Tile reads use the on-disk grid (`size`). Reported dimensions and
/// downsamples: successive ceil(width/2) / ceil(height/2) from level 0
/// and downsample = 2^level. This seems to keep the data stable across
/// different levels.
aifocore::Result<std::vector<OlympusVsiLevelInfo>> BuildPyramid(
    const EtsFileData& data) {
  std::vector<OlympusVsiLevelInfo> levels;
  if (data.tiles.empty())
    return levels;

  uint32_t max_level = data.tiles.front().level;
  for (const auto& t : data.tiles) {
    max_level = std::max(max_level, t.level);
  }
  levels.resize(static_cast<size_t>(max_level) + 1);

  const uint32_t tile_w = data.ets.tile_w;
  const uint32_t tile_h = data.ets.tile_h;

  for (size_t i = 0; i < levels.size(); ++i) {
    levels[i].level = static_cast<int>(i);
    levels[i].tile_w = tile_w;
    levels[i].tile_h = tile_h;
  }

  // Channel model. 16-bit Olympus VSI fluorescence stacks store several
  // grayscale planes that share one (x, y, level) grid; the plane index
  // lives in the "middle" tile-record dimensions (the slots between
  // (x, y) and the pyramid level — i.e. the ``channel`` field plus any
  // extra Z/T slot summed into ``extra_dim_sum``). QuPath surfaces these
  // as N channels of one image, so we do the same: enumerate the
  // distinct middle-dim tuples, sort them, and assign each a stable
  // ascending channel index.
  //
  // 8-bit brightfield stacks keep the classic single-plane RGB model
  // (channel 0, principal Z/T only); any non-principal slices are
  // skipped and reported in the diagnostic log below.
  const bool multi_plane = data.ets.pixel_type == TilePixelType::kUInt16;
  const auto plane_key_of = [](const TileRecord& rec) -> uint64_t {
    return (static_cast<uint64_t>(rec.channel) << 32) |
           static_cast<uint64_t>(rec.extra_dim_sum);
  };

  ankerl::unordered_dense::map<uint64_t, uint32_t> plane_to_channel;
  if (multi_plane) {
    std::vector<uint64_t> plane_keys;
    for (const auto& rec : data.tiles) {
      const uint64_t pk = plane_key_of(rec);
      if (plane_to_channel.emplace(pk, 0U).second) {
        plane_keys.push_back(pk);
      }
    }
    std::sort(plane_keys.begin(), plane_keys.end());
    for (uint32_t i = 0; i < plane_keys.size(); ++i) {
      plane_to_channel[plane_keys[i]] = i;
    }
  }
  const uint32_t n_channels =
      multi_plane ? static_cast<uint32_t>(plane_to_channel.size()) : 1U;

  uint64_t skipped_nonprincipal = 0;
  for (const auto& rec : data.tiles) {
    uint32_t channel_index = 0;
    if (multi_plane) {
      channel_index = plane_to_channel[plane_key_of(rec)];
    } else if (rec.channel != 0 || rec.extra_dim_sum != 0) {
      // Brightfield: only the principal slice is materialised. Count the
      // skipped non-principal tiles (additional channel-field planes or
      // non-zero Z/T focal planes) so the user can see what was dropped.
      ++skipped_nonprincipal;
      continue;
    }
    auto& lvl = levels[static_cast<size_t>(rec.level)];
    lvl.tile_map[OlympusVsiLevelInfo::PackKey3(channel_index, rec.x, rec.y)] =
        LevelTileEntry{rec.offset, rec.n_bytes};
    lvl.grid_cols = std::max(lvl.grid_cols, rec.x + 1);
    lvl.grid_rows = std::max(lvl.grid_rows, rec.y + 1);
  }

  for (auto& lvl : levels) {
    lvl.n_channels = n_channels;
  }

  // Reject coordinate tables that are not a dense pixel grid. A real
  // pyramid level fills (almost) every grid cell, so the cell count is at
  // most a small multiple of the materialised tile count. Some Olympus
  // ``blob_*.ets`` payloads (vector layers, masks, serialized data) reuse
  // the tile-record slots for flag-encoded indices (e.g. coordinates with
  // the ``0x40000000`` bit set), which would otherwise inflate the grid to
  // billions of cells and a meaningless, memory-exploding "image".
  for (const auto& lvl : levels) {
    if (lvl.tile_map.empty()) {
      continue;
    }
    const uint64_t cells = static_cast<uint64_t>(lvl.grid_cols) * lvl.grid_rows;
    if (cells > lvl.tile_map.size() * 4ULL + 16ULL) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format(
              "Olympus VSI: tile coordinates do not form a dense pixel grid "
              "({}x{} cells for {} tiles); this looks like a serialized "
              "blob_*.ets payload, not an image",
              lvl.grid_cols, lvl.grid_rows, lvl.tile_map.size()));
    }
  }

  if (skipped_nonprincipal > 0) {
    std::cerr << "[OlympusVSI] " << data.path.string() << ": skipped "
              << skipped_nonprincipal
              << " non-principal tile(s) (channel != 0 or Z/T != 0); only the "
                 "principal slice is materialised for this 8-bit stack.\n";
  } else if (multi_plane && n_channels > 1) {
    // Nothing to do here
  }

  // Drop trailing empty levels (rare, but possible if the file declares a
  // high `level` int that has no channel-0 tiles).
  while (!levels.empty() && levels.back().tile_map.empty()) {
    levels.pop_back();
  }
  if (levels.empty())
    return levels;

  uint32_t reported_w = levels[0].grid_cols * tile_w;
  uint32_t reported_h = levels[0].grid_rows * tile_h;
  for (size_t i = 0; i < levels.size(); ++i) {
    auto& lvl = levels[i];
    lvl.size = {lvl.grid_cols * tile_w, lvl.grid_rows * tile_h};
    if (i > 0) {
      reported_w = CeilHalf(reported_w);
      reported_h = CeilHalf(reported_h);
    }
    lvl.reported_size = {reported_w, reported_h};
    lvl.downsample = std::pow(2.0, static_cast<double>(i));
  }
  return levels;
}

/// @brief Name a discovered stack by its folder + ETS file stem.
///
/// Folder-based base names:
///   `stack1`, `stack2`, ... -> `"navigator"`/`"navigator 1"`/...
///   `stack1000n`            -> `"region 0"`, `"region 1"`, ...
///   anything else           -> the bare folder name.
///
/// Some VSI files (multi-position / time-lapse acquisitions) emit
/// multiple ETS files in a single stack directory named
/// ``frame_t_<index>.ets``. We disambiguate by appending the file stem
/// when it deviates from the canonical ``frame_t``, e.g.
/// ``"region 1 (frame_t_0)"``.
std::string ImageNameFromStack(std::string_view folder,
                               std::string_view file_stem, int navigator_idx,
                               int region_idx) {
  std::string base;
  if (const auto number = StackNumber(folder); number.has_value()) {
    if (*number >= kMainStackMin) {
      base = aifocore::fmt::format("region {}", region_idx);
    } else {
      base = navigator_idx == 0
                 ? std::string("navigator")
                 : aifocore::fmt::format("navigator {}", navigator_idx);
    }
  } else {
    base = std::string(folder);
  }
  if (file_stem.empty() || file_stem == "frame_t") {
    return base;
  }
  return aifocore::fmt::format("{} ({})", base, file_stem);
}

/// @brief Translate `.vsi` container channels into `ChannelMetadata`,
///        falling back to a generic name / white colour per channel.
std::vector<ChannelMetadata> ChannelMetadataFromPyramid(
    const VsiPyramidMeta& pyramid) {
  std::vector<ChannelMetadata> out;
  out.reserve(pyramid.channels.size());
  for (size_t i = 0; i < pyramid.channels.size(); ++i) {
    const auto& ch = pyramid.channels[i];
    ChannelMetadata cm;
    cm.name =
        ch.name.empty() ? aifocore::fmt::format("Channel {}", i) : ch.name;
    cm.biomarker = "Intensity";
    cm.color = ch.color.value_or(ColorRGB{255, 255, 255});
    out.push_back(std::move(cm));
  }
  return out;
}

/// @brief Pick the primary image. Largest main-stack (>= kMainStackMin) by
/// tile count, falling back to the largest stack overall.
int PickPrimary(
    const std::vector<std::unique_ptr<OlympusVsiStackImage>>& images) {
  if (images.empty())
    return 0;

  int best_main = -1;
  uint64_t best_main_score = 0;
  int best_any = 0;
  uint64_t best_any_score = 0;
  for (int i = 0; i < static_cast<int>(images.size()); ++i) {
    const auto& img = *images[i];
    const auto folder = img.GetEtsPath().parent_path().filename().string();
    const auto num = StackNumber(folder);
    std::error_code ec;
    const uint64_t score = fs::file_size(img.GetEtsPath(), ec);
    if (ec) {
      continue;
    }
    if (num.has_value() && *num >= kMainStackMin) {
      if (best_main < 0 || score > best_main_score) {
        best_main = i;
        best_main_score = score;
      }
    }
    if (i == 0 || score > best_any_score) {
      best_any = i;
      best_any_score = score;
    }
  }
  return best_main >= 0 ? best_main : best_any;
}

}  // namespace

// =====================================================================
// OlympusVsiStackImage
// =====================================================================

OlympusVsiStackImage::OlympusVsiStackImage(
    const OlympusVsiReader& cache_provider, std::string name, EtsFileData ets,
    std::vector<OlympusVsiLevelInfo> pyramid)
    : cache_provider_(cache_provider),
      name_(std::move(name)),
      ets_path_str_(ets.path.string()),
      ets_(std::move(ets)),
      pyramid_(std::move(pyramid)) {
  if (!pyramid_.empty()) {
    properties_.bounds =
        SlideBounds(0, 0, pyramid_[0].size[0], pyramid_[0].size[1]);
  }
  properties_.scanner_model = "Olympus VS";
}

int OlympusVsiStackImage::GetLevelCount() const {
  return static_cast<int>(pyramid_.size());
}

aifocore::Result<LevelInfo> OlympusVsiStackImage::GetLevelInfo(
    int level) const {
  if (level < 0 || static_cast<size_t>(level) >= pyramid_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Olympus VSI: level {} not found in image '{}'",
                              level, name_));
  }
  const auto& l = pyramid_[level];
  LevelInfo info;
  info.dimensions = l.reported_size;
  info.downsample_factor = l.downsample;
  return info;
}

void OlympusVsiStackImage::SetReportedDimensions(uint32_t width,
                                                 uint32_t height) {
  if (pyramid_.empty() || width == 0U || height == 0U) {
    return;
  }
  // Level 0 reports the true boundary; each coarser level halves it
  // (round-up), matching the pow(2, level) downsample assigned in
  // BuildPyramid. The tile-read grid (`size`) is left untouched.
  uint32_t reported_w = width;
  uint32_t reported_h = height;
  for (size_t i = 0; i < pyramid_.size(); ++i) {
    if (i > 0) {
      reported_w = CeilHalf(reported_w);
      reported_h = CeilHalf(reported_h);
    }
    pyramid_[i].reported_size = {reported_w, reported_h};
  }
  properties_.bounds = SlideBounds(0, 0, pyramid_[0].reported_size[0],
                                   pyramid_[0].reported_size[1]);
}

uint32_t OlympusVsiStackImage::GetNChannels() const {
  if (ets_.ets.pixel_type == TilePixelType::kUInt16) {
    return PlaneCount();
  }
  return ets_.ets.n_channels;
}

std::vector<ChannelMetadata> OlympusVsiStackImage::GetChannelMetadata() const {
  std::vector<ChannelMetadata> md;
  // Brightfield RGB keeps the historical single "RGB / Color" entry so
  // existing tests/tooling do not regress.
  if (ets_.ets.pixel_type != TilePixelType::kUInt16 &&
      ets_.ets.n_channels == 3U) {
    md.emplace_back("RGB", "Color", ColorRGB{255, 255, 255});
    return md;
  }
  const uint32_t channels = GetNChannels();
  // Real fluorophore names + display colours from the `.vsi` container tag
  // tree, when the reader resolved a matching pyramid. The on-disk `.ets`
  // header carries no channel naming, so this is the only source that
  // matches what QuPath shows ("FL DAPI", "FL FITC", "FL CY3").
  if (!channel_override_.empty() &&
      channel_override_.size() == static_cast<size_t>(channels)) {
    return channel_override_;
  }
  if (channels == 1U) {
    md.emplace_back("Gray", "Intensity", ColorRGB{255, 255, 255});
    return md;
  }
  // Fallback when no `.vsi` metadata was available (e.g. `.ets`-only input).
  for (uint32_t i = 0; i < channels; ++i) {
    md.emplace_back(aifocore::fmt::format("Channel {}", i), "Intensity",
                    ColorRGB{255, 255, 255});
  }
  return md;
}

ImageFormat OlympusVsiStackImage::GetImageFormat() const {
  // 16-bit fluorescence: stacked grayscale planes are independent
  // fluorophores, so 1 plane → kGray and >1 → kSpectral (never kRGB,
  // even at 3 planes, since they are not colour components).
  if (ets_.ets.pixel_type == TilePixelType::kUInt16) {
    return PlaneCount() == 1U ? ImageFormat::kGray : ImageFormat::kSpectral;
  }
  switch (ets_.ets.n_channels) {
    case 1U:
      return ImageFormat::kGray;
    case 3U:
      return ImageFormat::kRGB;
    case 4U:
      return ImageFormat::kRGBA;
    default:
      return ImageFormat::kSpectral;
  }
}

DataType OlympusVsiStackImage::GetDataType() const {
  return ets_.ets.pixel_type == TilePixelType::kUInt16 ? DataType::kUInt16
                                                       : DataType::kUInt8;
}

ImageDimensions OlympusVsiStackImage::GetTileSize() const {
  if (pyramid_.empty()) {
    return ImageDimensions{static_cast<uint32_t>(ets_.ets.tile_w),
                           static_cast<uint32_t>(ets_.ets.tile_h)};
  }
  return ImageDimensions{pyramid_[0].tile_w, pyramid_[0].tile_h};
}

aifocore::Result<core::TilePlan> OlympusVsiStackImage::PrepareRequest(
    const core::TileRequest& request) const {
  return OlympusVsiPlanBuilder::BuildPlan(request, pyramid_, ets_.ets);
}

aifocore::Status OlympusVsiStackImage::ExecutePlan(
    const core::TilePlan& plan, runtime::Canvas& canvas) const {
  const OlympusVsiExecContext context{
      .ets_path = ets_path_str_,
      .pyramid = pyramid_,
      .declared_codec = ets_.ets.compression,
      .pixel_type = ets_.ets.pixel_type,
      .n_channels = GetNChannels(),
      .cache = cache_provider_.GetCache(),
  };
  return OlympusVsiTileExecutor::ExecutePlan(plan, context, canvas);
}

// =====================================================================
// OlympusVsiReader
// =====================================================================

aifocore::Result<std::unique_ptr<OlympusVsiReader>> OlympusVsiReader::Create(
    std::string_view path) {
  std::unique_ptr<OlympusVsiReader> reader(
      new OlympusVsiReader(std::string(path)));
  AIFOCORE_RETURN_IF_ERROR(reader->Load());
  return reader;
}

OlympusVsiReader::OlympusVsiReader(std::string path)
    : input_path_(std::move(path)) {}

aifocore::Status OlympusVsiReader::Load() {
  AIFOCORE_RETURN_IF_ERROR(DiscoverAndBuildImages());

  properties_ = SlideProperties{};
  if (!images_.empty()) {
    properties_ = Primary().GetProperties();
  }
  // Prefer the real device name from the `.vsi` document properties; fall
  // back to the generic family name for `.ets`-only inputs or when absent.
  properties_.scanner_model =
      vsi_device_name_.empty() ? "Olympus VS" : vsi_device_name_;
  PopulateAssociatedFromVsi();
  return aifocore::Status::OkStatus();
}

aifocore::Status OlympusVsiReader::DiscoverAndBuildImages() {
  fs::path input(input_path_);
  std::error_code ec;
  if (!fs::exists(input, ec) || ec) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Olympus VSI: path does not exist: {}",
                              input_path_));
  }
  const auto stacks = DiscoverStacks(input);
  if (stacks.empty()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Olympus VSI: no stack*/*.ets files found");
  }

  // Channel names / display colours live in the `.vsi` container, not in
  // the `.ets` pixel files. Parse them up front (best-effort) so each
  // fluorescence stack image can expose real fluorophore names. `.ets`-only
  // inputs simply have no container and keep the generic fallback names.
  if (LowerExt(input) == ".vsi") {
    VsiContainerMeta vsi_meta = ParseVsiMetadata(input);
    vsi_device_name_ = std::move(vsi_meta.device_name);
    vsi_pyramids_ = std::move(vsi_meta.pyramids);
  }

  // Per-stack-folder counters: a single folder may contribute multiple
  // ETS files (e.g. ``frame_t.ets`` + ``frame_t_0.ets`` for time-lapse
  // acquisitions). All ETS files from the same folder share the
  // navigator/region index, and are disambiguated by file stem.
  //
  // Individual stacks that we cannot parse (unsupported pixel type,
  // codec, etc.) are *skipped*: the slide stays openable but every
  // failure is recorded in ``skipped_stacks_`` with the full
  // ``Status::ToString()`` so callers can explain "why didn't this
  // image show up?" without forcing the slide open to fail. Olympus
  // VSI files in the wild mix supported brightfield-RGB stacks with
  // auxiliary 16-bit / multi-channel fluorescence stacks that this
  // reader does not yet materialise; rejecting the whole slide because
  // of one unsupported sub-stack is strictly worse than just exposing
  // the ones we *can* read.
  int navigator_idx = 0;
  int region_idx = 0;
  std::string last_folder;
  // Tracks which `.vsi` container pyramids have been claimed by a stack so
  // equal-sized regions pair up in document order rather than colliding.
  std::vector<bool> vsi_claimed(vsi_pyramids_.size(), false);
  const auto skip_stack = [this](const fs::path& path,
                                 const aifocore::Status& status) {
    skipped_stacks_.push_back(
        {/*path=*/path.string(), /*reason=*/status.ToString()});
  };
  for (const auto& stack_path : stacks) {
    auto ets_result = ParseEtsFile(stack_path);
    if (!ets_result.ok()) {
      skip_stack(stack_path, ets_result.status());
      continue;
    }
    EtsFileData ets = std::move(*ets_result);
    if (ets.ets.compression != TileCodec::kRaw &&
        ets.ets.compression != TileCodec::kJpeg &&
        ets.ets.compression != TileCodec::kJp2) {
      skip_stack(stack_path,
                 AIFOCORE_MAKE_STATUS(
                     aifocore::StatusCode::kUnimplemented,
                     aifocore::fmt::format(
                         "Olympus VSI: unsupported tile codec '{}' for "
                         "brightfield reader",
                         CodecName(ets.ets.compression))));
      continue;
    }
    auto pyramid_result = BuildPyramid(ets);
    if (!pyramid_result.ok()) {
      skip_stack(stack_path, pyramid_result.status());
      continue;
    }
    auto pyramid = std::move(*pyramid_result);
    if (pyramid.empty()) {
      // Degenerate stack (no channel-0 tiles after filtering) — not a
      // hard error but still recorded so callers can see what happened.
      skip_stack(stack_path,
                 AIFOCORE_MAKE_STATUS(
                     aifocore::StatusCode::kFailedPrecondition,
                     "Olympus VSI: no usable pyramid levels (no channel-0 "
                     "tiles)"));
      continue;
    }
    const auto folder = stack_path.parent_path().filename().string();
    const auto stem = stack_path.stem().string();
    const auto num = StackNumber(folder);
    const bool first_in_folder = folder != last_folder;

    int folder_navigator_idx = navigator_idx;
    int folder_region_idx = region_idx;
    if (!first_in_folder) {
      // Subsequent ETS in the same folder reuse the prior counter so
      // that all frames of e.g. ``stack10002`` share ``region N``.
      folder_navigator_idx = std::max(0, navigator_idx - 1);
      folder_region_idx = std::max(0, region_idx - 1);
    }
    std::string image_name = ImageNameFromStack(
        folder, stem, folder_navigator_idx, folder_region_idx);

    if (first_in_folder) {
      if (num.has_value() && *num >= kMainStackMin) {
        ++region_idx;
      } else if (num.has_value()) {
        ++navigator_idx;
      }
      last_folder = folder;
    }

    // Match this stack to its `.vsi` container pyramid by image size to
    // recover the real series name ("10x_01", "Overview", ...) and the
    // real per-channel names/colours, both of which live only in the
    // container. Falls back to the generated name when unmatched (e.g.
    // `.ets`-only input or an orphan stack).
    const uint32_t plane_count =
        pyramid.empty() ? 1U : pyramid.front().n_channels;
    const VsiPyramidMeta* matched = nullptr;
    if (!pyramid.empty()) {
      const auto& l0 = pyramid.front();
      const int idx = MatchVsiPyramid(l0.size[0], l0.size[1], l0.tile_w,
                                      l0.tile_h, vsi_claimed);
      if (idx >= 0) {
        matched = &vsi_pyramids_[static_cast<size_t>(idx)];
        if (!matched->name.empty()) {
          image_name = matched->name;
        }
      }
    }

    auto image = std::make_unique<OlympusVsiStackImage>(
        *this, std::move(image_name), std::move(ets), std::move(pyramid));
    // Real scanner / acquisition-software model from the container's
    // document properties (no-op when the field is absent).
    image->SetScannerModel(vsi_device_name_);
    if (matched != nullptr) {
      // Per-image pixel size from the container's micron-scale tag (2019).
      // Each frame (navigator/overview/region) reports its own value.
      if (matched->mpp_x > 0.0 && matched->mpp_y > 0.0) {
        image->SetMpp(matched->mpp_x, matched->mpp_y);
      }
      // Per-image true boundary from the container's boundary-rect tag (2053),
      // trimming the tile-grid over-report individually for this frame.
      if (matched->width > 0U && matched->height > 0U) {
        image->SetReportedDimensions(matched->width, matched->height);
      }
      if (plane_count > 1U &&
          matched->channels.size() == static_cast<size_t>(plane_count)) {
        image->SetChannelMetadata(ChannelMetadataFromPyramid(*matched));
      }
    }
    images_.push_back(std::move(image));
  }
  if (images_.empty()) {
    // Bubble up the LAST skipped reason if any (the parse-error
    // detail is more actionable than a generic "no images").
    if (!skipped_stacks_.empty()) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kUnimplemented,
          aifocore::fmt::format(
              "Olympus VSI: no usable stacks among {} discovered ETS files. "
              "Last error: {}",
              stacks.size(), skipped_stacks_.back().reason));
    }
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        "Olympus VSI: no usable pyramid levels in any stack");
  }
  EmitSkippedStackWarnings();
  primary_index_ = PickPrimary(images_);
  return aifocore::Status::OkStatus();
}

int OlympusVsiReader::MatchVsiPyramid(uint32_t grid_w, uint32_t grid_h,
                                      uint32_t tile_w, uint32_t tile_h,
                                      std::vector<bool>& claimed) const {
  if (tile_w == 0U || tile_h == 0U) {
    return -1;
  }
  // The stack's level-0 grid is always a whole number of tiles wide/high.
  // A container pyramid belongs to this stack when its true (sub-tile)
  // boundary covers that same number of tiles per axis -- i.e. dividing the
  // boundary by the tile size and rounding up reproduces the grid's tile
  // count. The first not-yet-claimed pyramid that matches both axes wins;
  // claiming it keeps equal-sized regions paired in document order.
  const auto tiles_to_cover = [](uint32_t extent, uint32_t tile) -> uint32_t {
    return (extent + tile - 1U) / tile;
  };
  const uint32_t target_cols = grid_w / tile_w;
  const uint32_t target_rows = grid_h / tile_h;
  for (size_t i = 0; i < vsi_pyramids_.size(); ++i) {
    if (claimed[i]) {
      continue;
    }
    const auto& pyr = vsi_pyramids_[i];
    if (pyr.width == 0U || pyr.height == 0U) {
      continue;
    }
    if (tiles_to_cover(pyr.width, tile_w) == target_cols &&
        tiles_to_cover(pyr.height, tile_h) == target_rows) {
      claimed[i] = true;
      return static_cast<int>(i);
    }
  }
  return -1;
}

void OlympusVsiReader::EmitSkippedStackWarnings() const {
  // Emit one summary line plus one per-stack line to stderr. Callers
  // that prefer a structured surface can also read
  // ``GetSkippedStacks()`` or the ``olympus_vsi.skipped[*]`` metadata
  // keys; the stderr stream just makes the failure visible by default
  // (no need to opt into verbose mode to discover that some images
  // were silently dropped). Stays quiet when no stacks were skipped.
  if (skipped_stacks_.empty()) {
    return;
  }
  std::cerr << "[OlympusVSI] Skipped " << skipped_stacks_.size() << " of "
            << (skipped_stacks_.size() + images_.size())
            << " stacks while opening '" << input_path_ << "':\n";
  for (const auto& sk : skipped_stacks_) {
    std::cerr << "  - " << sk.path << ": " << sk.reason << '\n';
  }
}

void OlympusVsiReader::PopulateAssociatedFromVsi() {
  // The .vsi container is a TIFF whose subsequent pages are macro/label
  // associated images. .ets-only inputs simply have no container TIFF.
  //
  // MPP is deliberately *not* taken from the container TIFF resolution: the
  // `.vsi` page-0 resolution is a meaningless placeholder (e.g. 72 / 96 dpi
  // screen defaults), not the scan scale. The authoritative per-image pixel
  // size comes solely from each frame's micron-scale tag (2019, applied per
  // image in DiscoverAndBuildImages and seeding `properties_` via the
  // primary). When that tag is absent the MPP is left unset rather than
  // fabricated from the placeholder resolution.
  if (LowerExt(fs::path(input_path_)) != ".vsi")
    return;

  vsi_tiff_ = std::make_unique<simpletiff::TiffIndex>();
  int fd = -1;
  if (!simpletiff::OpenTiff(input_path_, *vsi_tiff_, fd)) {
    vsi_tiff_.reset();
    return;
  }
  if (vsi_tiff_->NumPages() == 0) {
    vsi_tiff_.reset();
    return;
  }

  // Convention: the first extra page is "macro"; if there is a second,
  // call it "label". This matches what tifffile sees on Olympus .vsi
  // exports and lines up with how other FastSlide readers expose these.
  for (size_t i = 1; i < vsi_tiff_->NumPages(); ++i) {
    const auto& page = vsi_tiff_->Page(i);
    if (page.width == 0 || page.height == 0)
      continue;
    const std::string name = (i == 1) ? "macro" : (i == 2 ? "label" : "");
    if (name.empty())
      break;
    AssociatedImage info;
    info.name = name;
    info.page = static_cast<uint16_t>(i);
    info.size = {page.width, page.height};
    associated_.push_back(std::move(info));
  }
}

// ---- Container API ---------------------------------------------------

std::vector<std::string> OlympusVsiReader::GetImageNames() const {
  std::vector<std::string> names;
  names.reserve(images_.size());
  for (const auto& img : images_) {
    names.push_back(img->GetName());
  }
  return names;
}

aifocore::Result<const SlideImage*> OlympusVsiReader::GetImage(
    int index) const {
  if (index < 0 || static_cast<size_t>(index) >= images_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format(
            "Olympus VSI: image index {} out of range [0, {})", index,
            images_.size()));
  }
  return static_cast<const SlideImage*>(images_[index].get());
}

// ---- Primary-image forwarders ---------------------------------------

int OlympusVsiReader::GetLevelCount() const {
  return Primary().GetLevelCount();
}

aifocore::Result<LevelInfo> OlympusVsiReader::GetLevelInfo(int level) const {
  return Primary().GetLevelInfo(level);
}

std::vector<ChannelMetadata> OlympusVsiReader::GetChannelMetadata() const {
  return Primary().GetChannelMetadata();
}

ImageFormat OlympusVsiReader::GetImageFormat() const {
  return Primary().GetImageFormat();
}

DataType OlympusVsiReader::GetDataType() const {
  return Primary().GetDataType();
}

ImageDimensions OlympusVsiReader::GetTileSize() const {
  return Primary().GetTileSize();
}

aifocore::Result<core::TilePlan> OlympusVsiReader::PrepareRequest(
    const core::TileRequest& request) const {
  return Primary().PrepareRequest(request);
}

aifocore::Status OlympusVsiReader::ExecutePlan(const core::TilePlan& plan,
                                               runtime::Canvas& canvas) const {
  return Primary().ExecutePlan(plan, canvas);
}

// ---- Associated thumbnails (container-level) ------------------------

std::vector<std::string> OlympusVsiReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_.size());
  for (const auto& a : associated_) {
    names.push_back(a.name);
  }
  return names;
}

aifocore::Result<ImageDimensions>
OlympusVsiReader::GetAssociatedImageDimensions(std::string_view name) const {
  for (const auto& a : associated_) {
    if (a.name == name)
      return a.size;
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Olympus VSI: associated image '{}' not found",
                            name));
}

aifocore::Result<RGBImage> OlympusVsiReader::ReadAssociatedImage(
    std::string_view name) const {
  if (!vsi_tiff_) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        "Olympus VSI: no .vsi TIFF container available");
  }
  for (const auto& a : associated_) {
    if (a.name == name) {
      return readers::simpletiff_decode::ReadAssociatedTiffPage(
          *vsi_tiff_, a.page, a.size, name);
    }
  }
  return AIFOCORE_MAKE_STATUS(
      aifocore::StatusCode::kNotFound,
      aifocore::fmt::format("Olympus VSI: associated image '{}' not found",
                            name));
}

Metadata OlympusVsiReader::GetMetadata() const {
  Metadata md;
  md[std::string(MetadataKeys::kFormat)] = std::string("OLYMPUS-VSI");
  md[std::string(MetadataKeys::kLevels)] =
      static_cast<size_t>(Primary().GetLevelCount());
  md[std::string(MetadataKeys::kChannels)] = static_cast<size_t>(3);
  if (properties_.mpp[0] > 0) {
    md[std::string(MetadataKeys::kMppX)] = properties_.mpp[0];
    md[std::string(MetadataKeys::kMppY)] = properties_.mpp[1];
  }
  const auto& primary = Primary();
  md["olympus_vsi.images"] = static_cast<size_t>(images_.size());
  md["olympus_vsi.primary_index"] = static_cast<size_t>(primary_index_);
  md["olympus_vsi.stack"] =
      primary.GetEtsPath().parent_path().filename().string();
  // Codec reported for the primary image; other stacks may declare a
  // different codec but in practice every stack in a VSI uses the same one.
  md["olympus_vsi.codec"] = std::string(CodecName(primary.GetCodec()));
  // Declared per-tile channel count for the primary image. The
  // materialised pyramid only exposes channel 0; this key lets callers
  // detect multi-channel fluorescence files even though we don't yet
  // composite them.
  md["olympus_vsi.n_channels"] = static_cast<size_t>(primary.GetNChannels());
  // Number of stacks that were discovered on disk but could not be
  // materialised (unsupported pixel type, codec, ...). Lets downstream
  // tools tell "complete slide" from "partial slide" reads. Each
  // skipped stack also gets two indexed keys with the file path and
  // the full failure ``Status::ToString()`` so tooling can render the
  // exact reason without re-running the open.
  md["olympus_vsi.skipped_stacks"] =
      static_cast<size_t>(skipped_stacks_.size());
  for (size_t i = 0; i < skipped_stacks_.size(); ++i) {
    const auto& sk = skipped_stacks_[i];
    md[aifocore::fmt::format("olympus_vsi.skipped[{}].path", i)] = sk.path;
    md[aifocore::fmt::format("olympus_vsi.skipped[{}].reason", i)] = sk.reason;
  }
  return md;
}

}  // namespace fastslide::formats::olympusvsi
