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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_HASH_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_HASH_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "absl/status/status.h"

namespace fs = std::filesystem;

namespace fastslide {

/// @brief SHA-256 hash builder for creating slide quickhashes
///
/// This class provides a simple interface for computing SHA-256 hashes
/// compatible with OpenSlide's quickhash format. It supports incremental
/// hashing of files and data buffers.
class QuickHashBuilder {
 public:
  /// @brief Constructor
  QuickHashBuilder();

  /// @brief Destructor
  ~QuickHashBuilder();

  // Delete copy/move operations (contains SHA-256 context)
  QuickHashBuilder(const QuickHashBuilder&) = delete;
  QuickHashBuilder& operator=(const QuickHashBuilder&) = delete;
  QuickHashBuilder(QuickHashBuilder&&) = delete;
  QuickHashBuilder& operator=(QuickHashBuilder&&) = delete;

  /// @brief Add file contents to hash
  /// @param file_path Path to file to hash
  /// @return Status indicating success or failure
  absl::Status HashFile(const fs::path& file_path);

  /// @brief Add file portion to hash
  /// @param file_path Path to file
  /// @param offset Offset in file
  /// @param length Number of bytes to hash
  /// @return Status indicating success or failure
  absl::Status HashFilePart(const fs::path& file_path, int64_t offset,
                            int64_t length);

  /// @brief Add data buffer to hash
  /// @param data Pointer to data
  /// @param length Number of bytes
  /// @return Status indicating success or failure
  absl::Status HashData(const uint8_t* data, size_t length);

  /// @brief Add data buffer to hash
  /// @param data Vector of data
  /// @return Status indicating success or failure
  absl::Status HashData(const std::vector<uint8_t>& data);

  /// @brief Finalize hash and get result as hex string
  /// @return Hex-encoded hash string (64 characters for SHA-256)
  std::string Finalize();

 private:
  void* ctx_;  // SHA-256 context (opaque pointer to avoid header dependency)
  std::vector<uint8_t> hash_buffer_;  // Buffer for final hash result
  bool finalized_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_HASH_H_
