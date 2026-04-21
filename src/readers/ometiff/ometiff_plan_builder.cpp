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

#include "fastslide/readers/ometiff/ometiff_plan_builder.h"

#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"
#include "simpletiff/index.h"

namespace fastslide {

aifocore::Result<core::TilePlan> OmetiffPlanBuilder::BuildPlan(
    const core::TileRequest& request,
    const std::vector<OmeTiffLevelInfo>& pyramid,
    PlanarConfig output_planar_config,
    const simpletiff::TiffIndex& tiff_index) {
  return readers::simpletiff_plan::BuildMultiChannelPlan<OmeTiffLevelInfo>(
      request, std::span<const OmeTiffLevelInfo>(pyramid), output_planar_config,
      tiff_index);
}

}  // namespace fastslide
