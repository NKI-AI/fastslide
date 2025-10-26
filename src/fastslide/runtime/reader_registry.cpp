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

#include "fastslide/runtime/reader_registry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "aifocore/status/status_macros.h"
#include "aifocore/utilities/fmt.h"
#include "fastslide/slide_reader.h"

namespace fastslide {
namespace runtime {

std::string ReaderRegistry::NormalizeExtension(std::string_view extension) {
  std::string result(extension);

  // Convert to lowercase
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  // Ensure leading dot
  if (!result.empty() && result[0] != '.') {
    result = "." + result;
  }

  return result;
}

void ReaderRegistry::RegisterFormat(FormatDescriptor descriptor) {
  absl::MutexLock lock(&mutex_);

  std::string normalized = NormalizeExtension(descriptor.primary_extension);
  formats_[normalized] = std::move(descriptor);

  // Also register aliases
  auto& desc = formats_[normalized];
  for (const auto& alias : desc.aliases) {
    std::string normalized_alias = NormalizeExtension(alias);
    formats_[normalized_alias] = desc;  // Copy descriptor for alias
  }
}

void ReaderRegistry::RegisterFormats(
    std::vector<FormatDescriptor> descriptors) {
  for (auto& desc : descriptors) {
    RegisterFormat(std::move(desc));
  }
}

absl::StatusOr<std::unique_ptr<SlideReader>> ReaderRegistry::CreateReader(
    std::string_view filename, std::shared_ptr<ITileCache> cache) const {
  // Extract extension
  std::filesystem::path path(filename);
  std::string extension = path.extension().string();

  if (extension.empty()) {
    return absl::InvalidArgumentError(
        aifocore::fmt::format("File has no extension: {}", filename));
  }

  // Normalize and find format
  std::string normalized = NormalizeExtension(extension);

  absl::ReaderMutexLock lock(&mutex_);

  auto it = formats_.find(normalized);
  if (it == formats_.end()) {
    return absl::NotFoundError(aifocore::fmt::format(
        "No reader registered for extension: {}", extension));
  }

  // Create reader using factory
  const auto& descriptor = it->second;
  if (!descriptor.factory) {
    return absl::InternalError(aifocore::fmt::format(
        "Format descriptor for {} has no factory function", extension));
  }

  return descriptor.factory(cache, filename);
}

std::vector<std::string> ReaderRegistry::ListFormats() const {
  absl::ReaderMutexLock lock(&mutex_);

  std::vector<std::string> formats;
  formats.reserve(formats_.size());

  // Collect unique format names (avoid duplicates from aliases)
  std::map<std::string, bool> seen;
  for (const auto& [ext, desc] : formats_) {
    if (seen.find(desc.format_name) == seen.end()) {
      formats.push_back(desc.format_name);
      seen[desc.format_name] = true;
    }
  }

  std::sort(formats.begin(), formats.end());
  return formats;
}

const FormatDescriptor* ReaderRegistry::GetFormat(
    std::string_view extension) const {
  std::string normalized = NormalizeExtension(extension);

  absl::ReaderMutexLock lock(&mutex_);

  auto it = formats_.find(normalized);
  if (it == formats_.end()) {
    return nullptr;
  }

  return &it->second;
}

bool ReaderRegistry::SupportsExtension(std::string_view extension) const {
  return GetFormat(extension) != nullptr;
}

bool ReaderRegistry::SupportsCapability(std::string_view extension,
                                        FormatCapability capability) const {
  const auto* format = GetFormat(extension);
  if (!format) {
    return false;
  }

  return HasCapability(format->capabilities, capability);
}

std::vector<std::string> ReaderRegistry::ListFormatsByCapability(
    FormatCapability capability) const {
  absl::ReaderMutexLock lock(&mutex_);

  std::vector<std::string> formats;

  // Collect unique format names that support the capability
  std::map<std::string, bool> seen;
  for (const auto& [ext, desc] : formats_) {
    if (HasCapability(desc.capabilities, capability)) {
      if (seen.find(desc.format_name) == seen.end()) {
        formats.push_back(desc.format_name);
        seen[desc.format_name] = true;
      }
    }
  }

  std::sort(formats.begin(), formats.end());
  return formats;
}

std::vector<std::string> ReaderRegistry::GetSupportedExtensions() const {
  absl::ReaderMutexLock lock(&mutex_);

  std::vector<std::string> extensions;
  extensions.reserve(formats_.size());

  for (const auto& [ext, desc] : formats_) {
    extensions.push_back(ext);
  }

  std::sort(extensions.begin(), extensions.end());
  return extensions;
}

void ReaderRegistry::Clear() {
  absl::MutexLock lock(&mutex_);
  formats_.clear();
}

// Global registry implementation
ReaderRegistry& GetGlobalRegistry() {
  static ReaderRegistry* global_registry = []() {
    auto* registry = new ReaderRegistry();
    // Register built-in formats (will be done in register_builtin_formats.cc)
    return registry;
  }();
  return *global_registry;
}

}  // namespace runtime
}  // namespace fastslide
