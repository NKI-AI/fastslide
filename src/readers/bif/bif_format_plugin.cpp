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

#include "fastslide/readers/bif/bif_format_plugin.h"

#include <memory>
#include <string>
#include <string_view>

#include "aifocore/platform/portability.h"
#include "aifocore/status/result.h"
#include "fastslide/readers/bif/bif.h"
#include "fastslide/readers/bif/bif_xml.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide {
namespace formats {
namespace bif {

namespace {

aifocore::Result<std::unique_ptr<SlideReader>> CreateBifReader(
    std::shared_ptr<ITileCache> cache, std::string_view filename) {
  AIFOCORE_ASSIGN_OR_RETURN(auto reader, BifReader::Create(filename));
  if (cache) {
    reader->SetCache(cache);
  }
  return std::unique_ptr<SlideReader>(std::move(reader));
}

// Content matcher: a BIF file is a (Big)TIFF whose IFD 0 XMP packet carries the
// VENTANA `<iScan>` marker. Cheap and tolerant of any I/O failure.
bool MatchesBifContent(std::string_view filename) {
  simpletiff::TiffIndex index;
  int fd = -1;
  if (!simpletiff::OpenTiff(std::string(filename), index, fd)) {
    if (fd >= 0) {
      aifocore::portable_close(fd);
    }
    return false;
  }
  bool matched = false;
  if (index.NumPages() > 0) {
    matched = fastslide::bif::LooksLikeBif(index.Page(0).xmp_packet);
  }
  if (fd >= 0) {
    aifocore::portable_close(fd);
  }
  return matched;
}

}  // namespace

FormatDescriptor CreateBifFormatDescriptor() {
  FormatDescriptor desc;
  desc.primary_extension = ".bif";
  desc.format_name = "BIF";
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
  desc.factory = CreateBifReader;
  desc.matches_content = MatchesBifContent;
  return desc;
}

}  // namespace bif
}  // namespace formats
}  // namespace fastslide
