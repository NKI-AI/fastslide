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

#include "fastslide/readers/generictiff/generictiff_format_plugin.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/generictiff/generictiff.h"
#include "fastslide/readers/philipstiff/philipstiff.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide {
namespace formats {
namespace generictiff {

namespace {

[[nodiscard]] bool IsFastSlideDebugEnabled() {
  static bool checked = false;
  static bool enabled = false;
  if (!checked) {
    const char* env = std::getenv("FASTSLIDE_DEBUG");
    enabled = (env != nullptr && std::strcmp(env, "1") == 0);
    checked = true;
  }
  return enabled;
}

bool IsPhilipsTiff(std::string_view filename, std::string* software_out) {
  simpletiff::TiffIndex index;
  int fd_val = -1;
  if (!simpletiff::OpenTiff(std::string(filename), index, fd_val)) {
    return false;
  }
  if (index.NumPages() == 0) {
    return false;
  }
  const std::string_view software = index.Page(0).software;
  if (software_out != nullptr) {
    software_out->assign(software.begin(), software.end());
  }
  return software.rfind("Philips", 0) == 0;
}

/// @brief Factory function for GenericTIFF readers
aifocore::Result<std::unique_ptr<SlideReader>> CreateGenericTiffReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  std::string software;
  const bool is_philips = IsPhilipsTiff(filename, &software);
  if (IsFastSlideDebugEnabled()) {
    std::cerr << "[TIFF] Open '" << filename << "' Software='" << software
              << "' -> " << (is_philips ? "PhilipsTIFF" : "GenericTIFF")
              << "\n";
  }

  if (is_philips) {
    AIFOCORE_ASSIGN_OR_RETURN(auto reader,
                              PhilipsTiffReader::Create(std::string(filename)));
    if (cache) {
      reader->SetCache(cache);
    }
    return std::unique_ptr<SlideReader>(std::move(reader));
  }

  AIFOCORE_ASSIGN_OR_RETURN(auto reader,
                            GenericTiffReader::Create(std::string(filename)));
  if (cache) {
    reader->SetCache(cache);
  }
  return std::unique_ptr<SlideReader>(std::move(reader));
}

}  // namespace

FormatDescriptor CreateGenericTiffFormatDescriptor() {
  FormatDescriptor desc;
  desc.primary_extension = ".tif";
  desc.aliases = {".tiff"};
  desc.format_name = "GenericTIFF";
  desc.version = "1.0.0";

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

  desc.required_capabilities.push_back("jpeg");

  desc.factory = CreateGenericTiffReader;
  return desc;
}

}  // namespace generictiff
}  // namespace formats
}  // namespace fastslide
