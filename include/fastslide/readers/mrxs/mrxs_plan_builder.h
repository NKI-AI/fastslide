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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_PLAN_BUILDER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_PLAN_BUILDER_H_

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/mrxs/mrxs_internal.h"
#include "fastslide/readers/mrxs/spatial_index.h"

namespace fastslide {

// Forward declarations
class MrxsReader;

namespace mrxs {
class MrxsSpatialIndex;
}

/// @brief Helper class for building MRXS tile plans
class MrxsPlanBuilder {
 public:
  /// @brief Build a tile plan for an MRXS request
  /// @param request The tile request
  /// @param reader The MRXS reader instance (for accessing slide info and spatial index)
  /// @return Tile plan or error status
  static absl::StatusOr<core::TilePlan> BuildPlan(
      const core::TileRequest& request, const MrxsReader& reader);

 private:
  /// @brief Validate the request parameters
  /// @param request The tile request
  /// @param reader The MRXS reader instance
  /// @return Status indicating success or failure
  static absl::Status ValidateRequest(const core::TileRequest& request,
                                      const MrxsReader& reader);

  /// @brief Determine region bounds from request
  /// @param request The tile request
  /// @param level_info Level information
  /// @param x Output X coordinate
  /// @param y Output Y coordinate
  /// @param width Output width
  /// @param height Output height
  static void DetermineRegionBounds(const core::TileRequest& request,
                                    const LevelInfo& level_info, double& x,
                                    double& y, uint32_t& width,
                                    uint32_t& height);

  /// @brief Create tile operations for intersecting tiles
  /// @param request The tile request
  /// @param spatial_index The spatial index for the level
  /// @param x Region X coordinate
  /// @param y Region Y coordinate
  /// @param width Region width
  /// @param height Region height
  /// @return Vector of tile operations
  static std::vector<core::TileReadOp> CreateTileOperations(
      const core::TileRequest& request,
      const mrxs::MrxsSpatialIndex& spatial_index, double x, double y,
      uint32_t width, uint32_t height);

  /// @brief Calculate clipping and transforms for a single tile
  /// @param request The tile request
  /// @param spatial_tile The spatial tile information
  /// @param x Region X coordinate
  /// @param y Region Y coordinate
  /// @param width Region width
  /// @param height Region height
  /// @return Tile operation or nullopt if tile should be skipped
  static std::optional<core::TileReadOp> CreateTileOperation(
      const core::TileRequest& request, const mrxs::SpatialTile& spatial_tile,
      double x, double y, uint32_t width, uint32_t height);

  /// @brief Create output specification for the plan
  /// @param width Output width
  /// @param height Output height
  /// @param zoom_level The zoom level info
  /// @return Output specification
  static core::OutputSpec CreateOutputSpec(
      uint32_t width, uint32_t height, const mrxs::SlideZoomLevel& zoom_level);

  /// @brief Calculate cost estimates for the plan
  /// @param operations Vector of tile operations
  /// @return Cost estimate
  static core::TilePlan::Cost CalculateCosts(
      const std::vector<core::TileReadOp>& operations);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_MRXS_MRXS_PLAN_BUILDER_H_
