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

#include "fastslide/readers/dicom/dicom_format_plugin.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>

#include "aifocore/status/result.h"
#include "fastslide/readers/dicom/dicom.h"
#include "fastslide/readers/dicom/dicom_magic.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

namespace fastslide {
namespace formats {
namespace dicom {

namespace {

aifocore::Result<std::unique_ptr<SlideReader>> CreateDicomReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  AIFOCORE_ASSIGN_OR_RETURN(auto reader, DicomReader::Create(filename));
  if (cache) {
    reader->SetCache(cache);
  }
  return std::unique_ptr<SlideReader>(std::move(reader));
}

bool MatchesDicomContent(std::string_view filename) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path path(filename);

  auto status = fs::status(path, ec);
  if (ec) {
    return false;
  }

  if (fs::is_regular_file(status)) {
    return ::fastslide::dicom::HasDicomMagic(path);
  }

  if (fs::is_directory(status)) {
    // Scan a small number of entries for a DICOM file. Limit the work so
    // that misidentifying a huge directory is cheap.
    constexpr std::size_t kMaxScanned = 64;
    std::size_t scanned = 0;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
      if (ec) {
        return false;
      }
      if (++scanned > kMaxScanned) {
        break;
      }
      if (!entry.is_regular_file(ec) || ec) {
        continue;
      }
      if (::fastslide::dicom::HasDicomMagic(entry.path())) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace

FormatDescriptor CreateDicomFormatDescriptor() {
  FormatDescriptor desc;
  desc.primary_extension = ".dcm";
  desc.format_name = "DICOM";
  desc.version = "1.0.0";

  desc.factory = CreateDicomReader;
  desc.matches_content = MatchesDicomContent;
  return desc;
}

}  // namespace dicom
}  // namespace formats
}  // namespace fastslide
