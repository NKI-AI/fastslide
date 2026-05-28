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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_PLAN_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_PLAN_CONTEXT_H_

#include <cstdint>

#include "fastslide/image.h"
#include "fastslide/readers/bif/bif_spatial_index.h"

namespace fastslide {

/// @brief Read-only inputs the BIF plan builder needs for one level.
struct BifPlanContext {
  const bif::BifSpatialIndex* spatial_index = nullptr;
  ImageDimensions level_dims = {0, 0};
  int level = 0;
  int scan_white_point = 255;  ///< Background fill for uncovered output.
  uint32_t channels = 3;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_BIF_BIF_PLAN_CONTEXT_H_
