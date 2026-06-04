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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_PLAN_CONTEXT_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_PLAN_CONTEXT_H_

#include <span>

#include "fastslide/image.h"
#include "fastslide/readers/qptiff/qptiff_level_info.h"
#include "simpletiff/index.h"

namespace fastslide {

/// @brief Read-only view of QPTIFF state needed by the plan builder.
struct QptiffPlanContext {
  std::span<const QpTiffLevelInfo> pyramid;
  PlanarConfig output_planar_config = PlanarConfig::kSeparate;
  const simpletiff::TiffIndex& tiff_index;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_PLAN_CONTEXT_H_
