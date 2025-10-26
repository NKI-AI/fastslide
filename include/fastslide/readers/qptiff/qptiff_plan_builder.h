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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_PLAN_BUILDER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_PLAN_BUILDER_H_

#include <vector>

#include "absl/status/statusor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"
#include "fastslide/readers/qptiff/qptiff.h"
#include "fastslide/utilities/tiff/tiff_file.h"

/**
 * @file qptiff_plan_builder.h
 * @brief QPTIFF tile plan builder (two-stage pipeline stage 1)
 * 
 * This header defines QptiffPlanBuilder, a helper class for creating
 * tile reading plans for QPTIFF images. This is stage 1 of the two-stage
 * pipeline, which analyzes the request and creates a plan without performing
 * any I/O.
 * 
 * **Responsibilities:**
 * - Determine which TIFF pages contain the requested channels
 * - Calculate tile intersections with the requested region
 * - Handle multi-channel spectral images
 * - Support both planar (separated) and interleaved channel layouts
 * - Estimate I/O costs for cache optimization
 * 
 * **QPTIFF Specifics:**
 * - One TIFF page per channel per pyramid level
 * - Channels may have different tile sizes
 * - Supports selective channel loading
 * - Handles both RGB and spectral image formats
 * 
 * @see QptiffTileExecutor for stage 2 (plan execution)
 * @see QpTiffReader for the main reader class
 */

namespace fastslide {

/// @brief Helper class for building QPTIFF tile plans
class QptiffPlanBuilder {
 public:
  /// @brief Build a tile plan for a QPTIFF request
  /// @param request The tile request
  /// @param pyramid Pyramid levels information
  /// @param output_planar_config Planar configuration for output
  /// @param tiff_file TiffFile instance for querying tile structure
  /// @return Tile plan or error status
  static absl::StatusOr<core::TilePlan> BuildPlan(
      const core::TileRequest& request,
      const std::vector<QpTiffLevelInfo>& pyramid,
      PlanarConfig output_planar_config, TiffFile& tiff_file);

 private:
  /// @brief Validate the request parameters
  /// @param request The tile request
  /// @param pyramid Pyramid levels information
  /// @return Status indicating success or failure
  static absl::Status ValidateRequest(
      const core::TileRequest& request,
      const std::vector<QpTiffLevelInfo>& pyramid);

  /// @brief Determine region bounds from request
  /// @param request The tile request
  /// @param level_info Level information
  /// @param x Output X coordinate
  /// @param y Output Y coordinate
  /// @param width Output width
  /// @param height Output height
  static void DetermineRegionBounds(const core::TileRequest& request,
                                    const QpTiffLevelInfo& level_info,
                                    double& x, double& y, uint32_t& width,
                                    uint32_t& height);

  /// @brief Get tile dimensions from TIFF file
  /// @param tiff_file TiffFile instance
  /// @param level_info Level information
  /// @param tile_width Output tile width
  /// @param tile_height Output tile height
  /// @param is_tiled Output whether pages are tiled
  /// @return Status indicating success or failure
  static absl::Status GetTileDimensions(TiffFile& tiff_file,
                                        const QpTiffLevelInfo& level_info,
                                        uint32_t& tile_width,
                                        uint32_t& tile_height, bool& is_tiled);

  /// @brief Create tile operations for intersecting tiles
  /// @param request The tile request
  /// @param level_info Level information
  /// @param region_x Region X coordinate
  /// @param region_y Region Y coordinate
  /// @param width Region width
  /// @param height Region height
  /// @param tile_width Tile width
  /// @param tile_height Tile height
  /// @param is_tiled Whether pages are tiled
  /// @param bytes_per_sample Bytes per sample
  /// @return Vector of tile operations
  static std::vector<core::TileReadOp> CreateTileOperations(
      const core::TileRequest& request, const QpTiffLevelInfo& level_info,
      uint32_t region_x, uint32_t region_y, uint32_t width, uint32_t height,
      uint32_t tile_width, uint32_t tile_height, bool is_tiled,
      uint32_t bytes_per_sample);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_QPTIFF_QPTIFF_PLAN_BUILDER_H_
