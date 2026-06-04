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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SELF_IMAGE_VIEW_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SELF_IMAGE_VIEW_H_

#include <string>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/slide_image.h"

namespace fastslide {

class SlideReader;

/// @brief `SlideImage` adapter that forwards every call back to a
/// `SlideReader`.
///
/// This is what `SlideReader::GetImage(0)` returns by default: a thin
/// view that lets the existing single-image readers participate in the
/// multi-image API without changing any of their code. Multi-image
/// readers (e.g. Olympus VSI) implement their own `SlideImage` instead
/// and never use this adapter.
class SelfImageView final : public SlideImage {
 public:
  explicit SelfImageView(const SlideReader& reader) : reader_(reader) {}

  [[nodiscard]] std::string GetName() const override;
  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;
  [[nodiscard]] const SlideProperties& GetProperties() const override;
  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;
  [[nodiscard]] ImageFormat GetImageFormat() const override;
  [[nodiscard]] DataType GetDataType() const override;
  [[nodiscard]] ImageDimensions GetTileSize() const override;
  [[nodiscard]] StackInfo GetStackInfo() const override;

  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;
  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& canvas) const override;

 private:
  const SlideReader& reader_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_SELF_IMAGE_VIEW_H_
