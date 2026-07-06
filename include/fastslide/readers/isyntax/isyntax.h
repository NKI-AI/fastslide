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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_H_

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/reader_factory.h"
#include "fastslide/slide_reader.h"

// Forward declarations for libisyntax types
typedef struct isyntax_t isyntax_t;
typedef struct isyntax_cache_t isyntax_cache_t;

namespace isyntax {
class IsyntaxFile;
}  // namespace isyntax

namespace fastslide {

/// @brief Reader for Philips iSyntax files using libisyntax
class IsyntaxReader : public SlideReader, public ReaderFactory<IsyntaxReader> {
 public:
  static aifocore::Result<std::unique_ptr<IsyntaxReader>> Create(
      std::string_view filename);

  ~IsyntaxReader() override;

  // SlideReader interface implementation
  [[nodiscard]] int GetLevelCount() const override;
  [[nodiscard]] aifocore::Result<LevelInfo> GetLevelInfo(
      int level) const override;
  [[nodiscard]] const SlideProperties& GetProperties() const override;
  [[nodiscard]] std::vector<ChannelMetadata> GetChannelMetadata()
      const override;
  [[nodiscard]] std::vector<std::string> GetAssociatedImageNames()
      const override;
  [[nodiscard]] aifocore::Result<ImageDimensions> GetAssociatedImageDimensions(
      std::string_view name) const override;
  [[nodiscard]] aifocore::Result<RGBImage> ReadAssociatedImage(
      std::string_view name) const override;

  [[nodiscard]] Metadata GetMetadata() const override;

  /// @brief Embedded DICOM ICC profile for the WSI image, if present.
  [[nodiscard]] aifocore::Result<std::vector<uint8_t>> GetIccProfile()
      const override;

  [[nodiscard]] std::string GetFormatName() const override { return "iSyntax"; }

  [[nodiscard]] ImageFormat GetImageFormat() const override {
    return ImageFormat::kRGB;
  }

  [[nodiscard]] DataType GetDataType() const override {
    return DataType::kUInt8;
  }

  [[nodiscard]] ImageDimensions GetTileSize() const override;

  // Two-stage pipeline implementation
  [[nodiscard]] aifocore::Result<core::TilePlan> PrepareRequest(
      const core::TileRequest& request) const override;

  [[nodiscard]] aifocore::Status ExecutePlan(
      const core::TilePlan& plan, runtime::Canvas& writer) const override;

  // Internal accessors
  const isyntax::IsyntaxFile& GetIsyntaxFile() const { return *isyntax_file_; }

  std::mutex& GetMutex() const { return mutex_; }

  std::string_view GetFilename() const { return filename_; }

 private:
  friend class ReaderFactory<IsyntaxReader>;

  explicit IsyntaxReader(std::string filename);

  static aifocore::Status ValidateInput(const std::filesystem::path& filename);
  static aifocore::Result<std::unique_ptr<IsyntaxReader>> CreateReaderImpl(
      const std::filesystem::path& filename);

  aifocore::Status Initialize();
  void PopulateSlideProperties();

  std::string filename_;
  std::unique_ptr<isyntax::IsyntaxFile> isyntax_file_;
  SlideProperties properties_;

  // Cache channel metadata
  std::vector<ChannelMetadata> channels_;

  // Mutex for thread safety if needed (libisyntax usage)
  mutable std::mutex mutex_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_ISYNTAX_ISYNTAX_H_
