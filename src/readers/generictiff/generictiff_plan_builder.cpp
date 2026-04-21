// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#include "fastslide/readers/generictiff/generictiff_plan_builder.h"

#include <span>

#include "fastslide/readers/generictiff/generictiff.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> GenericTiffPlanBuilder::BuildPlan(
    const core::TileRequest& request, const GenericTiffReader& reader) {
  const auto& pyramid = reader.GetPyramidLevels();
  return readers::simpletiff_plan::BuildSinglePagePlan<GenericTiffLevelInfo>(
      request, std::span<const GenericTiffLevelInfo>(pyramid),
      reader.GetTiffIndex(), "GenericTIFF");
}

}  // namespace fastslide
