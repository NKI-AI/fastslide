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

#include "aifocore/status/result.h"
#include "fastslide/readers/readers.h"
#include "fastslide/runtime/plugin_loader.h"
#include "fastslide/runtime/reader_registry.h"

namespace fastslide {
namespace runtime {

// This registration runs at static initialization time to register
// built-in formats with the global registry.
//
// It uses BuiltInPluginsInitializer to handle version checking and
// capability detection.

namespace {

// Static registrar that runs on startup
struct BuiltInFormatRegistrar {
  BuiltInFormatRegistrar() {
    // Register all built-in formats with the global registry
    // Note: We ignore errors during static init as we can't report them
    // effectively. Applications should verify supported formats via
    // GetSupportedExtensions() if critical.

    auto& registry = GetGlobalRegistry();
    (void)BuiltInPluginsInitializer::RegisterAll(registry);
  }
};

// Instantiate registrar
static BuiltInFormatRegistrar g_builtin_registrar;

}  // namespace

}  // namespace runtime
}  // namespace fastslide
