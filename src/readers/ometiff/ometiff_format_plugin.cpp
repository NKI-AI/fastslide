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

#include "fastslide/readers/ometiff/ometiff_format_plugin.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "aifocore/status/result.h"
#include "fastslide/readers/ometiff/ometiff.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

namespace fastslide::formats::ometiff {
namespace {

aifocore::Result<std::unique_ptr<SlideReader>> CreateOmetiffReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  AIFOCORE_ASSIGN_OR_RETURN(auto reader,
                            OmeTiffReader::Create(std::string(filename)));
  if (cache) {
    reader->SetCache(cache);
  }
  return std::unique_ptr<SlideReader>(std::move(reader));
}

}  // namespace

FormatDescriptor CreateOmetiffFormatDescriptor() {
  FormatDescriptor desc;
  desc.primary_extension = ".ome.tif";
  desc.aliases = {".ome.tiff"};
  desc.format_name = "OME-TIFF";
  desc.version = "1.0.0";

  desc.factory = CreateOmetiffReader;
  return desc;
}

}  // namespace fastslide::formats::ometiff
