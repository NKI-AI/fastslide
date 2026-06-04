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

#include "fastslide/self_image_view.h"

#include <string>
#include <vector>

#include "fastslide/slide_reader.h"

namespace fastslide {

std::string SelfImageView::GetName() const {
  // Default name for single-image readers; multi-image formats don't use
  // this adapter at all.
  return "image 0";
}

int SelfImageView::GetLevelCount() const {
  return reader_.GetLevelCount();
}

aifocore::Result<LevelInfo> SelfImageView::GetLevelInfo(int level) const {
  return reader_.GetLevelInfo(level);
}

const SlideProperties& SelfImageView::GetProperties() const {
  return reader_.GetProperties();
}

std::vector<ChannelMetadata> SelfImageView::GetChannelMetadata() const {
  return reader_.GetChannelMetadata();
}

ImageFormat SelfImageView::GetImageFormat() const {
  return reader_.GetImageFormat();
}

DataType SelfImageView::GetDataType() const {
  return reader_.GetDataType();
}

ImageDimensions SelfImageView::GetTileSize() const {
  return reader_.GetTileSize();
}

StackInfo SelfImageView::GetStackInfo() const {
  // This adapter carries no data of its own, and `SlideReader::GetStackInfo()`
  // delegates back to the primary image (i.e. this view) -- forwarding here
  // would recurse forever. A single-image reader exposed through the default
  // self-view has no Z/T stack, so report the empty default (matching
  // `SlideImage::GetStackInfo()`). Readers that genuinely expose a stack
  // provide their own `SlideImage` and never use this adapter.
  return {};
}

aifocore::Result<core::TilePlan> SelfImageView::PrepareRequest(
    const core::TileRequest& request) const {
  return reader_.PrepareRequest(request);
}

aifocore::Status SelfImageView::ExecutePlan(const core::TilePlan& plan,
                                            runtime::Canvas& canvas) const {
  return reader_.ExecutePlan(plan, canvas);
}

}  // namespace fastslide
