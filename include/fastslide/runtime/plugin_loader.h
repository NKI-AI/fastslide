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

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "fastslide/runtime/format_descriptor.h"

/**
 * @file plugin_loader.h
 * @brief Plugin loading system with version constraints and feature detection
 * 
 * This header defines an enhanced plugin loading system that supports:
 * - Version constraint checking
 * - Optional feature detection (e.g., hardware codecs)
 * - Runtime plugin discovery
 * - Deployment-specific plugin filtering
 */

namespace fastslide {
namespace runtime {

// Forward declaration
class ReaderRegistry;

/// @brief Version constraint type
///
/// Specifies version requirements for a plugin. Uses semantic versioning.
/// Examples: ">=1.0.0", "^2.0", "~1.2.3"
struct VersionConstraint {
  /// @brief Constraint string (e.g., ">=1.0.0", "^2.0")
  std::string constraint;

  /// @brief Check if a version satisfies this constraint
  /// @param version Version string to check (e.g., "1.2.3")
  /// @return True if version satisfies constraint
  [[nodiscard]] bool IsSatisfiedBy(std::string_view version) const;

  /// @brief Parse a constraint string
  /// @param constraint_str Constraint string
  /// @return Parsed constraint or error
  [[nodiscard]] static absl::StatusOr<VersionConstraint> Parse(
      std::string_view constraint_str);
};

/// @brief Plugin capability requirements
///
/// Defines what capabilities must be available for a plugin to load.
/// This allows deployments to disable incompatible plugins without
/// recompilation.
struct PluginRequirements {
  /// @brief Minimum FastSlide version required
  VersionConstraint min_version;

  /// @brief Maximum FastSlide version supported (empty = no limit)
  std::string max_version;

  /// @brief Required hardware capabilities (e.g., "cuda", "opencl")
  std::vector<std::string> required_hardware;

  /// @brief Required codec capabilities (e.g., "jpeg2000", "jpegxr")
  std::vector<std::string> required_codecs;

  /// @brief Optional features (plugin works without, but with reduced functionality)
  std::vector<std::string> optional_features;
};

/// @brief Plugin loading context
///
/// Provides information about the current environment to plugin loaders.
/// This allows loaders to make informed decisions about which plugins
/// to load based on available resources and capabilities.
struct PluginLoadContext {
  /// @brief Available codec capabilities
  std::vector<std::string> available_codecs;

  /// @brief Available hardware capabilities
  std::vector<std::string> available_hardware;

  /// @brief FastSlide version string
  std::string fastslide_version;

  /// @brief Check if a capability is available
  /// @param capability Capability name
  /// @return True if available
  [[nodiscard]] bool HasCapability(std::string_view capability) const;

  /// @brief Create default context with auto-detected capabilities
  /// @return Default context
  static PluginLoadContext CreateDefault();
};

/// @brief Plugin loader interface (enhanced)
///
/// Enhanced plugin loader that understands version constraints and
/// optional features. Loaders can:
/// - Query available capabilities before loading
/// - Skip incompatible plugins gracefully
/// - Provide detailed error messages about missing dependencies
class PluginLoader {
 public:
  virtual ~PluginLoader() = default;

  /// @brief Get loader name for diagnostics
  /// @return Loader name
  [[nodiscard]] virtual std::string GetLoaderName() const = 0;

  /// @brief Load plugins with context information
  /// @param registry Registry to register plugins with
  /// @param context Loading context (available capabilities, version)
  /// @return Status indicating success or failure
  [[nodiscard]] virtual absl::Status LoadPlugins(
      ReaderRegistry& registry, const PluginLoadContext& context) = 0;

  /// @brief Check if loader can provide plugins in current context
  /// @param context Loading context
  /// @return True if loader has plugins available
  [[nodiscard]] virtual bool CanLoadInContext(
      const PluginLoadContext& context) const {
    return true;  // Default: always try
  }

  /// @brief Get plugin requirements (for pre-flight checks)
  /// @return Vector of plugin requirements this loader provides
  [[nodiscard]] virtual std::vector<PluginRequirements> GetPluginRequirements()
      const {
    return {};  // Default: no specific requirements
  }
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
  [[nodiscard]] static absl::Status RegisterAll(ReaderRegistry& registry);

  /// @brief Register built-in plugins with capability filtering
  /// @param registry Registry to register with
  /// @param context Loading context for filtering
  /// @return Status indicating success or any errors
  [[nodiscard]] static absl::Status RegisterAll(
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
using runtime::PluginLoader;
using runtime::PluginRequirements;
using runtime::VersionConstraint;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_PLUGIN_LOADER_H_
