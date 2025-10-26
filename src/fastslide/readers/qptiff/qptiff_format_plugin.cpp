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

#include "fastslide/readers/qptiff/qptiff_format_plugin.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/readers/qptiff/qptiff.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

namespace fastslide {
namespace formats {
namespace qptiff {

namespace {

/// @brief Factory function for QPTIFF readers
absl::StatusOr<std::unique_ptr<SlideReader>> CreateQptiffReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  // Create the QPTIFF reader (using existing implementation)
  DECLARE_ASSIGN_OR_RETURN_MOVE(std::unique_ptr<QpTiffReader>, reader,
                                QpTiffReader::Create(std::string(filename)));

  // Apply cache if provided
  if (cache) {
    reader->SetCache(cache);
  }

  return reader;
}

}  // namespace

FormatDescriptor CreateQptiffFormatDescriptor() {
  FormatDescriptor desc;

  desc.primary_extension = ".qptiff";
  desc.format_name = "QPTIFF";
  desc.version = "1.0.0";

  // QPTIFF capabilities
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kTiled);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kPyramidal);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kSpectral);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kAssociatedImages);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kCompressed);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kRandomAccess);

  // Required capabilities (codecs)
  // QPTIFF can use various compressions, not just JPEG
  // Remove requirement so it always loads
  // desc.required_capabilities.push_back("jpeg");

  // Factory function
  desc.factory = CreateQptiffReader;

  return desc;
}

}  // namespace qptiff
}  // namespace formats
}  // namespace fastslide
