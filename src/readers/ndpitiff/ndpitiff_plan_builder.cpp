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

#include "fastslide/readers/ndpitiff/ndpitiff_plan_builder.h"

#include <span>

#include "fastslide/readers/ndpitiff/ndpitiff.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> NdpiTiffPlanBuilder::BuildPlan(
    const core::TileRequest& request, const NdpiTiffReader& reader) {
  const auto& pyramid = reader.GetPyramidLevels();
  return readers::simpletiff_plan::BuildSinglePagePlan<NdpiTiffLevelInfo>(
      request, std::span<const NdpiTiffLevelInfo>(pyramid),
      reader.GetTiffIndex(), "NDPI");
}

}  // namespace fastslide
