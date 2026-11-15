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
#include <memory>
#include <string>
#include <vector>

#include "aifocore/status/result.h"

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
  aifocore::Status HashFile(const fs::path& file_path);

  /// @brief Add file portion to hash
  /// @param file_path Path to file
  /// @param offset Offset in file
  /// @param length Number of bytes to hash
  /// @return Status indicating success or failure
  aifocore::Status HashFilePart(const fs::path& file_path, int64_t offset,
                                int64_t length);

  /// @brief Add data buffer to hash
  /// @param data Pointer to data
  /// @param length Number of bytes
  /// @return Status indicating success or failure
  aifocore::Status HashData(const uint8_t* data, size_t length);

  /// @brief Add data buffer to hash
  /// @param data Vector of data
  /// @return Status indicating success or failure
  aifocore::Status HashData(const std::vector<uint8_t>& data);

  /// @brief Total bytes fed into the digest so far.
  [[nodiscard]] int64_t BytesHashed() const;

  /// @brief Finalize the digest.
  ///
  /// @return Lowercase 64-character hex digest.
  /// @retval kFailedPrecondition if called twice, or if nothing was ever
  ///         hashed. The latter would otherwise yield the SHA-256 of the empty
  ///         input, a fixed value that silently collides across every slide
  ///         the caller failed to read.
  [[nodiscard]] aifocore::Result<std::string> Finalize();

 private:
  /// @brief Holds the SHA-256 context, kept out of line so this header does
  ///        not pull in the codec.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_UTILITIES_HASH_H_
