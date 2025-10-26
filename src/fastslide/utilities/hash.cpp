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

#include "fastslide/utilities/hash.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "aifocore/status/status_macros.h"
#include "fastslide/utilities/sha-256.h"

namespace fastslide {

QuickHashBuilder::QuickHashBuilder() : finalized_(false) {
  ctx_ = new Sha_256;
  hash_buffer_.resize(SIZE_OF_SHA_256_HASH);
  sha_256_init(static_cast<Sha_256*>(ctx_), hash_buffer_.data());
}

QuickHashBuilder::~QuickHashBuilder() {
  if (ctx_) {
    delete static_cast<Sha_256*>(ctx_);
  }
}

absl::Status QuickHashBuilder::HashFile(const fs::path& file_path) {
  if (finalized_) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "Hash already finalized");
  }

  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    return MAKE_STATUS(
        absl::StatusCode::kNotFound,
        absl::StrFormat("Cannot open file: %s", file_path.string()));
  }

  std::array<uint8_t, 8192> buffer;
  while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) ||
         file.gcount() > 0) {
    sha_256_write(static_cast<Sha_256*>(ctx_), buffer.data(), file.gcount());
  }

  if (file.bad()) {
    return MAKE_STATUS(
        absl::StatusCode::kInternal,
        absl::StrFormat("Error reading file: %s", file_path.string()));
  }

  return absl::OkStatus();
}

absl::Status QuickHashBuilder::HashFilePart(const fs::path& file_path,
                                            int64_t offset, int64_t length) {
  if (finalized_) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "Hash already finalized");
  }

  FILE* file = fopen(file_path.string().c_str(), "rb");
  if (!file) {
    return MAKE_STATUS(
        absl::StatusCode::kNotFound,
        absl::StrFormat("Cannot open file: %s", file_path.string()));
  }

  if (fseek(file, offset, SEEK_SET) != 0) {
    fclose(file);
    return MAKE_STATUS(absl::StatusCode::kInternal, "Failed to seek in file");
  }

  std::array<uint8_t, 8192> buffer;
  int64_t remaining = length;

  while (remaining > 0) {
    const size_t to_read =
        std::min(remaining, static_cast<int64_t>(buffer.size()));
    const size_t bytes_read = fread(buffer.data(), 1, to_read, file);

    if (bytes_read > 0) {
      sha_256_write(static_cast<Sha_256*>(ctx_), buffer.data(), bytes_read);
      remaining -= bytes_read;
    }

    if (bytes_read < to_read) {
      if (ferror(file)) {
        fclose(file);
        return MAKE_STATUS(absl::StatusCode::kInternal, "Error reading file");
      }
      break;  // EOF
    }
  }

  fclose(file);
  return absl::OkStatus();
}

absl::Status QuickHashBuilder::HashData(const uint8_t* data, size_t length) {
  if (finalized_) {
    return MAKE_STATUS(absl::StatusCode::kFailedPrecondition,
                       "Hash already finalized");
  }

  sha_256_write(static_cast<Sha_256*>(ctx_), data, length);
  return absl::OkStatus();
}

absl::Status QuickHashBuilder::HashData(const std::vector<uint8_t>& data) {
  return HashData(data.data(), data.size());
}

std::string QuickHashBuilder::Finalize() {
  if (finalized_) {
    return "";  // Already finalized
  }

  sha_256_close(static_cast<Sha_256*>(ctx_));
  finalized_ = true;

  // Convert to hex string
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < SIZE_OF_SHA_256_HASH; ++i) {
    oss << std::setw(2) << static_cast<unsigned>(hash_buffer_[i]);
  }

  return oss.str();
}

}  // namespace fastslide
