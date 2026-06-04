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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_SCENE_IMAGE_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_SCENE_IMAGE_H_

/// @file czi_scene_image.h
/// @brief One navigable CZI scene exposed as a `SlideImage`.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/readers/czi/czi_spatial_index.h"
#include "fastslide/slide_image.h"

namespace fastslide {

class CziReader;

/// @brief A single CZI scene (the "S" dimension) as an independent pyramid.
///
/// Each scene owns its own pyramid-level list, content bounds and lazily-built
/// per-level spatial index. Tile pixel data is shared: the owning `CziReader`
/// holds the one subblock array and tile cache, and this image references
/// subblocks by their global index. Scene-local coordinates are produced by
/// subtracting the scene's top-left origin when the spatial index is built, so
/// the shared subblock records keep their absolute file coordinates.
class CziSceneImage final : public SlideImage {
 public:
  /// @param container Owning reader (supplies filename, subblocks and cache);
  ///   must outlive this image.
  /// @param scene_id CZI scene index ("S" dimension start).
  /// @param name Human-readable image name.
  /// @param subblock_indices Indices into the container's subblock array that
  ///   belong to this scene.
  /// @param mpp_x Microns per pixel in X (0 if unknown).
  /// @param mpp_y Microns per pixel in Y (0 if unknown).
  /// @param objective_magnification Objective power (0 if unknown).
  /// @param scanner_model Scanner / vendor string.
  /// @param z_spacing_um Focal-plane step in microns (nullopt if unknown).
  /// @param t_interval_s Time-point step in seconds (nullopt if unknown).
  CziSceneImage(const CziReader& container, int32_t scene_id, std::string name,
                std::vector<uint32_t> subblock_indices, double mpp_x,
                double mpp_y, double objective_magnification,
                std::string scanner_model,
                std::optional<double> z_spacing_um = std::nullopt,
                std::optional<double> t_interval_s = std::nullopt);

  [[nodiscard]] std::string GetName() const override { return name_; }

  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;

  [[nodiscard]] const SlideProperties& GetProperties() const override {
    return properties_;
  }

  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override { return data_type_; }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  [[nodiscard]] StackInfo GetStackInfo() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& canvas) const override;

  /// @brief Scene index ("S" dimension start) backing this image.
  [[nodiscard]] int32_t GetSceneId() const { return scene_id_; }

 private:
  /// @brief Get (or lazily build) the spatial index for one plane of `level`.
  ///
  /// `z`/`t` are zero-based selectors into `z_values_`/`t_values_`; the index
  /// covers only the subblocks of that focal plane and time point, so a
  /// multi-plane level never composites planes together.
  [[nodiscard]] aifocore::Result<std::shared_ptr<czi::CziSpatialIndex>>
  GetSpatialIndex(int level, uint32_t z, uint32_t t) const;

  const CziReader& container_;
  int32_t scene_id_;
  std::string name_;
  std::vector<uint32_t> subblock_indices_;

  // Sorted unique downsample factors; the vector index is the pyramid level.
  std::vector<int32_t> downsamples_;
  // Per-level lists of global subblock indices belonging to this scene.
  std::vector<std::vector<uint32_t>> level_subblocks_;

  // Sorted unique CZI "Z"/"T" dimension starts present in this scene. A plane
  // selector indexes these; absent axes collapse to a single value {0}.
  std::vector<int32_t> z_values_;
  std::vector<int32_t> t_values_;
  std::optional<double> z_spacing_um_;
  std::optional<double> t_interval_s_;

  // Scene top-left origin in absolute level-0 coordinates. Subtracted to make
  // the scene's own pyramid start at (0, 0).
  int32_t origin_x_ = 0;
  int32_t origin_y_ = 0;

  // Full level-0 size of this scene (content extent), in pixels.
  ImageDimensions base_size_{0, 0};

  // Native output sample type: kUInt16 for 16-bit pixel types (GRAY16/BGR48),
  // kUInt8 otherwise. CZI 16-bit data is emitted at full depth, not rescaled.
  DataType data_type_ = DataType::kUInt8;

  SlideProperties properties_{};

  // Lazily built spatial indices, keyed by {level, z_idx, t_idx} (thread-safe).
  mutable std::mutex spatial_index_mutex_;
  mutable std::map<std::array<uint32_t, 3>,
                   std::shared_ptr<czi::CziSpatialIndex>>
      spatial_indices_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_CZI_CZI_SCENE_IMAGE_H_
