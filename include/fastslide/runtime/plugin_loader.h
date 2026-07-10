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

#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/runtime/format_descriptor.h"

/**
 * @file plugin_loader.h
 * @brief Built-in plugin initialization.
 *
 * Defines BuiltInPluginsInitializer: a helper that registers all built-in
 * format plugins with a ReaderRegistry.
 */

namespace fastslide {
namespace runtime {

// Forward declaration
class ReaderRegistry;

/// @brief Built-in plugins initializer
///
/// Helper class to initialize built-in format plugins with proper
/// dependency injection and configuration.
class BuiltInPluginsInitializer {
 public:
  /// @brief Get all built-in format descriptors
  /// @return Vector of format descriptors for all built-in formats
  [[nodiscard]] static std::vector<FormatDescriptor> GetDescriptors();

  /// @brief Register built-in plugins with a registry
  /// @param registry Registry to register with
  /// @return Status indicating success or any errors
  [[nodiscard]] static aifocore::Status RegisterAll(ReaderRegistry& registry);
};

}  // namespace runtime

// Import into fastslide namespace
using runtime::BuiltInPluginsInitializer;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_PLUGIN_LOADER_H_
