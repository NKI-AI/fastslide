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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_FORMAT_DESCRIPTOR_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_FORMAT_DESCRIPTOR_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/runtime/cache_interface.h"

/**
 * @file format_descriptor.h
 * @brief Format plugin descriptors
 *
 * This header defines the structures used to describe formats and create
 * reader instances through a plugin system. This provides a more structured
 * and extensible alternative to raw factory functions.
 */

namespace fastslide {

// Forward declarations
class SlideReader;

namespace runtime {

/// @brief Format descriptor
///
/// Describes a format plugin including its extension, name, and factory
/// function. This provides a structured way to register and query format
/// support.
struct FormatDescriptor {
  /// @brief Primary file extension (e.g., ".mrxs", ".svs")
  std::string primary_extension;

  /// @brief Alternative extensions (e.g., {".tif", ".tiff"})
  std::vector<std::string> aliases;

  /// @brief Human-readable format name (e.g., "MRXS", "SVS")
  std::string format_name;

  /// @brief Version string (e.g., "1.0.0")
  std::string version;

  /// @brief Factory function for creating reader instances
  ///
  /// Creates a new reader instance for the given file. The cache parameter
  /// provides optional tile caching support.
  std::function<aifocore::Result<std::unique_ptr<SlideReader>>(
      std::shared_ptr<ITileCache>, std::string_view filename)>
      factory;

  /// @brief Optional content-based matcher for extensionless inputs.
  ///
  /// Some formats (notably DICOM WSI exports) ship files without any
  /// extension. When the registry cannot resolve a file by extension it
  /// falls back to calling this matcher on every registered descriptor in
  /// turn; the first one to return @c true wins.
  ///
  /// Implementations should be cheap (typically a small magic-byte read) and
  /// must tolerate both regular files and directories. They should never
  /// throw — return @c false on any I/O error.
  std::function<bool(std::string_view filename)> matches_content;

  /// @brief Default constructor
  FormatDescriptor() = default;

  /// @brief Check if this descriptor handles a given extension
  /// @param extension File extension to check (with leading dot)
  /// @return True if this format handles the extension
  [[nodiscard]] bool HandlesExtension(std::string_view extension) const {
    if (extension == primary_extension) {
      return true;
    }
    for (const auto& alias : aliases) {
      if (extension == alias) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace runtime

// Import runtime types into fastslide namespace
using runtime::FormatDescriptor;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_FORMAT_DESCRIPTOR_H_
