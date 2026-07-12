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

#include "fastslide/readers/czi/czi_format_plugin.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "aifocore/status/result.h"
#include "fastslide/readers/czi/czi.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

namespace fastslide {
namespace formats {
namespace czi {

namespace {

aifocore::Result<std::unique_ptr<SlideReader>> CreateCziReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  AIFOCORE_ASSIGN_OR_RETURN(auto reader, CziReader::Create(filename));
  if (cache) {
    reader->SetCache(cache);
  }
  return std::unique_ptr<SlideReader>(std::move(reader));
}

}  // namespace

FormatDescriptor CreateCziFormatDescriptor() {
  FormatDescriptor desc;
  desc.primary_extension = ".czi";
  desc.format_name = "CZI";
  desc.version = "1.0.0";

  // Factory function
  desc.factory = CreateCziReader;
  return desc;
}

}  // namespace czi
}  // namespace formats
}  // namespace fastslide
