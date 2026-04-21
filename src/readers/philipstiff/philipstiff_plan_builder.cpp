// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#include "fastslide/readers/philipstiff/philipstiff_plan_builder.h"

#include <span>

#include "fastslide/readers/philipstiff/philipstiff.h"
#include "fastslide/readers/simpletiff_plan_builder_utils.h"

namespace fastslide {

aifocore::Result<core::TilePlan> PhilipsTiffPlanBuilder::BuildPlan(
    const core::TileRequest& request, const PhilipsTiffReader& reader) {
  const auto& pyramid = reader.GetPyramidLevels();
  return readers::simpletiff_plan::BuildSinglePagePlan<PhilipsTiffLevelInfo>(
      request, std::span<const PhilipsTiffLevelInfo>(pyramid),
      reader.GetTiffIndex(), "PhilipsTIFF");
}

}  // namespace fastslide
