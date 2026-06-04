// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#include "fastslide/readers/generictiff/generictiff_plan_builder.h"

#include <span>

#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> GenericTiffPlanBuilder::BuildPlan(
    const core::TileRequest& request, const GenericTiffPlanContext& context) {
  return readers::simpletiff_plan::BuildSinglePagePlan<GenericTiffLevelInfo>(
      request, context.pyramid_levels, context.tiff_index, "GenericTIFF");
}

}  // namespace fastslide
