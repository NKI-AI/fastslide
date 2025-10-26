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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_METADATA_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_METADATA_H_

#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"

namespace fastslide {

/// @brief Metadata key constants for standardized access
///
/// This namespace defines the standardized metadata keys that slide readers
/// should populate. Keys are categorized as:
/// - **Mandatory**: All readers MUST provide these keys
/// - **Optional**: Readers SHOULD provide these if available
/// - **Format-specific**: Readers MAY provide additional format-specific keys
namespace MetadataKeys {

// Mandatory keys (all readers must provide)
inline constexpr std::string_view kFormat =
    "format";  ///< File format name (e.g., "MRXS", "SVS", "QPTIFF")
inline constexpr std::string_view kLevels =
    "levels";  ///< Number of pyramid levels

// Optional keys (provide if available)
inline constexpr std::string_view kMppX =
    "mpp_x";  ///< Microns per pixel in X direction
inline constexpr std::string_view kMppY =
    "mpp_y";  ///< Microns per pixel in Y direction
inline constexpr std::string_view kMagnification =
    "magnification";  ///< Objective magnification
inline constexpr std::string_view kObjective = "objective";  ///< Objective name
inline constexpr std::string_view kScannerModel =
    "scanner_model";  ///< Scanner manufacturer/model
inline constexpr std::string_view kScannerID = "scanner_id";  ///< Scanner ID
inline constexpr std::string_view kSlideID = "slide_id";  ///< Slide identifier
inline constexpr std::string_view kChannels =
    "channels";  ///< Number of channels
inline constexpr std::string_view kAssociatedImages =
    "associated_images";  ///< Number of associated images

// Format-specific keys (readers may add more as needed)

}  // namespace MetadataKeys

/// @brief Enhanced metadata container with printing capabilities
///
/// The Metadata class provides a type-safe container for slide metadata with
/// standardized keys defined in MetadataKeys namespace. All readers should:
/// 1. Provide all mandatory keys (format, levels)
/// 2. Provide optional keys when available
/// 3. May add format-specific keys with appropriate prefixes
class Metadata {
 public:
  /// @brief Value type for metadata entries
  using Value = std::variant<std::string, size_t, double>;

  /// @brief Underlying container type
  using Container = std::map<std::string, Value>;

  /// @brief Iterator types
  using iterator = Container::iterator;
  using const_iterator = Container::const_iterator;
  using value_type = Container::value_type;
  using key_type = Container::key_type;
  using mapped_type = Container::mapped_type;

  /// @brief Default constructor
  Metadata() = default;

  /// @brief Constructor from initializer list
  /// @param init Initializer list of key-value pairs
  Metadata(std::initializer_list<value_type> init) : data_(init) {}

  /// @brief Constructor from std::map (for backward compatibility)
  /// @param data Map to initialize from
  explicit Metadata(const Container& data) : data_(data) {}

  /// @brief Constructor from std::map (move version)
  /// @param data Map to move from
  explicit Metadata(Container&& data) : data_(std::move(data)) {}

  // Map-like interface for full backward compatibility

  /// @brief Access element by key
  /// @param key Key to access
  /// @return Reference to the value
  Value& operator[](const std::string& key) { return data_[key]; }

  /// @brief Access element by key (const)
  /// @param key Key to access
  /// @return Const reference to the value
  const Value& at(const std::string& key) const { return data_.at(key); }

  /// @brief Access element by key
  /// @param key Key to access
  /// @return Reference to the value
  Value& at(const std::string& key) { return data_.at(key); }

  /// @brief Insert or assign key-value pair
  /// @param key Key to insert
  /// @param value Value to insert
  /// @return Pair of iterator and bool indicating insertion
  template <typename T>
  std::pair<iterator, bool> insert_or_assign(const std::string& key,
                                             T&& value) {
    return data_.insert_or_assign(key, std::forward<T>(value));
  }

  /// @brief Insert key-value pair
  /// @param value Key-value pair to insert
  /// @return Pair of iterator and bool indicating insertion
  std::pair<iterator, bool> insert(const value_type& value) {
    return data_.insert(value);
  }

  /// @brief Find element by key
  /// @param key Key to find
  /// @return Iterator to element or end()
  iterator find(const std::string& key) { return data_.find(key); }

  /// @brief Find element by key (const)
  /// @param key Key to find
  /// @return Const iterator to element or end()
  const_iterator find(const std::string& key) const { return data_.find(key); }

  /// @brief Check if key exists
  /// @param key Key to check
  /// @return True if key exists
  bool contains(const std::string& key) const { return data_.contains(key); }

  /// @brief Get number of elements
  /// @return Number of elements
  size_t size() const noexcept { return data_.size(); }

  /// @brief Check if empty
  /// @return True if empty
  bool empty() const noexcept { return data_.empty(); }

  /// @brief Clear all elements
  void clear() noexcept { data_.clear(); }

  /// @brief Begin iterator
  /// @return Iterator to beginning
  iterator begin() { return data_.begin(); }

  /// @brief Begin iterator (const)
  /// @return Const iterator to beginning
  const_iterator begin() const { return data_.begin(); }

  /// @brief End iterator
  /// @return Iterator to end
  iterator end() { return data_.end(); }

  /// @brief End iterator (const)
  /// @return Const iterator to end
  const_iterator end() const { return data_.end(); }

  /// @brief Const begin iterator
  /// @return Const iterator to beginning
  const_iterator cbegin() const { return data_.cbegin(); }

  /// @brief Const end iterator
  /// @return Const iterator to end
  const_iterator cend() const { return data_.cend(); }

  // Enhanced functionality

  /// @brief Get value as string with optional default
  /// @param key Key to look up
  /// @param default_value Default value if key not found
  /// @return String value or default
  std::string GetString(const std::string& key,
                        const std::string& default_value = "") const {
    auto it = data_.find(key);
    if (it == data_.end()) {
      return default_value;
    }

    return std::visit(
        [&default_value](const auto& value) -> std::string {
          if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                       std::string>) {
            return value;
          } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                              size_t>) {
            return std::to_string(value);
          } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                              double>) {
            return std::to_string(value);
          }
          return default_value;
        },
        it->second);
  }

  /// @brief Get value as double with optional default
  /// @param key Key to look up
  /// @param default_value Default value if key not found or cannot convert
  /// @return Double value or default
  double GetDouble(const std::string& key, double default_value = 0.0) const {
    auto it = data_.find(key);
    if (it == data_.end()) {
      return default_value;
    }

    return std::visit(
        [default_value](const auto& value) -> double {
          if constexpr (std::is_same_v<std::decay_t<decltype(value)>, double>) {
            return value;
          } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                              size_t>) {
            return static_cast<double>(value);
          } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                              std::string>) {
            try {
              return std::stod(value);
            } catch (...) {
              return default_value;
            }
          }
          return default_value;
        },
        it->second);
  }

  /// @brief Get value as size_t with optional default
  /// @param key Key to look up
  /// @param default_value Default value if key not found or cannot convert
  /// @return size_t value or default
  size_t GetSize(const std::string& key, size_t default_value = 0) const {
    auto it = data_.find(key);
    if (it == data_.end()) {
      return default_value;
    }

    return std::visit(
        [default_value](const auto& value) -> size_t {
          if constexpr (std::is_same_v<std::decay_t<decltype(value)>, size_t>) {
            return value;
          } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                              double>) {
            return static_cast<size_t>(value);
          } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                              std::string>) {
            try {
              return std::stoull(value);
            } catch (...) {
              return default_value;
            }
          }
          return default_value;
        },
        it->second);
  }

  /// @brief Convert to formatted string
  /// @param indent Number of spaces to indent each line
  /// @return Formatted string representation
  std::string ToString(size_t indent = 0) const {
    std::string result;
    std::string indent_str(indent, ' ');

    for (const auto& [key, value] : data_) {
      result += indent_str + key + ": ";

      std::visit(
          [&result](const auto& v) {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                         std::string>) {
              result += "\"" + v + "\"";
            } else {
              result += std::to_string(v);
            }
          },
          value);

      result += "\n";
    }

    return result;
  }

  /// @brief Get underlying container (for compatibility)
  /// @return Reference to underlying container
  const Container& GetContainer() const { return data_; }

  /// @brief Get underlying container (for compatibility)
  /// @return Reference to underlying container
  Container& GetContainer() { return data_; }

  /// @brief Implicit conversion to underlying container for backward
  /// compatibility
  /// @return Reference to underlying container
  operator const Container&() const { return data_; }

  /// @brief Implicit conversion to underlying container for backward
  /// compatibility
  /// @return Reference to underlying container
  operator Container&() { return data_; }

  /// @brief Validate that mandatory keys are present
  /// @return Status indicating success or missing keys
  [[nodiscard]] absl::Status ValidateMandatory() const {
    std::vector<std::string> missing_keys;

    if (!contains(std::string(MetadataKeys::kFormat))) {
      missing_keys.push_back(std::string(MetadataKeys::kFormat));
    }
    if (!contains(std::string(MetadataKeys::kLevels))) {
      missing_keys.push_back(std::string(MetadataKeys::kLevels));
    }

    if (!missing_keys.empty()) {
      std::string msg = "Missing mandatory metadata keys: ";
      for (size_t i = 0; i < missing_keys.size(); ++i) {
        if (i > 0)
          msg += ", ";
        msg += missing_keys[i];
      }
      return absl::InvalidArgumentError(msg);
    }

    return absl::OkStatus();
  }

 private:
  Container data_;  ///< Underlying metadata storage
};

/// @brief Stream output operator for Metadata
/// @param os Output stream
/// @param metadata Metadata to output
/// @return Output stream reference
inline std::ostream& operator<<(std::ostream& os, const Metadata& metadata) {
  os << "Metadata {\n";
  for (const auto& [key, value] : metadata) {
    os << "  " << key << ": ";

    std::visit(
        [&os](const auto& v) {
          if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                       std::string>) {
            os << "\"" << v << "\"";
          } else {
            os << v;
          }
        },
        value);

    os << "\n";
  }
  os << "}";
  return os;
}

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_METADATA_H_
