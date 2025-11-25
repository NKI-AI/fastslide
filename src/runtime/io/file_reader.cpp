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

#include "fastslide/runtime/io/file_reader.h"

#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"

namespace fastslide {
namespace runtime {
namespace io {

absl::StatusOr<FileReader> FileReader::Open(const fs::path& path,
                                            const char* mode) {
  FILE* file = fopen(path.string().c_str(), mode);
  if (!file) {
    return MAKE_STATUS(absl::StatusCode::kNotFound,
                       absl::StrFormat("Cannot open file: %s", path.string()));
  }
  return FileReader(file);
}

absl::Status FileReader::Seek(int64_t offset, int whence) const {
  if (fseek(file_.get(), offset, whence) != 0) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        absl::StrFormat("Failed to seek to offset %lld", offset));
  }
  return absl::OkStatus();
}

absl::StatusOr<int64_t> FileReader::GetSize() const {
  const int64_t current_pos = ftell(file_.get());
  if (current_pos < 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to get current file position");
  }

  if (fseek(file_.get(), 0, SEEK_END) != 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to seek to end of file");
  }

  const int64_t size = ftell(file_.get());
  if (size < 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to determine file size");
  }

  // Restore original position
  if (fseek(file_.get(), current_pos, SEEK_SET) != 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to restore file position");
  }

  return size;
}

absl::Status FileReader::Read(void* buffer, size_t size) const {
  if (fread(buffer, 1, size, file_.get()) != size) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       absl::StrFormat("Failed to read %zu bytes", size));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> FileReader::ReadBytes(size_t size) const {
  std::vector<uint8_t> buffer(size);
  RETURN_IF_ERROR(Read(buffer.data(), size), "Failed to read into buffer");
  return buffer;
}

absl::StatusOr<int64_t> FileReader::Tell() const {
  const int64_t pos = ftell(file_.get());
  if (pos < 0) {
    return MAKE_STATUS(absl::StatusCode::kInternal,
                       "Failed to get file position");
  }
  return pos;
}

}  // namespace io
}  // namespace runtime
}  // namespace fastslide
