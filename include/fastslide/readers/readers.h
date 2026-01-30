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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READERS_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READERS_H_

#include <cstddef>
#include <vector>

#include "fastslide/readers/czi/czi_format_plugin.h"
#include "fastslide/readers/isyntax/isyntax.h"
#include "fastslide/readers/mrxs/mrxs_format_plugin.h"
#include "fastslide/readers/ometiff/ometiff_format_plugin.h"
#include "fastslide/readers/qptiff/qptiff_format_plugin.h"

#include "fastslide/readers/aperio/aperio_format_plugin.h"  // SVS format not implemented yet
#include "fastslide/readers/generictiff/generictiff_format_plugin.h"
#include "fastslide/readers/ndpitiff/ndpitiff_format_plugin.h"

/**
 * @file readers.h
 * @brief Convenience header for all format plugins
 *
 * This header includes all built-in format plugins, providing a single
 * include point for applications that want to register all formats.
 */

namespace fastslide {
namespace readers {

/// @brief Get all built-in format descriptors
///
/// Returns a vector of all built-in format descriptors that can be
/// registered with the reader registry.
///
/// Example usage:
/// @code
/// auto descriptors = GetBuiltinFormats();
/// for (const auto& desc : descriptors) {
///   registry.RegisterFormat(desc);
/// }
/// @endcode
inline std::vector<FormatDescriptor> GetBuiltinFormats() {
  std::vector<FormatDescriptor> formats = {
      formats::czi::CreateCziFormatDescriptor(),
      formats::mrxs::CreateMrxsFormatDescriptor(),
      formats::ometiff::CreateOmetiffFormatDescriptor(),
      formats::qptiff::CreateQptiffFormatDescriptor(),
      formats::aperio::CreateAperioFormatDescriptor(),
      formats::generictiff::CreateGenericTiffFormatDescriptor(),
      formats::ndpitiff::CreateNdpiTiffFormatDescriptor(),
  };

  // iSyntax format
  FormatDescriptor isyntax_desc;
  isyntax_desc.format_name = "iSyntax";
  isyntax_desc.primary_extension = ".isyntax";
  isyntax_desc.version = "1.0.0";
  isyntax_desc.capabilities = SetCapability(0, FormatCapability::kTiled);
  isyntax_desc.factory = [](std::shared_ptr<ITileCache> cache,
                            std::string_view filename)
      -> aifocore::Result<std::unique_ptr<SlideReader>> {
    auto result = IsyntaxReader::Create(filename);
    if (!result.ok()) {
      return result.status();
    }
    auto reader = std::unique_ptr<SlideReader>(std::move(result.value()));
    reader->SetCache(cache);
    return reader;
  };
  formats.push_back(std::move(isyntax_desc));

  return formats;
}

}  // namespace readers
}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_READERS_READERS_H_
