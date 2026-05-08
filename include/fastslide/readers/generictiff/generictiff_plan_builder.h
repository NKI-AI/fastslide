// Copyright 2025 Jonas Teuwen. All Rights Reserved.

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_PLAN_BUILDER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_PLAN_BUILDER_H_

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/readers/generictiff/generictiff_plan_context.h"

namespace fastslide {

class GenericTiffPlanBuilder {
 public:
  /// @brief Build a tile plan for a generic TIFF request.
  static aifocore::Result<core::TilePlan> BuildPlan(
      const core::TileRequest& request, const GenericTiffPlanContext& context);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_GENERICTIFF_GENERICTIFF_PLAN_BUILDER_H_
