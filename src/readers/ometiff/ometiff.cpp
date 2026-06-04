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

#include "fastslide/readers/ometiff/ometiff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/ometiff/ometiff_exec_context.h"
#include "fastslide/readers/ometiff/ometiff_metadata_loader.h"
#include "fastslide/readers/ometiff/ometiff_plan_builder.h"
#include "fastslide/readers/ometiff/ometiff_plan_context.h"
#include "fastslide/readers/ometiff/ometiff_tile_executor.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide {

aifocore::Result<std::unique_ptr<OmeTiffReader>> OmeTiffReader::Create(
    std::string_view filename) {
  return CreateImpl(filename);
}

OmeTiffReader::OmeTiffReader(std::string_view filename)
    : TiffBasedReader(std::string(filename)) {
  tiff_index_ = std::make_unique<simpletiff::TiffIndex>();
  int fd = -1;
  (void)simpletiff::OpenTiff(std::string(filename), *tiff_index_, fd);
  // Initialize metadata immediately (mirrors other TIFF-based readers).
  (void)ProcessMetadata();
}

int OmeTiffReader::GetLevelCount() const {
  return static_cast<int>(pyramid_.size());
}

aifocore::Result<LevelInfo> OmeTiffReader::GetLevelInfo(int level) const {
  if (level < 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "Level cannot be negative");
  }
  if (static_cast<size_t>(level) >= pyramid_.size()) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kNotFound,
        aifocore::fmt::format("Level {} not found", level));
  }

  const auto& pyr = pyramid_[level];
  LevelInfo info;
  info.dimensions = pyr.size;

  if (level == 0) {
    info.downsample_factor = 1.0;
  } else if (!pyramid_.empty()) {
    const double sx = static_cast<double>(pyramid_[0].size[0]) /
                      static_cast<double>(pyr.size[0]);
    const double sy = static_cast<double>(pyramid_[0].size[1]) /
                      static_cast<double>(pyr.size[1]);
    info.downsample_factor = (sx + sy) / 2.0;
  }
  return info;
}

const SlideProperties& OmeTiffReader::GetProperties() const {
  return properties_;
}

std::vector<ChannelMetadata> OmeTiffReader::GetChannelMetadata() const {
  std::vector<ChannelMetadata> out;
  out.reserve(channels_.size());
  for (const auto& ch : channels_) {
    ChannelMetadata md;
    md.name = ch.name;
    md.biomarker = ch.biomarker;
    md.color = ch.color;
    md.exposure_time = 0;
    md.signal_units = 0;
    out.push_back(std::move(md));
  }
  return out;
}

std::vector<std::string> OmeTiffReader::GetAssociatedImageNames() const {
  std::vector<std::string> names;
  names.reserve(associated_images_.size());
  for (const auto& [name, page] : associated_images_) {
    (void)page;
    names.push_back(name);
  }
  return names;
}

aifocore::Result<ImageDimensions> OmeTiffReader::GetAssociatedImageDimensions(
    std::string_view name) const {
  auto it = associated_images_.find(std::string(name));
  if (it == associated_images_.end()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                                "Associated image not found");
  }
  const uint32_t page = it->second;
  if (!tiff_index_ || page >= tiff_index_->NumPages()) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInternal,
                                "Invalid associated image page");
  }
  const auto& ph = tiff_index_->Page(page);
  return ImageDimensions{ph.width, ph.height};
}

aifocore::Result<RGBImage> OmeTiffReader::ReadAssociatedImage(
    std::string_view name) const {
  (void)name;
  return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kNotFound,
                              "OME-TIFF associated images not implemented");
}

Metadata OmeTiffReader::GetMetadata() const {
  Metadata md;
  md[std::string(MetadataKeys::kFormat)] = std::string("OME-TIFF");
  md[std::string(MetadataKeys::kLevels)] = pyramid_.size();
  md[std::string(MetadataKeys::kChannels)] = channels_.size();
  md[std::string(MetadataKeys::kMppX)] = metadata_.mpp_x;
  md[std::string(MetadataKeys::kMppY)] = metadata_.mpp_y;
  md["ometiff.z-count"] = static_cast<size_t>(metadata_.z_count);
  md["ometiff.t-count"] = static_cast<size_t>(metadata_.t_count);
  if (metadata_.z_spacing_um.has_value()) {
    md["ometiff.z-spacing-um"] = *metadata_.z_spacing_um;
  }
  if (metadata_.t_interval_s.has_value()) {
    md["ometiff.t-interval-s"] = *metadata_.t_interval_s;
  }
  return md;
}

ImageDimensions OmeTiffReader::GetTileSize() const {
  if (pyramid_.empty() || pyramid_[0].pages.empty() || !tiff_index_) {
    return ImageDimensions{512, 512};
  }
  const uint32_t page = pyramid_[0].pages[0];
  if (page >= tiff_index_->NumPages()) {
    return ImageDimensions{512, 512};
  }
  const auto& ph = tiff_index_->Page(page);
  if (ph.storage == simpletiff::Storage::kTiles) {
    const auto& tiles = tiff_index_->Tiles(ph.payload_id);
    return ImageDimensions{tiles.tile_w, tiles.tile_h};
  }
  return ImageDimensions{512, 512};
}

StackInfo OmeTiffReader::GetStackInfo() const {
  StackInfo info;
  info.z_count = metadata_.z_count;
  info.t_count = metadata_.t_count;
  info.z_spacing_um = metadata_.z_spacing_um;
  info.t_interval_s = metadata_.t_interval_s;
  return info;
}

aifocore::Result<core::TilePlan> OmeTiffReader::PrepareRequest(
    const core::TileRequest& request) const {
  if (request.plane.z >= metadata_.z_count) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("OME-TIFF: z index {} out of range [0,{})",
                              request.plane.z, metadata_.z_count));
  }
  if (request.plane.t >= metadata_.t_count) {
    return AIFOCORE_MAKE_STATUS(
        aifocore::StatusCode::kInvalidArgument,
        aifocore::fmt::format("OME-TIFF: t index {} out of range [0,{})",
                              request.plane.t, metadata_.t_count));
  }

  // Build a per-request pyramid view whose channel pages point at the selected
  // (z, t) plane. For 2D files this is identical to `pyramid_`.
  std::vector<OmeTiffLevelInfo> plane_view = pyramid_;
  for (auto& level : plane_view) {
    const auto pages = level.PagesForPlane(request.plane.z, request.plane.t);
    if (!pages.empty()) {
      level.pages.assign(pages.begin(), pages.end());
    }
  }

  const OmetiffPlanContext context{
      .pyramid = plane_view,
      .output_planar_config = output_planar_config_,
      .tiff_index = *tiff_index_,
  };
  AIFOCORE_ASSIGN_OR_RETURN(core::TilePlan plan,
                            OmetiffPlanBuilder::BuildPlan(request, context));

  // Fluorescence OME-TIFFs with 3 or 4 channels share RGB(A)'s buffer layout
  // but carry independent fluorophores. Tag the output as spectral so the
  // Canvas (and downstream FFI/viewers) take the multi-channel path instead of
  // treating the planes as RGB; without this a 3-channel stack is mis-typed as
  // RGB and rejected by the fluorescence renderer.
  plan.output.force_spectral_image = (format_ == ImageFormat::kSpectral);
  return plan;
}

aifocore::Status OmeTiffReader::ExecutePlan(const core::TilePlan& plan,
                                            runtime::Canvas& writer) const {
  const OmetiffExecContext context(GetFilename(), pyramid_, *tiff_index_,
                                   GetCache());
  return OmetiffTileExecutor::ExecutePlan(plan, context, writer);
}

void OmeTiffReader::PopulateSlideProperties() {
  properties_.mpp[0] = metadata_.mpp_x;
  properties_.mpp[1] = metadata_.mpp_y;
  properties_.scanner_model = "OME-TIFF";
  auto level0_or = GetLevelInfo(0);
  if (level0_or.ok()) {
    properties_.bounds =
        SlideBounds(0, 0, level0_or->dimensions[0], level0_or->dimensions[1]);
  }
}

aifocore::Status OmeTiffReader::ProcessMetadata() {
  if (!tiff_index_ || tiff_index_->NumPages() == 0) {
    return AIFOCORE_MAKE_STATUS(aifocore::StatusCode::kInvalidArgument,
                                "OME-TIFF: failed to open TIFF");
  }

  AIFOCORE_RETURN_IF_ERROR(OmetiffMetadataLoader::LoadMetadata(
      *tiff_index_, metadata_, channels_, pyramid_, associated_images_));

  // Choose output format and planar config.
  // OME brightfield often encodes SizeC=3, Interleaved=true but stores a single
  // RGB page (SamplesPerPixel=3). In that case we treat the slide as RGB.
  if (!pyramid_.empty() && !pyramid_[0].pages.empty()) {
    const uint32_t page0 = pyramid_[0].pages[0];
    const auto& ph = tiff_index_->Page(page0);
    if (channels_.size() == 1 && ph.samples_per_pixel == 3) {
      format_ = ImageFormat::kRGB;
      output_planar_config_ = PlanarConfig::kContiguous;
    } else {
      format_ = ImageFormat::kSpectral;
      output_planar_config_ = PlanarConfig::kSeparate;
    }
    data_type_ = DataTypeFromBitsPerSample(ph.bits_per_sample);
  }

  PopulateSlideProperties();
  return aifocore::Status::OkStatus();
}

}  // namespace fastslide
