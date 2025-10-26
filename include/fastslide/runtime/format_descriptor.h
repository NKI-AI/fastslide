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
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "fastslide/runtime/cache_interface.h"

/**
 * @file format_descriptor.h
 * @brief Format plugin descriptors and capability negotiation
 * 
 * This header defines the structures used to describe format capabilities
 * and create reader instances through a plugin system. This provides a
 * more structured and extensible alternative to raw factory functions.
 */

namespace fastslide {

// Forward declarations
class SlideReader;

namespace runtime {

/// @brief Format capability flags
///
/// Describes the capabilities supported by a format plugin. These can be
/// queried by applications to determine which features are available.
enum class FormatCapability : uint32_t {
  kTiled = 1 << 0,      ///< Format uses tiled storage
  kPyramidal = 1 << 1,  ///< Format supports multi-resolution pyramids
  kSpectral = 1 << 2,   ///< Format supports spectral/multichannel imaging
  kAssociatedImages = 1 << 3,  ///< Format supports associated images
  kLabelLayers = 1 << 4,       ///< Format supports label/annotation layers
  kCompressed = 1 << 5,        ///< Format uses compression
  kRandomAccess = 1 << 6,      ///< Format supports efficient random tile access
  kStreaming = 1 << 7,         ///< Format supports streaming access
};

/// @brief Capability flags container
using CapabilityFlags = uint32_t;

/// @brief Check if capability is set
inline bool HasCapability(CapabilityFlags flags, FormatCapability cap) {
  return (flags & static_cast<uint32_t>(cap)) != 0;
}

/// @brief Set a capability flag
inline CapabilityFlags SetCapability(CapabilityFlags flags,
                                     FormatCapability cap) {
  return flags | static_cast<uint32_t>(cap);
}

/// @brief Clear a capability flag
inline CapabilityFlags ClearCapability(CapabilityFlags flags,
                                       FormatCapability cap) {
  return flags & ~static_cast<uint32_t>(cap);
}

/// @brief Format descriptor
///
/// Describes a format plugin including its extension, name, capabilities,
/// and factory function. This provides a structured way to register and
/// query format support.
struct FormatDescriptor {
  /// @brief Primary file extension (e.g., ".mrxs", ".svs")
  std::string primary_extension;

  /// @brief Alternative extensions (e.g., {".tif", ".tiff"})
  std::vector<std::string> aliases;

  /// @brief Human-readable format name (e.g., "MRXS", "SVS")
  std::string format_name;

  /// @brief Format capabilities
  CapabilityFlags capabilities;

  /// @brief Version string (e.g., "1.0.0")
  std::string version;

  /// @brief Required external capabilities (e.g., "jpeg2000", "jpegxr")
  ///
  /// Lists external dependencies that must be available for this format
  /// to work. The registry can use this to filter formats based on
  /// available codecs.
  std::vector<std::string> required_capabilities;

  /// @brief Factory function for creating reader instances
  ///
  /// Creates a new reader instance for the given file. The cache parameter
  /// provides optional tile caching support.
  std::function<absl::StatusOr<std::unique_ptr<SlideReader>>(
      std::shared_ptr<ITileCache>, std::string_view filename)>
      factory;

  /// @brief Default constructor
  FormatDescriptor() : capabilities(0) {}

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

  /// @brief Check if format has a specific capability
  /// @param capability Capability to check
  /// @return True if capability is supported
  [[nodiscard]] bool HasCapability(FormatCapability capability) const {
    return runtime::HasCapability(capabilities, capability);
  }
};

}  // namespace runtime

// Import runtime types into fastslide namespace
using runtime::CapabilityFlags;
using runtime::ClearCapability;
using runtime::FormatCapability;
using runtime::FormatDescriptor;
using runtime::HasCapability;
using runtime::SetCapability;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_FORMAT_DESCRIPTOR_H_
