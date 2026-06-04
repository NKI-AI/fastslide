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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_PLUGIN_LOADER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_PLUGIN_LOADER_H_

#include <string>
#include <string_view>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/runtime/format_descriptor.h"

/**
 * @file plugin_loader.h
 * @brief Built-in plugin initialization and capability-based filtering.
 *
 * Defines:
 * - PluginLoadContext: environment description (available codecs, hardware,
 *   FastSlide version) used to filter built-in formats.
 * - BuiltInPluginsInitializer: helper that registers all built-in format
 *   plugins with a ReaderRegistry, optionally filtered by a load context.
 */

namespace fastslide {
namespace runtime {

// Forward declaration
class ReaderRegistry;

/// @brief Plugin loading context
///
/// Describes the current environment so callers can filter which built-in
/// plugins should be loaded based on available codecs and hardware.
struct PluginLoadContext {
  /// @brief Available codec capabilities (e.g. "jpeg", "png", "jpeg2000")
  std::vector<std::string> available_codecs;

  /// @brief Available hardware capabilities (e.g. "cuda", "opencl")
  std::vector<std::string> available_hardware;

  /// @brief FastSlide version string
  std::string fastslide_version;

  /// @brief Check if a capability is available (codec or hardware).
  /// @param capability Capability name
  /// @return True if available
  [[nodiscard]] bool HasCapability(std::string_view capability) const;

  /// @brief Create default context with auto-detected capabilities.
  /// @return Default context
  static PluginLoadContext CreateDefault();
};

/// @brief Built-in plugins initializer
///
/// Helper class to initialize built-in format plugins with proper
/// dependency injection and configuration.
class BuiltInPluginsInitializer {
 public:
  /// @brief Get all built-in format descriptors
  /// @return Vector of format descriptors for all built-in formats
  [[nodiscard]] static std::vector<FormatDescriptor> GetDescriptors();

  /// @brief Get format descriptors filtered by available capabilities
  /// @param context Loading context with available capabilities
  /// @return Filtered vector of format descriptors
  [[nodiscard]] static std::vector<FormatDescriptor> GetDescriptors(
      const PluginLoadContext& context);

  /// @brief Register built-in plugins with a registry
  /// @param registry Registry to register with
  /// @return Status indicating success or any errors
  [[nodiscard]] static aifocore::Status RegisterAll(ReaderRegistry& registry);

  /// @brief Register built-in plugins with capability filtering
  /// @param registry Registry to register with
  /// @param context Loading context for filtering
  /// @return Status indicating success or any errors
  [[nodiscard]] static aifocore::Status RegisterAll(
      ReaderRegistry& registry, const PluginLoadContext& context);

  /// @brief Check if a format can be loaded in the current context
  /// @param format_name Format name (e.g., "MRXS", "SVS")
  /// @param context Loading context
  /// @return True if format can be loaded
  [[nodiscard]] static bool CanLoadFormat(std::string_view format_name,
                                          const PluginLoadContext& context);
};

}  // namespace runtime

// Import into fastslide namespace
using runtime::BuiltInPluginsInitializer;
using runtime::PluginLoadContext;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_PLUGIN_LOADER_H_
