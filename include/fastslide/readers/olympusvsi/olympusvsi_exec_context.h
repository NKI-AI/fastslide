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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_EXEC_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_EXEC_CONTEXT_H_

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "fastslide/readers/olympusvsi/olympusvsi_ets.h"
#include "fastslide/readers/olympusvsi/olympusvsi_level_info.h"
#include "fastslide/runtime/cache_interface.h"

namespace fastslide::formats::olympusvsi {

/// @brief Read-only view of the Olympus VSI reader state needed during
/// `ExecutePlan`.
///
/// The reader owns the underlying storage (the parsed pyramid and the path
/// to the backing `frame_t.ets` file). The executor reads tile bytes from
/// disk via the path, so we keep the API as a thin POD passed by value.
struct OlympusVsiExecContext {
  /// @brief Path to the backing `frame_t.ets` file, used for `pread`.
  std::string_view ets_path;

  /// @brief Pyramid levels (one entry per level, in ascending level index).
  std::span<const OlympusVsiLevelInfo> pyramid;

  /// @brief Declared tile codec from the ETS header (JPEG or JPEG 2000).
  TileCodec declared_codec = TileCodec::kUnknown;

  /// @brief Per-sample storage width (UINT8 -> 8-bit RGB tile, UINT16
  /// -> 16-bit RGB or grayscale tile). Drives both decoder dispatch
  /// and the byte size of cached tile payloads.
  TilePixelType pixel_type = TilePixelType::kUInt8;

  /// @brief Declared per-tile channel count from the ETS header.
  ///
  /// Used together with `pixel_type` to decide how the decoded buffer
  /// is fed to the Canvas: 8-bit RGB tiles paint as 3-channel uint8;
  /// 16-bit single-channel tiles paint as 1-channel uint16; 16-bit
  /// RGB tiles paint as 3-channel uint16.
  uint32_t n_channels = 3U;

  /// @brief Optional decoded-tile cache. May be null when caching is off.
  std::shared_ptr<runtime::ITileCache> cache;
};

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_EXEC_CONTEXT_H_
