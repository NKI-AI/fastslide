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

#include <cstdint>

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
/// `tile_map` is a sparse map from `(channel, x, y)` -> `LevelTileEntry`.
/// Olympus pyramids are regular grids but a level may legitimately be
/// missing border cells (no scan data there), so missing keys mean
/// "background". Brightfield stacks have a single channel (channel 0);
/// 16-bit fluorescence stacks stack several grayscale planes that share
/// the same `(x, y)` grid and are keyed by their channel index.
struct OlympusVsiLevelInfo {
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

  /// @brief Packed (channel, x, y) -> file offset/length. Sparse.
  ankerl::unordered_dense::map<uint64_t, LevelTileEntry> tile_map;

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
};

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_LEVEL_INFO_H_
