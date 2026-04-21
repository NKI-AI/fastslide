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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_TILE_EXECUTOR_H_

#include "fastslide/readers/multi_channel_tiff_tile_executor.h"
#include "fastslide/readers/qptiff/qptiff.h"

/**
 * @file qptiff_tile_executor.h
 * @brief QPTIFF tile plan executor (alias for the shared multi-channel
 *        TIFF executor template).
 *
 * QPTIFF stores one TIFF page per channel per pyramid level. The runtime
 * pipeline (cache lookup, page-state cache, decode via simpletiff, blend) is
 * identical to OME-TIFF, so it lives in MultiChannelTiffTileExecutor.
 *
 * @see fastslide::MultiChannelTiffTileExecutor
 * @see QptiffPlanBuilder for stage 1 (plan creation)
 * @see QpTiffReader for the main reader class
 */

namespace fastslide {

using QptiffTileExecutor =
    MultiChannelTiffTileExecutor<QpTiffReader, QpTiffLevelInfo>;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_TILE_EXECUTOR_H_
