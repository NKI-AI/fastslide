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

#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
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
absl::StatusOr<std::unique_ptr<SlideReader>> CreateMrxsReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  DECLARE_ASSIGN_OR_RETURN_MOVE(std::unique_ptr<MrxsReader>, reader,
                                MrxsReader::Create(std::string(filename)));

  // Apply cache if provided
  if (cache) {
    reader->SetITileCache(cache);
  }

  return reader;
}

}  // namespace

/// @brief Create a format descriptor for MRXS files
///
/// Constructs a FormatDescriptor that describes the MRXS format's capabilities,
/// requirements, and factory function. This descriptor is used by the reader
/// registry to identify and handle MRXS files.
///
/// Capabilities:
/// - Tiled: Images are stored as tiles
/// - Pyramidal: Multiple resolution levels
/// - AssociatedImages: Label, macro, thumbnail images
/// - Compressed: JPEG/PNG/BMP compression
/// - RandomAccess: Efficient random tile access
///
/// @return Complete FormatDescriptor for MRXS format
FormatDescriptor CreateMrxsFormatDescriptor() {
  FormatDescriptor desc;

  desc.primary_extension = ".mrxs";
  desc.format_name = "MRXS";
  desc.version = "1.0.0";

  // MRXS capabilities
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

  // Required capabilities (codecs, etc.)
  desc.required_capabilities.push_back("jpeg");

  // Factory function
  desc.factory = CreateMrxsReader;

  return desc;
}

}  // namespace mrxs
}  // namespace formats
}  // namespace fastslide
