// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#include "fastslide/readers/philipstiff/philipstiff_plan_builder.h"

#include <span>

#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> PhilipsTiffPlanBuilder::BuildPlan(
    const core::TileRequest& request, const PhilipsTiffPlanContext& context) {
  return readers::simpletiff_plan::BuildSinglePagePlan<PhilipsTiffLevelInfo>(
      request, context.pyramid_levels, context.tiff_index, "PhilipsTIFF");
}

}  // namespace fastslide
