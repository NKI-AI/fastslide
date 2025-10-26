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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_READER_REGISTRY_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_READER_REGISTRY_H_

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "fastslide/runtime/cache_interface.h"
#include "fastslide/runtime/format_descriptor.h"

/**
 * @file reader_registry.h
 * @brief Modern reader registry with format descriptor support
 * 
 * This header defines the new ReaderRegistry that uses FormatDescriptor
 * for format registration.
 * The registry is designed to be injectable for testing while providing
 * a global accessor for convenience.
 */

namespace fastslide {

// Forward declaration
class SlideReader;

namespace runtime {

/// @brief Reader registry for format plugins
///
/// The ReaderRegistry manages format descriptors and creates readers based
/// on file extensions and capabilities.
/// This registry can be owned by applications or test fixtures, allowing
/// proper dependency injection.
///
/// Key features:
/// - Format registration via FormatDescriptor
/// - Capability queries (what formats support what features)
/// - Extension-based reader creation
/// - Thread-safe operations
/// - Testable (non-singleton by default)
///
/// Example usage:
/// @code
/// // Create and configure registry
/// ReaderRegistry registry;
/// registry.RegisterFormat(CreateMrxsFormatDescriptor());
/// registry.RegisterFormat(CreateQptiffFormatDescriptor());
///
/// // Query capabilities
/// auto formats = registry.ListFormats();
/// bool supports_spectral = registry.SupportsCapability(
///     ".qptiff", FormatCapability::kSpectral);
///
/// // Create reader with cache
/// auto reader = registry.CreateReader("slide.mrxs", my_cache);
/// @endcode
class ReaderRegistry {
 public:
  /// @brief Constructor
  ReaderRegistry() = default;

  /// @brief Destructor
  ~ReaderRegistry() = default;

  // Non-copyable, movable
  ReaderRegistry(const ReaderRegistry&) = delete;
  ReaderRegistry& operator=(const ReaderRegistry&) = delete;
  ReaderRegistry(ReaderRegistry&&) = default;
  ReaderRegistry& operator=(ReaderRegistry&&) = default;

  /// @brief Register a format descriptor
  /// @param descriptor Format descriptor to register
  /// @note Thread-safe. If a format with the same primary extension already
  ///       exists, it will be replaced.
  void RegisterFormat(FormatDescriptor descriptor);

  /// @brief Register multiple format descriptors
  /// @param descriptors Vector of format descriptors
  void RegisterFormats(std::vector<FormatDescriptor> descriptors);

  /// @brief Create a reader for the given file
  /// @param filename Path to slide file
  /// @param cache Optional tile cache (nullptr = no caching)
  /// @return SlideReader instance or error status
  /// @note Thread-safe
  [[nodiscard]] absl::StatusOr<std::unique_ptr<SlideReader>> CreateReader(
      std::string_view filename,
      std::shared_ptr<ITileCache> cache = nullptr) const;

  /// @brief List all registered formats
  /// @return Vector of format names
  /// @note Thread-safe
  [[nodiscard]] std::vector<std::string> ListFormats() const;

  /// @brief Get format descriptor for a given extension
  /// @param extension File extension (e.g., ".mrxs")
  /// @return Format descriptor or nullptr if not found
  /// @note Thread-safe
  [[nodiscard]] const FormatDescriptor* GetFormat(
      std::string_view extension) const;

  /// @brief Check if extension is supported
  /// @param extension File extension (e.g., ".mrxs")
  /// @return True if supported
  /// @note Thread-safe
  [[nodiscard]] bool SupportsExtension(std::string_view extension) const;

  /// @brief Check if format supports a capability
  /// @param extension File extension
  /// @param capability Capability to check
  /// @return True if supported, false if not or format not found
  /// @note Thread-safe
  [[nodiscard]] bool SupportsCapability(std::string_view extension,
                                        FormatCapability capability) const;

  /// @brief List formats that support a specific capability
  /// @param capability Capability to filter by
  /// @return Vector of format names that support the capability
  /// @note Thread-safe
  [[nodiscard]] std::vector<std::string> ListFormatsByCapability(
      FormatCapability capability) const;

  /// @brief Get all supported extensions
  /// @return Vector of extensions (sorted)
  /// @note Thread-safe
  [[nodiscard]] std::vector<std::string> GetSupportedExtensions() const;

  /// @brief Clear all registered formats
  /// @note Thread-safe
  void Clear();

 private:
  /// @brief Normalize extension (lowercase with leading dot)
  static std::string NormalizeExtension(std::string_view extension);

  /// @brief Format storage (extension -> descriptor)
  std::map<std::string, FormatDescriptor> formats_;

  /// @brief Mutex for thread-safe access
  mutable absl::Mutex mutex_;
};

/// @brief Get global default registry
///
/// Returns a reference to the global registry instance. This is provided
/// for backward compatibility and convenience. For testing or applications
/// that need control over the registry, create your own ReaderRegistry
/// instance instead.
///
/// The global registry is initialized lazily on first access with all
/// built-in formats registered.
///
/// @return Reference to global registry
ReaderRegistry& GetGlobalRegistry();

}  // namespace runtime

// Import into fastslide namespace
using runtime::GetGlobalRegistry;
using runtime::ReaderRegistry;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_READER_REGISTRY_H_
