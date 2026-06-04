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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_TILE_EXECUTOR_H_

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/olympusvsi/olympusvsi_exec_context.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide::formats::olympusvsi {

/// @brief Executor for Olympus VSI `TilePlan`s.
///
/// For each `TileReadOp` in the plan, the executor:
///   1. Reads the compressed tile bytes from the backing `frame_t.ets`
///      file at `[byte_offset, byte_offset + byte_size)`.
///   2. Sniffs the codec from the leading magic bytes (fallback: the
///      ETS-header-declared compression).
///   3. Decodes to packed RGB8 via `runtime::decoders::DecodeJ2kToRgb`
///      or `runtime::decoders::DecodeJpegToRgb`.
///   4. Paints the decoded tile onto the canvas via
///      `simpletiff_exec::PaintTileMaybeLocked` with 3 channels.
///
/// Decoded tiles can be cached in the slide's optional `ITileCache` to
/// avoid redundant decoder work when the same region is read repeatedly.
class OlympusVsiTileExecutor {
 public:
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const OlympusVsiExecContext& context,
                                      runtime::Canvas& canvas);
};

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_TILE_EXECUTOR_H_
