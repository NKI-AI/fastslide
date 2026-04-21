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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_TILE_EXECUTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_TILE_EXECUTOR_H_

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/tiff_based_tile_executor.h"
#include "fastslide/runtime/tile_writer.h"

namespace fastslide {

class NdpiTiffReader;

class NdpiTiffTileExecutor
    : public TiffBasedTileExecutor<NdpiTiffTileExecutor> {
 public:
  static aifocore::Status ExecutePlan(const core::TilePlan& plan,
                                      const NdpiTiffReader& reader,
                                      runtime::Canvas& writer);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_NDPITIFF_NDPITIFF_TILE_EXECUTOR_H_
