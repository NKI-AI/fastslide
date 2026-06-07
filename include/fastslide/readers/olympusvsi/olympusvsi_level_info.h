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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_LEVEL_INFO_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_LEVEL_INFO_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fastslide/image.h"
#include "fastslide/readers/olympusvsi/olympusvsi_ets.h"
#include "fastslide/utilities/unordered_dense.h"

namespace fastslide::formats::olympusvsi {

/// @brief One tile slot at a given pyramid level.
///
/// Holds the file byte range of the compressed tile payload. Tiles are
/// keyed in the level map by `(x, y)` (column, row) of the tile grid.
struct LevelTileEntry {
  uint64_t offset = 0;
  uint32_t n_bytes = 0;
};

/// @brief Per-level pyramid metadata for an Olympus VSI stack.
///
/// Tiles are grouped into one sparse map per focal/time plane. Each map is
/// keyed by `(channel, x, y)` -> `LevelTileEntry`; Olympus pyramids are
/// regular grids but a level may legitimately be missing border cells (no
/// scan data there), so missing keys mean "background". Brightfield stacks
/// have a single channel (channel 0) and a single plane; 16-bit
/// fluorescence stacks stack several grayscale channel planes, and may
/// additionally span several focal (Z) / time (T) planes.
struct OlympusVsiLevelInfo {
  /// @brief Sparse `(channel, x, y)` -> entry map for one focal/time plane.
  using TileMap = ankerl::unordered_dense::map<uint64_t, LevelTileEntry>;

  int level = 0;
  /// @brief On-disk tile-grid extent (``grid_cols * tile_w`` etc.), rounded
  ///        up to whole tiles. Used only for tile addressing.
  ImageDimensions size = {0, 0};
  /// @brief Logical (API-facing) image size for this level. Defaults to the
  ///        tile-grid extent but is replaced by the true sub-tile boundary
  ///        from the `.vsi` boundary-rect tag (2053) when available. The plan
  ///        builder clamps region reads to this, so trailing-tile padding
  ///        past the real image edge is never returned.
  ImageDimensions reported_size = {0, 0};
  /// @brief pow(2, level) downsample paired with @ref reported_size.
  double downsample = 1.0;
  uint32_t tile_w = 0;
  uint32_t tile_h = 0;
  uint32_t grid_cols = 0;
  uint32_t grid_rows = 0;

  /// @brief Number of channel planes sharing this level's grid.
  ///
  /// ``1`` for brightfield (a single RGB-decoding plane) and for
  /// single-band fluorescence. ``> 1`` for stacked grayscale
  /// fluorescence, where each plane is one output channel.
  uint32_t n_channels = 1;

  /// @brief Number of focal (Z) planes. ``1`` for a plain 2D image.
  uint32_t z_count = 1;
  /// @brief Number of time (T) points. ``1`` for a plain 2D image.
  uint32_t t_count = 1;

  /// @brief One tile map per (focal, time) plane, laid out plane-major as
  ///        ``z * t_count + t``. A plain 2D image has a single entry.
  std::vector<TileMap> plane_maps;

  /// @brief Pack tile-grid (x, y) into a 64-bit key (channel 0).
  static constexpr uint64_t PackKey(uint32_t x, uint32_t y) {
    return (static_cast<uint64_t>(y) << 32) | static_cast<uint64_t>(x);
  }

  /// @brief Pack (channel, x, y) into a 64-bit key.
  ///
  /// Channel occupies the top 8 bits, row the next 24, column the low
  /// 32. ``PackKey3(0, x, y) == PackKey(x, y)`` so single-channel maps
  /// (and existing call sites / tests) stay bit-compatible.
  static constexpr uint64_t PackKey3(uint32_t channel, uint32_t x, uint32_t y) {
    return (static_cast<uint64_t>(channel) << 56) |
           (static_cast<uint64_t>(y) << 32) | static_cast<uint64_t>(x);
  }

  /// @brief Plane-major index of focal plane ``z`` / time point ``t``.
  [[nodiscard]] size_t PlaneOffset(uint32_t z, uint32_t t) const {
    return static_cast<size_t>(z) * t_count + t;
  }

  /// @brief Tile map for plane (z, t), or ``nullptr`` when out of range.
  [[nodiscard]] const TileMap* MapForPlane(uint32_t z, uint32_t t) const {
    if (z >= z_count || t >= t_count) {
      return nullptr;
    }
    const size_t idx = PlaneOffset(z, t);
    return idx < plane_maps.size() ? &plane_maps[idx] : nullptr;
  }
};

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_LEVEL_INFO_H_
