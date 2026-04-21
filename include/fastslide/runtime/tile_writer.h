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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/core/tile_plan.h"
#include "fastslide/image.h"

/// @file tile_writer.h
/// @brief Canvas compositing surface for FastSlide.
///
/// Provides a single compositing interface that supports:
/// - Blended (weighted / sub-pixel) painting for MRXS-style fractional tiles
/// - Direct painting for SVS, QPTIFF, and other aligned tile formats
/// - Arbitrary channel counts (1, 3, 4, N) and data types (UInt8, UInt16, ...)

namespace fastslide {
namespace runtime {

/// @brief Compositing canvas that paints decoded tiles onto an output image.
///
/// Handles RGB8 with sub-pixel bilinear blending and coverage tracking,
/// as well as direct memcpy paths for grayscale, RGBA, spectral, planar,
/// and non-UInt8 data types.
///
/// ## Usage Examples:
///
/// ### Simple Usage:
/// ```cpp
/// Canvas canvas(plan);
/// auto status = reader.ExecutePlan(plan, canvas);
/// canvas.Finalize();
/// auto result = canvas.GetOutput();
/// ```
///
/// ### Manual Configuration:
/// ```cpp
/// Canvas::Config config;
/// config.dimensions = {1024, 1024};
/// config.channels = 5;
/// config.data_type = DataType::kUInt16;
/// Canvas canvas(config);
/// ```
class Canvas {
 public:
  /// @brief Background color specification (multi-channel).
  struct BackgroundColor {
    std::vector<double> values;

    BackgroundColor() : values{255.0} {}

    explicit BackgroundColor(uint8_t gray)
        : values{static_cast<double>(gray)} {}

    BackgroundColor(uint8_t r, uint8_t g, uint8_t b)
        : values{static_cast<double>(r), static_cast<double>(g),
                 static_cast<double>(b)} {}

    explicit BackgroundColor(std::vector<double> vals)
        : values(std::move(vals)) {}
  };

  /// @brief Configuration for manual canvas creation.
  struct Config {
    ImageDimensions dimensions;
    uint32_t channels = 3;
    DataType data_type = DataType::kUInt8;
    PlanarConfig planar_config = PlanarConfig::kContiguous;
    BackgroundColor background;
    bool enable_blending = false;
    /// @brief When true, the output `Image` is constructed with
    ///        `ImageFormat::kSpectral` regardless of channel count.
    ///        See `core::OutputSpec::force_spectral_image` for the
    ///        plan-builder hint that drives this.
    bool force_spectral_image = false;
  };

  /// @brief Create canvas from TilePlan (recommended).
  explicit Canvas(const core::TilePlan& plan);

  /// @brief Create canvas with manual configuration.
  explicit Canvas(const Config& config);

  /// @brief Convenience constructor for RGB output.
  Canvas(ImageDimensions dimensions,
         BackgroundColor background = BackgroundColor(255, 255, 255),
         bool enable_blending = false);

  /// @brief Paint a decoded tile onto the canvas.
  [[nodiscard]] aifocore::Status PaintTile(const core::TileReadOp& op,
                                           std::span<const uint8_t> tile_data,
                                           uint32_t tile_width,
                                           uint32_t tile_height,
                                           uint32_t tile_channels);

  /// @brief Paint a decoded tile under an explicit mutex.
  [[nodiscard]] aifocore::Status PaintTile(const core::TileReadOp& op,
                                           std::span<const uint8_t> tile_data,
                                           uint32_t tile_width,
                                           uint32_t tile_height,
                                           uint32_t tile_channels,
                                           std::mutex& accumulator_mutex);

  /// @brief Fill entire canvas with background color (for empty plans).
  [[nodiscard]] aifocore::Status FillBackground(uint8_t r, uint8_t g,
                                                uint8_t b);

  [[nodiscard]] aifocore::Status Finalize();

  [[nodiscard]] ImageDimensions GetDimensions() const;

  [[nodiscard]] uint32_t GetChannels() const;

  /// @brief Get output image after finalization.
  [[nodiscard]] aifocore::Result<Image> GetOutput();

  /// @brief Check if canvas uses blended composition.
  [[nodiscard]] bool IsBlendingEnabled() const;

 private:
  void InitOutputImage();
  Config AnalyzePlan(const core::TilePlan& plan);

  // -- Paint internals ------------------------------------------------------

  aifocore::Status PaintTileLocked(const core::TileReadOp& op,
                                   std::span<const uint8_t> pixel_data,
                                   uint32_t tile_width, uint32_t tile_height,
                                   uint32_t tile_channels);

  aifocore::Status PaintTileRgb8Blended(const core::TileReadOp& op,
                                        std::span<const uint8_t> pixel_data,
                                        uint32_t tile_width,
                                        uint32_t tile_height,
                                        uint32_t tile_channels);

  /// @brief Integer-position copy with coverage tracking for 16-bit RGB.
  ///
  /// Selected when the Canvas is configured for 3-channel kContiguous RGB
  /// at 16 bits per sample. No bilinear sampling and no sRGB gain are
  /// applied -- fluorescence intensities live in linear space and we
  /// already drop the BlendMetadata in the plan builder for 16-bit slides.
  aifocore::Status PaintTileRgb16Copy(const core::TileReadOp& op,
                                      std::span<const uint8_t> pixel_data,
                                      uint32_t tile_width, uint32_t tile_height,
                                      uint32_t tile_channels);

  /// @brief Templated integer-position RGB blit with coverage tracking.
  ///
  /// Used by both the 8-bit RGB brightfield blender (after gain has been
  /// applied to the source pixels) and the new 16-bit fluorescence copy
  /// path. The coverage map remains a single byte per pixel.
  template <typename PixelT>
  void RgbBlitT(const PixelT* src, int src_w, int src_h, int dest_x,
                int dest_y);

  template <typename PixelT>
  void RgbBlitOffsetT(const PixelT* src, int src_w, int src_h, int src_off_x,
                      int src_off_y, int blit_w, int blit_h, int dest_x,
                      int dest_y);

  void BilinearRgbBlit(const uint8_t* src, int src_w, int src_h, double dest_x,
                       double dest_y, double src_offset_x, double src_offset_y,
                       int visible_w, int visible_h);

  aifocore::Status PaintTilePlanar(const core::TileReadOp& op,
                                   std::span<const uint8_t> pixel_data,
                                   uint32_t tile_width, uint32_t tile_height);

  aifocore::Status PaintTileInterleaved(const core::TileReadOp& op,
                                        std::span<const uint8_t> pixel_data,
                                        uint32_t tile_width,
                                        uint32_t tile_height,
                                        uint32_t tile_channels);

  // -- State ----------------------------------------------------------------

  Config config_;
  std::unique_ptr<Image> output_image_;
  std::vector<uint8_t> coverage_;  ///< Used by the RGB8/RGB16 copy blenders.
  int out_w_ = 0;
  int out_h_ = 0;
  bool use_rgb8_blending_ = false;  ///< 3ch UInt8 contiguous (full bilinear).
  bool use_rgb16_copy_blending_ = false;  ///< 3ch UInt16 contiguous (copy).
  bool finalized_ = false;
};

}  // namespace runtime
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_TILE_WRITER_H_
