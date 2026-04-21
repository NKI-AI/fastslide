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

#ifndef AIFO_FASTSLIDE_SRC_TESTING_MINIMAL_SLIDE_READER_H_
#define AIFO_FASTSLIDE_SRC_TESTING_MINIMAL_SLIDE_READER_H_

#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/image.h"
#include "fastslide/metadata.h"
#include "fastslide/slide_reader.h"

namespace fastslide::testing {

/// @brief Minimal concrete `SlideReader` for unit tests.
///
/// Provides safe defaults for every pure virtual on `SlideReader` so that
/// tests can derive and override only the methods relevant to the behavior
/// being exercised. Adding a new pure virtual to `SlideReader` only requires
/// adding a default here, instead of editing every test mock.
///
/// Defaults:
///   - 0 levels, all level/associated lookups return Unimplemented/NotFound.
///   - Empty channel and associated-image lists.
///   - 256x256 tile size, RGB / UInt8.
///   - Format name `"MinimalSlideReader"`.
class MinimalSlideReader : public SlideReader {
 public:
  MinimalSlideReader() = default;

  [[nodiscard]] int GetLevelCount() const override { return 0; }

  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int /*level*/) const override {
    return aifocore::Status(aifocore::StatusCode::kUnimplemented,
                            "MinimalSlideReader::GetLevelInfo");
  }

  [[nodiscard]] const SlideProperties& GetProperties() const override {
    static const SlideProperties props;
    return props;
  }

  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override {
    return {};
  }

  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override {
    return {};
  }

  [[nodiscard]] aifocore::Result<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view /*name*/) const override {
    return aifocore::Status(aifocore::StatusCode::kNotFound,
                            "MinimalSlideReader: no associated images");
  }

  [[nodiscard]] aifocore::Result<Image> ReadAssociatedImage(
      std::string_view /*name*/) const override {
    return aifocore::Status(aifocore::StatusCode::kNotFound,
                            "MinimalSlideReader: no associated images");
  }

  [[nodiscard]] Metadata GetMetadata() const override { return Metadata(); }

  [[nodiscard]] std::string GetFormatName() const override {
    return "MinimalSlideReader";
  }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override {
    return ImageDimensions{256, 256};
  }
};

}  // namespace fastslide::testing

#endif  // AIFO_FASTSLIDE_SRC_TESTING_MINIMAL_SLIDE_READER_H_
