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

#include "aifocore/status/result.h"
#include "aifocore/utilities/fmt.h"
#include "aifocore/platform/portability.h"

namespace fastslide {
namespace runtime {
namespace io {

aifocore::Result<FileReader> FileReader::Open(const fs::path& path,
                                            const char* mode) {
  FILE* file = aifocore::portable_fopen(path, mode);
  if (!file) {
    return aifocore::Status(aifocore::StatusCode::kNotFound,
                       aifocore::fmt::format("Cannot open file: {}", path.string()));
  }
  return FileReader(file);
}

aifocore::Status FileReader::Seek(int64_t offset, int whence) const {
  if (aifocore::portable_fseek(file_.get(), offset, whence) != 0) {
    return aifocore::Status(
        aifocore::StatusCode::kInternal,
        aifocore::fmt::format("Failed to seek to offset {}", offset));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Result<int64_t> FileReader::GetSize() const {
  const int64_t current_pos = aifocore::portable_ftell(file_.get());
  if (current_pos < 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                       "Failed to get current file position");
  }

  if (aifocore::portable_fseek(file_.get(), 0, SEEK_END) != 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                       "Failed to seek to end of file");
  }

  const int64_t size = aifocore::portable_ftell(file_.get());
  if (size < 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                       "Failed to determine file size");
  }

  // Restore original position
  if (aifocore::portable_fseek(file_.get(), current_pos, SEEK_SET) != 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                       "Failed to restore file position");
  }

  return size;
}

aifocore::Status FileReader::Read(void* buffer, size_t size) const {
  if (fread(buffer, 1, size, file_.get()) != size) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                       aifocore::fmt::format("Failed to read {} bytes", size));
  }
  return aifocore::Status::OkStatus();
}

aifocore::Result<std::vector<uint8_t>> FileReader::ReadBytes(size_t size) const {
  std::vector<uint8_t> buffer(size);
  AIFOCORE_RETURN_IF_ERROR(Read(buffer.data(), size));
  return buffer;
}

aifocore::Result<int64_t> FileReader::Tell() const {
  const int64_t pos = aifocore::portable_ftell(file_.get());
  if (pos < 0) {
    return aifocore::Status(aifocore::StatusCode::kInternal,
                       "Failed to get file position");
  }
  return pos;
}

}  // namespace io
}  // namespace runtime
}  // namespace fastslide
