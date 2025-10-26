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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_PLAN_BUILDER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_PLAN_BUILDER_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "fastslide/core/slide_descriptor.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/core/tile_request.h"

namespace fastslide {

// Forward declarations
class AperioReader;

/// @brief TIFF structure metadata needed for tile execution
///
/// This struct captures TIFF file structure information queried during
/// planning that is needed by the executor for tile reading.
struct TiffStructureMetadata {
  uint16_t page = 0;               ///< TIFF page/directory number
  uint32_t tile_width = 0;         ///< Tile width in pixels
  uint32_t tile_height = 0;        ///< Tile height (or rows per strip)
  uint16_t samples_per_pixel = 3;  ///< Number of channels (typically 3 for RGB)
  bool is_tiled = true;            ///< Whether TIFF uses tiles (vs strips)
};

/// @brief Helper class for building Aperio SVS tile plans
///
/// Implements the planning stage of the two-stage pipeline for Aperio readers.
/// Analyzes tile requests and produces execution plans without performing I/O.
///
/// **Responsibilities:**
/// - Validate request parameters (level bounds, region coordinates)
/// - Query TIFF structure (tile dimensions, channel count)
/// - Enumerate tiles that intersect the requested region
/// - Calculate clipping and coordinate transforms
/// - Create TileReadOp descriptors for each tile
/// - Estimate execution costs
///
/// **Design:**
/// - Static helper class (no state)
/// - Thread-safe (read-only operations)
/// - Uses handle pool for TIFF queries (same as execution)
///
/// @see AperioTileExecutor for the execution stage
class AperioPlanBuilder {
 public:
  /// @brief Build a tile plan for an Aperio SVS request
  /// @param request The tile request specifying region and level
  /// @param reader The Aperio reader instance (for accessing slide metadata)
  /// @param[out] tiff_metadata Output parameter for TIFF structure info
  /// @return Tile plan or error status
  ///
  /// @note This method queries TIFF structure once and stores metadata
  ///       for use by the executor, avoiding redundant queries.
  static absl::StatusOr<core::TilePlan> BuildPlan(
      const core::TileRequest& request, const AperioReader& reader,
      TiffStructureMetadata& tiff_metadata);

 private:
  /// @brief Validate the request parameters
  /// @param request The tile request
  /// @param reader The Aperio reader instance
  /// @return Status indicating success or failure
  static absl::Status ValidateRequest(const core::TileRequest& request,
                                      const AperioReader& reader);

  /// @brief Determine region bounds from request
  /// @param request The tile request
  /// @param level_info Level dimensions and downsample factor
  /// @param[out] x Output X coordinate
  /// @param[out] y Output Y coordinate
  /// @param[out] width Output width
  /// @param[out] height Output height
  static void DetermineRegionBounds(const core::TileRequest& request,
                                    const LevelInfo& level_info, double& x,
                                    double& y, uint32_t& width,
                                    uint32_t& height);

  /// @brief Query TIFF structure from file
  /// @param reader Aperio reader with handle pool access
  /// @param page TIFF page/directory to query
  /// @param level_info Level dimensions for strip fallback
  /// @param[out] tiff_metadata Output structure metadata
  /// @return Status indicating success or failure
  static absl::Status QueryTiffStructure(const AperioReader& reader,
                                         uint16_t page,
                                         const LevelInfo& level_info,
                                         TiffStructureMetadata& tiff_metadata);

  /// @brief Create tile operations for intersecting tiles
  /// @param request The tile request
  /// @param tiff_metadata TIFF structure information
  /// @param level_info Level dimensions
  /// @param x Region X coordinate
  /// @param y Region Y coordinate
  /// @param width Region width
  /// @param height Region height
  /// @return Vector of tile operations
  static std::vector<core::TileReadOp> CreateTileOperations(
      const core::TileRequest& request,
      const TiffStructureMetadata& tiff_metadata, const LevelInfo& level_info,
      double x, double y, uint32_t width, uint32_t height);

  /// @brief Create output specification for the plan
  /// @param width Output width
  /// @param height Output height
  /// @param samples_per_pixel Number of channels
  /// @return Output specification
  static core::OutputSpec CreateOutputSpec(uint32_t width, uint32_t height,
                                           uint16_t samples_per_pixel);

  /// @brief Calculate cost estimates for the plan
  /// @param operations Vector of tile operations
  /// @return Cost estimate
  static core::TilePlan::Cost CalculateCosts(
      const std::vector<core::TileReadOp>& operations);
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_APERIO_APERIO_PLAN_BUILDER_H_
