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

#include "fastslide/readers/aperio/aperio_format_plugin.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/readers/aperio/aperio.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

namespace fastslide {
namespace formats {
namespace aperio {

namespace {

/// @brief Factory function for Aperio readers
absl::StatusOr<std::unique_ptr<SlideReader>> CreateAperioReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  // Create the Aperio reader (using existing implementation)
  DECLARE_ASSIGN_OR_RETURN_MOVE(std::unique_ptr<AperioReader>, reader,
                                AperioReader::Create(std::string(filename)));

  // Apply cache if provided
  if (cache) {
    reader->SetCache(cache);
  }

  return reader;
}

}  // namespace

FormatDescriptor CreateAperioFormatDescriptor() {
  FormatDescriptor desc;

  desc.primary_extension = ".svs";
  desc.format_name = "Aperio";
  desc.version = "1.0.0";

  // Aperio capabilities
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kTiled);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kPyramidal);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kAssociatedImages);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kCompressed);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kRandomAccess);

  // Required capabilities (codecs)
  desc.required_capabilities.push_back("jpeg");
  // Note: Aperio may also use JPEG2000, but it's optional depending on the file
  // For full compatibility, JPEG2000 support is recommended

  // Factory function
  desc.factory = CreateAperioReader;

  return desc;
}

}  // namespace aperio
}  // namespace formats
}  // namespace fastslide
