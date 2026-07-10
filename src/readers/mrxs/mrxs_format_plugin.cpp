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

#include "fastslide/readers/mrxs/mrxs_format_plugin.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/mrxs/mrxs.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

namespace fastslide {
namespace formats {
namespace mrxs {

namespace {

/// @brief Factory function for creating MRXS reader instances
///
/// Creates an MrxsReader instance from a filename. This function is used by
/// the format registry to instantiate readers for MRXS files.
///
/// @param cache Optional tile cache (nullptr = no caching)
/// @param filename Path to the .mrxs file
/// @return StatusOr containing unique pointer to SlideReader or error
aifocore::Result<std::unique_ptr<SlideReader>> CreateMrxsReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {

  AIFOCORE_ASSIGN_OR_RETURN(auto reader,
                            MrxsReader::Create(std::string(filename)));

  // Apply cache if provided
  if (cache) {
    reader->SetCache(cache);
  }

  return std::unique_ptr<SlideReader>(std::move(reader));
}

}  // namespace

/// @brief Create a format descriptor for MRXS files
///
/// Constructs a FormatDescriptor that describes the MRXS format's name,
/// extension, and factory function. This descriptor is used by the reader
/// registry to identify and handle MRXS files.
///
/// @return Complete FormatDescriptor for MRXS format
FormatDescriptor CreateMrxsFormatDescriptor() {
  FormatDescriptor desc;

  desc.primary_extension = ".mrxs";
  desc.format_name = "MRXS";
  desc.version = "1.0.0";

  // Factory function
  desc.factory = CreateMrxsReader;

  return desc;
}

}  // namespace mrxs
}  // namespace formats
}  // namespace fastslide
