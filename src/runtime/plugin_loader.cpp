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
#include <cstddef>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/readers/readers.h"
#include "fastslide/runtime/reader_registry.h"

namespace fastslide {
namespace runtime {

namespace {

/// @brief Parse semantic version string (major.minor.patch)
struct SemanticVersion {
  int major = 0;
  int minor = 0;
  int patch = 0;

  static absl::StatusOr<SemanticVersion> Parse(std::string_view version_str) {
    std::regex version_regex(R"((\d+)\.(\d+)\.(\d+))");
    std::string version_string(version_str);
    std::smatch match;

    if (!std::regex_search(version_string, match, version_regex)) {
      return absl::InvalidArgumentError(
          aifocore::fmt::format("Invalid version string: {}", version_str));
    }

    SemanticVersion version;
    version.major = std::stoi(match[1].str());
    version.minor = std::stoi(match[2].str());
    version.patch = std::stoi(match[3].str());

    return version;
  }

  [[nodiscard]] bool operator>=(const SemanticVersion& other) const {
    if (major != other.major)
      return major >= other.major;
    if (minor != other.minor)
      return minor >= other.minor;
    return patch >= other.patch;
  }

  [[nodiscard]] bool operator<=(const SemanticVersion& other) const {
    if (major != other.major)
      return major <= other.major;
    if (minor != other.minor)
      return minor <= other.minor;
    return patch <= other.patch;
  }
};

}  // namespace

// ============================================================================
// VersionConstraint implementation
// ============================================================================

bool VersionConstraint::IsSatisfiedBy(std::string_view version) const {
  auto version_or = SemanticVersion::Parse(version);

  if (!version_or.ok()) {
    return false;
  }

  // Parse constraint
  if (constraint.empty()) {
    return true;  // No constraint means any version
  }

  // Simple ">=" constraint
  if (constraint.substr(0, 2) == ">=") {
    auto constraint_version_or = SemanticVersion::Parse(constraint.substr(2));
    if (!constraint_version_or.ok()) {
      return false;
    }
    return *version_or >= *constraint_version_or;
  }

  // Simple "=" constraint
  if (constraint[0] == '=') {
    auto constraint_version_or = SemanticVersion::Parse(constraint.substr(1));
    if (!constraint_version_or.ok()) {
      return false;
    }
    auto cv = *constraint_version_or;
    auto v = *version_or;
    return v.major == cv.major && v.minor == cv.minor && v.patch == cv.patch;
  }

  // No operator means exact match
  auto constraint_version_or = SemanticVersion::Parse(constraint);
  if (!constraint_version_or.ok()) {
    return false;
  }
  auto cv = *constraint_version_or;
  auto v = *version_or;
  return v.major == cv.major && v.minor == cv.minor && v.patch == cv.patch;
}

absl::StatusOr<VersionConstraint> VersionConstraint::Parse(
    std::string_view constraint_str) {
  VersionConstraint constraint;
  constraint.constraint = std::string(constraint_str);
  return constraint;
}

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

absl::Status BuiltInPluginsInitializer::RegisterAll(ReaderRegistry& registry) {
  auto descriptors = GetDescriptors();
  for (auto& descriptor : descriptors) {
    registry.RegisterFormat(std::move(descriptor));
  }
  return absl::OkStatus();
}

absl::Status BuiltInPluginsInitializer::RegisterAll(
    ReaderRegistry& registry, const PluginLoadContext& context) {
  auto descriptors = GetDescriptors(context);

  if (descriptors.empty()) {
    return absl::FailedPreconditionError(
        "No built-in formats can be loaded with available capabilities");
  }

  for (auto& descriptor : descriptors) {
    registry.RegisterFormat(std::move(descriptor));
  }

  return absl::OkStatus();
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

// ============================================================================
// Plugin loader implementations
// ============================================================================

}  // namespace runtime
}  // namespace fastslide
