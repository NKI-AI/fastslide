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

/// @file mrxs_pipeline.cpp
/// @brief Implementation of the `MrxsPipeline` facade.

#include "fastslide/readers/mrxs/mrxs_pipeline.h"

#include <memory>
#include <utility>

#include "fastslide/readers/mrxs/mrxs_exec_context.h"
#include "fastslide/readers/mrxs/mrxs_plan_builder.h"
#include "fastslide/readers/mrxs/mrxs_plan_context.h"
#include "fastslide/readers/mrxs/mrxs_tile_executor.h"
#include "fastslide/readers/mrxs/spatial_index.h"

namespace fastslide {
namespace mrxs {

aifocore::Result<core::TilePlan> MrxsPipeline::BuildPlan(
    const core::TileRequest& request, const SlideDataInfo& slide_info,
    const LevelInfo& level_info,
    std::shared_ptr<const MrxsSpatialIndex> spatial_index) {
  const MrxsPlanContext context{
      .slide_info = slide_info,
      .level_info = level_info,
      .spatial_index = std::move(spatial_index),
  };
  return MrxsPlanBuilder::BuildPlan(request, context);
}

aifocore::Status MrxsPipeline::ExecutePlan(
    const core::TilePlan& plan, const fs::path& dirname,
    const SlideDataInfo& slide_info, std::shared_ptr<runtime::ITileCache> cache,
    runtime::Canvas& writer) {
  const MrxsExecContext context(dirname, slide_info, std::move(cache));
  return MrxsTileExecutor::ExecutePlan(plan, context, writer);
}

}  // namespace mrxs
}  // namespace fastslide
