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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_TILE_EXECUTOR_H_

#include "fastslide/readers/imagejtiff/imagejtiff_exec_context.h"
#include "fastslide/readers/imagejtiff/imagejtiff_level_info.h"
#include "fastslide/readers/multi_channel_tiff_tile_executor.h"

/**
 * @file imagejtiff_tile_executor.h
 * @brief ImageJ TIFF tile plan executor (alias for the shared multi-channel
 *        TIFF executor template).
 *
 * ImageJ hyperstacks store one TIFF page per channel for a given z/t plane,
 * identical at runtime to QPTIFF / OME-TIFF, so the executor is reused.
 */

namespace fastslide {

using ImageJTiffTileExecutor =
    MultiChannelTiffTileExecutor<ImageJTiffExecContext, ImageJTiffLevelInfo>;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_IMAGEJTIFF_IMAGEJTIFF_TILE_EXECUTOR_H_
