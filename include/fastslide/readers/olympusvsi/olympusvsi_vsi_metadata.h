// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_VSI_METADATA_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_VSI_METADATA_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "fastslide/utilities/colors.h"

namespace fastslide::formats::olympusvsi {

/// @brief One acquisition channel described in the `.vsi` container.
///
/// Olympus stores the human-readable channel name (e.g. ``"FL DAPI"``)
/// and an optional display colour in the `.vsi` tag tree, *not* in the
/// per-stack `.ets` files. The colour is derived from the channel's
/// 256-entry display LUT (its brightest entry); it is absent when no LUT
/// precedes the name.
struct VsiChannelMeta {
  std::string name;               ///< e.g. "FL DAPI", "FL FITC".
  std::optional<ColorRGB> color;  ///< Display colour from the LUT endpoint.
};

/// @brief Per-image ("pyramid") metadata extracted from the `.vsi` tag
///        tree, in document order.
///
/// Mirrors the Olympus "image frame volume" blocks: a label, an overview,
/// then one entry per scanned region. Only the identifying name, the image
/// boundary (used to match the pyramid to a discovered `.ets` stack), and
/// the channels are surfaced; everything else (objective, exposures, ...)
/// is ignored.
struct VsiPyramidMeta {
  std::string name;     ///< Stack/layer name (may be empty).
  uint32_t width = 0;   ///< Image boundary width (px), 0 if absent.
  uint32_t height = 0;  ///< Image boundary height (px), 0 if absent.
  /// Pixel size (microns-per-pixel) from the micron-scale tag (2019). Each
  /// image frame carries its own scale, so the navigator/overview and the
  /// scanned regions can differ. ``0`` when the tag is absent.
  double mpp_x = 0.0;
  double mpp_y = 0.0;
  std::vector<VsiChannelMeta> channels;  ///< One per acquisition channel.
};

/// @brief Document-level metadata recovered from a `.vsi` container, in
///        addition to the per-image pyramid metadata.
struct VsiContainerMeta {
  /// Acquisition software / device name from the document-properties
  /// device-name field (tag 34), e.g. ``"OLYMPUS VS200 ASW"``. Empty when
  /// the field is absent. Carried once per container, not per image.
  std::string device_name;
  /// Per-image metadata in document order.
  std::vector<VsiPyramidMeta> pyramids;
};

/// @brief Walk the Olympus tag tree embedded in a `.vsi` container and
///        return its document-level and per-image metadata.
///
/// @param vsi_path Absolute path to the `.vsi` container file.
/// @return Container metadata (device name + pyramids in document order);
///         empty on any failure.
[[nodiscard]] VsiContainerMeta ParseVsiMetadata(
    const std::filesystem::path& vsi_path);

}  // namespace fastslide::formats::olympusvsi

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_OLYMPUSVSI_OLYMPUSVSI_VSI_METADATA_H_
