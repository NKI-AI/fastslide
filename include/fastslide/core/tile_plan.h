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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_TILE_PLAN_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_TILE_PLAN_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "fastslide/core/tile_request.h"
#include "fastslide/image.h"

/**
 * @file tile_plan.h
 * @brief Tile reading execution plan (pure metadata, no I/O)
 *
 * This header defines the TilePlan structure that represents the result
 * of planning a tile read operation. Plans are pure data structures that
 * specify WHAT to read without actually performing I/O, enabling:
 * - Unit testing of planning logic without filesystem
 * - Batching multiple requests efficiently
 * - Async execution with worker pools
 * - Prefetching and caching strategies
 * - Cost estimation before execution
 */

namespace fastslide {
namespace core {

/// @brief Coordinate transformation for a tile
///
/// Describes how to transform a tile from its native coordinates to
/// the requested output space (e.g., scaling, cropping, rotation).
struct TileTransform {
  /// @brief Source region within the tile (pixels)
  struct SourceRegion {
    uint32_t x;       ///< X offset within tile
    uint32_t y;       ///< Y offset within tile
    uint32_t width;   ///< Width to read
    uint32_t height;  ///< Height to read
  } source;

  /// @brief Destination region in output image (pixels)
  struct DestRegion {
    uint32_t x;       ///< X offset in output
    uint32_t y;       ///< Y offset in output
    uint32_t width;   ///< Width in output
    uint32_t height;  ///< Height in output
  } dest;

  /// @brief Scale factor (if resampling needed)
  double scale_x = 1.0;
  double scale_y = 1.0;

  /// @brief Whether scaling is needed
  [[nodiscard]] bool NeedsScaling() const {
    return scale_x != 1.0 || scale_y != 1.0;
  }

  /// @brief Whether cropping is needed
  [[nodiscard]] bool NeedsCropping() const {
    return source.width != dest.width || source.height != dest.height;
  }
};

/// @brief Blending mode for tile composition
enum class BlendMode : uint8_t {
  kOverwrite,     ///< Simply overwrite destination (TIFF, SVS)
  kAverage,       ///< Average overlapping regions (MRXS)
  kMaxIntensity,  ///< Take maximum intensity (future)
  kMinIntensity,  ///< Take minimum intensity (future)
};

/// @brief Blend metadata for weighted tile composition
///
/// Optional metadata for formats requiring weighted blending or fractional
/// positioning. Used by WeightedTileWriter to handle overlapping tiles and
/// subpixel placement.
struct BlendMetadata {
  /// @brief Fractional offset from integer position
  /// @note For subpixel-accurate placement via resampling kernels
  double fractional_x = 0.0;
  double fractional_y = 0.0;

  /// @brief Coverage/confidence weight for this tile
  /// @note 1.0 = full confidence, 0.0 = ignore tile
  double weight = 1.0;

  /// @brief Intensity gain correction factor
  /// @note 1.0 = no correction. Applied in linear RGB space.
  /// @note MRXS-specific: corrects illumination variations (typically
  /// 0.97-1.04)
  float gain = 1.0f;

  /// @brief Blending mode
  BlendMode mode = BlendMode::kOverwrite;

  /// @brief Enable subpixel resampling (Magic Kernel for MRXS)
  /// @note Only meaningful if fractional_x or fractional_y is non-zero
  bool enable_subpixel_resampling = true;
};

/// @brief Single tile read operation
///
/// Specifies one physical tile to read and how to place it in the output.
/// This is pure metadata - no I/O or decoding information.
struct TileReadOp {
  int level;                  ///< Pyramid level of the tile
  TileCoordinate tile_coord;  ///< Tile grid coordinates
  TileTransform transform;    ///< How to transform tile → output

  /// @brief File/data source identifier (format-specific)
  /// @note For MRXS: index into datafile list
  /// @note For TIFF: TIFF directory index
  uint32_t source_id;

  /// @brief Byte offset within source (format-specific)
  uint64_t byte_offset;

  /// @brief Size of compressed data in bytes
  uint32_t byte_size;

  /// @brief Priority for async execution (higher = read first)
  int priority = 0;

  /// @brief Optional blending metadata for weighted composition
  /// @note Used by formats with overlapping tiles (MRXS) or fractional
  /// positioning
  std::optional<BlendMetadata> blend_metadata;
};

/// @brief Output specification for tile reading
struct OutputSpec {
  ImageDimensions dimensions;           ///< Output image dimensions
  uint32_t channels;                    ///< Number of output channels
  std::vector<size_t> channel_indices;  ///< Which channels to extract

  /// @brief Pixel format for output
  enum class PixelFormat {
    kUInt8,   ///< 8-bit unsigned integer
    kUInt16,  ///< 16-bit unsigned integer
    kFloat32  ///< 32-bit float
  } pixel_format = PixelFormat::kUInt8;

  /// @brief Planar configuration for output
  PlanarConfig planar_config = PlanarConfig::kContiguous;

  /// @brief Whether to apply color correction
  bool apply_color_correction = false;

  /// @brief Background color for missing tiles
  struct Background {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
  } background;

  /// @brief Get total output size in bytes
  [[nodiscard]] size_t GetTotalBytes() const {
    size_t bytes_per_pixel = channels;
    if (pixel_format == PixelFormat::kUInt16) {
      bytes_per_pixel *= 2;
    } else if (pixel_format == PixelFormat::kFloat32) {
      bytes_per_pixel *= 4;
    }
    return static_cast<size_t>(dimensions[0]) * dimensions[1] * bytes_per_pixel;
  }
};

/// @brief Complete tile reading plan
///
/// The result of PrepareRequest() - specifies WHAT tiles to read and HOW
/// to assemble them into the output image. This is pure metadata that can
/// be inspected, modified, batched, or cached without performing I/O.
struct TilePlan {
  /// @brief Original request that generated this plan
  TileRequest request;

  /// @brief Output specification
  OutputSpec output;

  /// @brief Tile read operations (in dependency order)
  std::vector<TileReadOp> operations;

  /// @brief Clamped/validated region (may differ from request)
  RegionSpec actual_region;

  /// @brief Estimated cost metrics
  struct Cost {
    size_t total_bytes_to_read;  ///< Total I/O in bytes
    size_t total_tiles;          ///< Number of tiles to read
    size_t tiles_to_decode;      ///< Tiles needing decompression
    size_t tiles_from_cache;     ///< Tiles available in cache (if checked)
    double estimated_time_ms;    ///< Estimated time (rough)
  } cost;

  /// @brief Check if plan is valid
  [[nodiscard]] bool IsValid() const {
    return actual_region.IsValid() && !operations.empty();
  }

  /// @brief Check if plan is empty (nothing to read)
  [[nodiscard]] bool IsEmpty() const { return operations.empty(); }

  /// @brief Get total number of operations
  [[nodiscard]] size_t GetOperationCount() const { return operations.size(); }

  /// @brief Get operations that need I/O (not in cache)
  [[nodiscard]] std::vector<const TileReadOp*> GetUncachedOperations() const;
};

/// @brief Batch tile reading plan for multiple requests
///
/// Allows batching multiple TileRequests into a single execution plan,
/// enabling optimizations like:
/// - Deduplicating overlapping tiles
/// - Sorting operations by file offset for sequential I/O
/// - Parallel execution of independent operations
struct BatchTilePlan {
  /// @brief Individual plans for each request
  std::vector<TilePlan> plans;

  /// @brief Deduplicated tile operations (shared across plans)
  std::vector<TileReadOp> unique_operations;

  /// @brief Mapping from plan index → operation indices
  std::vector<std::vector<size_t>> plan_operation_map;

  /// @brief Get total operation count (including duplicates)
  [[nodiscard]] size_t GetTotalOperations() const;

  /// @brief Get unique operation count (after deduplication)
  [[nodiscard]] size_t GetUniqueOperations() const {
    return unique_operations.size();
  }

  /// @brief Get estimated total I/O in bytes
  [[nodiscard]] size_t GetEstimatedIO() const;
};

}  // namespace core

// Import into fastslide namespace
using core::BatchTilePlan;
using core::OutputSpec;
using core::TilePlan;
using core::TileReadOp;
using core::TileTransform;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_CORE_TILE_PLAN_H_
