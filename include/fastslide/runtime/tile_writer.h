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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

/**
 * @file tile_writer.h
 * @brief Primary tile writer interface with automatic strategy selection
 * 
 * This is the main tile writing interface for FastSlide. It automatically:
 * - Chooses between blended (weighted) vs direct writing based on plan metadata
 * - Supports arbitrary channel counts (1, 3, 4, N) and data types
 * - Provides optimal performance for each format (MRXS, SVS, QPTIFF, etc.)
 * - Maintains clean, consistent API across all slide formats
 */

namespace fastslide {
namespace runtime {

// Forward declaration
class ITileWriterStrategy;

/// @brief Primary tile writer with automatic strategy selection
///
/// This class provides a clean, unified interface for all tile writing operations.
/// It automatically selects the most efficient internal implementation based on
/// the TilePlan characteristics:
///
/// - **Blended Writing**: For MRXS and other formats requiring fractional positioning
/// - **Direct Writing**: For SVS, QPTIFF, and other aligned tile formats
/// - **Multi-Channel Support**: Handles 1, 3, 4, or N channels seamlessly
/// - **Multiple Data Types**: UInt8, UInt16, Float32 with optimal performance
///
/// ## Usage Examples:
///
/// ### Simple Usage (Automatic Strategy Selection):
/// ```cpp
/// // Automatically chooses strategy based on plan
/// TileWriter writer(plan);
///
/// // Execute tiles (same interface for all formats)
/// for (const auto& op : plan.operations) {
///   auto status = reader.ExecutePlan(plan, writer);
/// }
///
/// // Get result (returns appropriate type based on channels)
/// auto result = writer.Finalize();
/// ```
///
/// ### Advanced Usage (Manual Configuration):
/// ```cpp
/// // Manual configuration for specific needs
/// TileWriter::Config config;
/// config.dimensions = {1024, 1024};
/// config.channels = 5;  // Spectral imaging data
/// config.data_type = DataType::kUInt16;
/// config.enable_blending = false;  // Force direct mode
///
/// TileWriter writer(config);
/// ```
class TileWriter {
 public:
  /// @brief Destructor (required for PIMPL with incomplete strategy types)
  virtual ~TileWriter();

  /// @brief Background color specification (multi-channel)
  struct BackgroundColor {
    std::vector<double> values;  // Per-channel background values [0-255]

    // Convenience constructors
    BackgroundColor() : values{255.0} {}  // White/max for single channel

    explicit BackgroundColor(uint8_t gray)
        : values{static_cast<double>(gray)} {}

    BackgroundColor(uint8_t r, uint8_t g, uint8_t b)
        : values{static_cast<double>(r), static_cast<double>(g),
                 static_cast<double>(b)} {}

    explicit BackgroundColor(std::vector<double> vals)
        : values(std::move(vals)) {}
  };

  /// @brief Configuration for manual writer creation
  struct Config {
    ImageDimensions dimensions;
    uint32_t channels = 3;
    DataType data_type = DataType::kUInt8;
    PlanarConfig planar_config = PlanarConfig::kContiguous;
    BackgroundColor background;
    bool enable_blending = false;  // Auto-detected by default
    bool enable_subpixel_resampling = true;
  };

  /// @brief Output image (always returns Image since RGBImage is just an alias)
  using OutputImage = Image;

  /// @brief Create writer from TilePlan (automatic strategy selection)
  ///
  /// This is the recommended constructor. It analyzes the plan to:
  /// - Detect if blending is needed (blend_metadata present)
  /// - Determine channel count and data type
  /// - Choose optimal implementation strategy
  ///
  /// @param plan Execution plan containing output spec and operations
  explicit TileWriter(const core::TilePlan& plan);

  /// @brief Create writer with manual configuration
  ///
  /// For advanced users who need specific control over the writer behavior.
  ///
  /// @param config Manual configuration
  explicit TileWriter(const Config& config);

  /// @brief Convenience constructor for RGB output
  ///
  /// Creates a writer optimized for RGB output with optional blending.
  ///
  /// @param width Output width
  /// @param height Output height
  /// @param background RGB background color
  /// @param enable_blending Whether to enable advanced blending features
  TileWriter(uint32_t width, uint32_t height,
             BackgroundColor background = BackgroundColor(255, 255, 255),
             bool enable_blending = false);

  // Tile writer interface methods
  [[nodiscard]] absl::Status WriteTile(const core::TileReadOp& op,
                                       std::span<const uint8_t> pixel_data,
                                       uint32_t tile_width,
                                       uint32_t tile_height,
                                       uint32_t tile_channels);

  /// @brief Write tile with explicit mutex for thread-safe accumulation
  ///
  /// This overload is used when tiles are processed in parallel by multiple
  /// threads. The mutex protects the accumulator during blending operations.
  /// For non-blended strategies, the mutex parameter is ignored.
  ///
  /// @param op Tile operation metadata
  /// @param pixel_data Tile pixel data
  /// @param tile_width Tile width
  /// @param tile_height Tile height
  /// @param tile_channels Number of channels
  /// @param accumulator_mutex Mutex for thread-safe accumulation
  [[nodiscard]] absl::Status WriteTile(const core::TileReadOp& op,
                                       std::span<const uint8_t> pixel_data,
                                       uint32_t tile_width,
                                       uint32_t tile_height,
                                       uint32_t tile_channels,
                                       absl::Mutex& accumulator_mutex);

  /// @brief Fill entire canvas with solid color (for empty plans)
  ///
  /// This method should ONLY be called by readers when handling empty plans
  /// (no tiles to read). It's not used during normal tile writing operations.
  ///
  /// @param r Red value (0-255)
  /// @param g Green value (0-255)
  /// @param b Blue value (0-255)
  [[nodiscard]] absl::Status FillWithColor(uint8_t r, uint8_t g, uint8_t b);

  [[nodiscard]] absl::Status Finalize();

  [[nodiscard]] ImageDimensions GetDimensions() const;

  [[nodiscard]] uint32_t GetChannels() const;

  /// @brief Get output image after finalization
  ///
  /// Returns the final Image with appropriate format and channel count.
  /// Automatically optimized for RGB (3-channel) or multi-channel data.
  ///
  /// @return StatusOr containing final Image result
  [[nodiscard]] absl::StatusOr<OutputImage> GetOutput();

  /// @brief Check if writer uses blended composition
  [[nodiscard]] bool IsBlendingEnabled() const;

  /// @brief Get the internal strategy type (for debugging/testing)
  [[nodiscard]] std::string GetStrategyName() const;

 private:
  /// @brief Create appropriate strategy based on configuration
  std::unique_ptr<ITileWriterStrategy> CreateStrategy(const Config& config);

  /// @brief Analyze plan to determine optimal configuration
  Config AnalyzePlan(const core::TilePlan& plan);

  std::unique_ptr<ITileWriterStrategy> strategy_;
  Config config_;
};

}  // namespace runtime
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_H_
