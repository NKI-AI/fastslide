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

#include "fastslide/runtime/plugin_loader.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aifocore/status/result.h"
#include "fastslide/readers/readers.h"
#include "fastslide/runtime/reader_registry.h"

namespace fastslide {
namespace runtime {

// ============================================================================
// PluginLoadContext implementation
// ============================================================================

bool PluginLoadContext::HasCapability(std::string_view capability) const {
  // Check codecs
  if (std::ranges::find(available_codecs, capability) !=
      available_codecs.end()) {
    return true;
  }

  // Check hardware
  if (std::ranges::find(available_hardware, capability) !=
      available_hardware.end()) {
    return true;
  }

  return false;
}

PluginLoadContext PluginLoadContext::CreateDefault() {
  PluginLoadContext context;

  // Auto-detect available codecs
  // TODO(fastslide): Implement actual codec detection
  context.available_codecs = {"jpeg", "png"};

  // Auto-detect hardware capabilities
  // TODO(fastslide): Implement actual hardware detection
  context.available_hardware = {};

  // Set FastSlide version
  context.fastslide_version = "0.0.1";

  return context;
}

// ============================================================================
// BuiltInPluginsInitializer implementation
// ============================================================================

std::vector<FormatDescriptor> BuiltInPluginsInitializer::GetDescriptors() {
  return readers::GetBuiltinFormats();
}

std::vector<FormatDescriptor> BuiltInPluginsInitializer::GetDescriptors(
    const PluginLoadContext& context) {
  auto all_descriptors = GetDescriptors();
  std::vector<FormatDescriptor> filtered;

  for (auto& descriptor : all_descriptors) {
    // Check if all required capabilities are available
    bool can_load = true;
    for (const auto& required_cap : descriptor.required_capabilities) {
      if (!context.HasCapability(required_cap)) {
        can_load = false;
        break;
      }
    }

    if (can_load) {
      filtered.push_back(std::move(descriptor));
    }
  }

  return filtered;
}

aifocore::Status BuiltInPluginsInitializer::RegisterAll(
    ReaderRegistry& registry) {
  auto descriptors = GetDescriptors();
  for (auto& descriptor : descriptors) {
    registry.RegisterFormat(std::move(descriptor));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Status BuiltInPluginsInitializer::RegisterAll(
    ReaderRegistry& registry, const PluginLoadContext& context) {
  auto descriptors = GetDescriptors(context);

  if (descriptors.empty()) {
    return aifocore::Status(
        aifocore::StatusCode::kFailedPrecondition,
        "No built-in formats can be loaded with available capabilities");
  }

  for (auto& descriptor : descriptors) {
    registry.RegisterFormat(std::move(descriptor));
  }

  return aifocore::Status::OkStatus();
}

bool BuiltInPluginsInitializer::CanLoadFormat(
    std::string_view format_name, const PluginLoadContext& context) {
  auto descriptors = GetDescriptors();

  for (const auto& descriptor : descriptors) {
    if (descriptor.format_name == format_name) {
      // Check if all required capabilities are available
      for (const auto& required_cap : descriptor.required_capabilities) {
        if (!context.HasCapability(required_cap)) {
          return false;
        }
      }
      return true;
    }
  }

  return false;
}

}  // namespace runtime
}  // namespace fastslide
