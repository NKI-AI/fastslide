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

#include "fastslide/readers/qptiff/qptiff_plan_builder.h"

#include "aifocore/status/result.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> QptiffPlanBuilder::BuildPlan(
    const core::TileRequest& request, const QptiffPlanContext& context) {
  return readers::simpletiff_plan::BuildMultiChannelPlan<QpTiffLevelInfo>(
      request, context.pyramid, context.output_planar_config,
      context.tiff_index);
}

}  // namespace fastslide
