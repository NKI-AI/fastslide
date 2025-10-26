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

#include "absl/log/log.h"
#include "fastslide/runtime/plugin_loader.h"
#include "fastslide/runtime/reader_registry.h"
#include "fastslide/slide_reader.h"  // Complete type needed for unique_ptr

/**
 * @file register_builtin_formats.cpp
 * @brief Automatic registration of built-in format plugins
 * 
 * This file contains the logic to register all built-in format plugins
 * with the global registry. The registration happens automatically via
 * static initialization when the library is loaded.
 * 
 * The new system uses BuiltInPluginsInitializer which supports:
 * - Capability filtering (only load plugins with available codecs)
 * - Version constraints
 * - Optional feature detection
 */

namespace fastslide::runtime {

namespace {

/// @brief Helper class for automatic format registration
///
/// This class registers all built-in formats when the library is loaded.
/// It's instantiated as a static variable, ensuring registration happens
/// before main() is called.
class BuiltinFormatRegistrar {
 public:
  BuiltinFormatRegistrar() {
    auto& registry = GetGlobalRegistry();

    // Create default load context with auto-detected capabilities
    auto context = PluginLoadContext::CreateDefault();

    // Register all built-in formats with capability filtering
    auto status = BuiltInPluginsInitializer::RegisterAll(registry, context);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to register built-in formats: " << status.message();
    }
  }
};

// Static instance triggers registration at library load time
static BuiltinFormatRegistrar g_registrar;

}  // namespace

/// @brief Manually register built-in formats
///
/// This function can be called to explicitly register built-in formats
/// if automatic registration via static initialization is not desired.
/// It's safe to call multiple times (formats will be re-registered).
///
/// @param registry Registry to register formats with
void RegisterBuiltinFormats(ReaderRegistry& registry) {
  // Ignore return value - we're registering all formats unconditionally
  (void)BuiltInPluginsInitializer::RegisterAll(registry);
}

/// @brief Manually register built-in formats with capability filtering
///
/// This function registers only formats that can be loaded with the
/// available capabilities in the provided context.
///
/// @param registry Registry to register formats with
/// @param context Loading context with available capabilities
/// @return Status indicating success or failure
[[nodiscard]] absl::Status RegisterBuiltinFormats(
    ReaderRegistry& registry, const PluginLoadContext& context) {
  return BuiltInPluginsInitializer::RegisterAll(registry, context);
}

}  // namespace fastslide::runtime
