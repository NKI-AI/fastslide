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

#ifndef AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILE_READER_H_
#define AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILE_READER_H_

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace fs = std::filesystem;

namespace fastslide {
namespace runtime {
namespace io {

/// @brief RAII wrapper for FILE* operations
///
/// Provides automatic file handle cleanup and error-checked operations.
/// Prevents resource leaks and reduces boilerplate in file I/O code.
///
/// Example usage:
/// ```cpp
/// ASSIGN_OR_RETURN(auto reader, FileReader::Open(path, "rb"));
/// ASSIGN_OR_RETURN(int64_t size, reader.GetSize());
/// RETURN_IF_ERROR(reader.Seek(offset));
/// ```
class FileReader {
 public:
  /// @brief Default constructor (creates invalid reader)
  FileReader() : file_(nullptr, fclose) {}

  /// @brief Open a file for reading/writing
  /// @param path Path to file
  /// @param mode File open mode ("rb", "wb", etc.)
  /// @return FileReader instance or error
  /// @retval absl::NotFoundError if file cannot be opened
  static absl::StatusOr<FileReader> Open(const fs::path& path,
                                         const char* mode);

  /// @brief Move constructor
  FileReader(FileReader&& other) noexcept = default;

  /// @brief Move assignment
  FileReader& operator=(FileReader&& other) noexcept = default;

  /// @brief Destructor (automatic via unique_ptr)
  ~FileReader() = default;

  /// @brief Deleted copy constructor (unique ownership)
  FileReader(const FileReader&) = delete;

  /// @brief Deleted copy assignment (unique ownership)
  FileReader& operator=(const FileReader&) = delete;

  /// @brief Get raw FILE pointer
  /// @return FILE pointer (never null)
  FILE* Get() const { return file_.get(); }

  /// @brief Seek to position in file
  /// @param offset Byte offset
  /// @param whence SEEK_SET, SEEK_CUR, or SEEK_END
  /// @return OkStatus or error
  absl::Status Seek(int64_t offset, int whence = SEEK_SET) const;

  /// @brief Get file size
  /// @return File size in bytes or error
  absl::StatusOr<int64_t> GetSize() const;

  /// @brief Read data from file
  /// @param buffer Buffer to read into
  /// @param size Number of bytes to read
  /// @return OkStatus or error
  absl::Status Read(void* buffer, size_t size) const;

  /// @brief Read data into vector
  /// @param size Number of bytes to read
  /// @return Vector of bytes or error
  absl::StatusOr<std::vector<uint8_t>> ReadBytes(size_t size) const;

  /// @brief Get current file position
  /// @return Current position or error
  absl::StatusOr<int64_t> Tell() const;

 private:
  /// @brief Private constructor (use Open factory method)
  explicit FileReader(FILE* file) : file_(file, fclose) {}

  std::unique_ptr<FILE, decltype(&fclose)> file_;
};

}  // namespace io
}  // namespace runtime

// Import into fastslide namespace for convenience
using runtime::io::FileReader;

}  // namespace fastslide

#endif  // AIFO_FASTSLIDE_INCLUDE_FASTSLIDE_RUNTIME_IO_FILE_READER_H_
