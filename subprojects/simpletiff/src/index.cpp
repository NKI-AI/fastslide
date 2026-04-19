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

#include "simpletiff/index.h"

#include <utility>

#include "aifocore/platform/portability.h"

namespace simpletiff {

TiffIndex::~TiffIndex() {
  if (fd_ >= 0) {
    aifocore::portable_close(fd_);
  }
}

TiffIndex::TiffIndex(TiffIndex&& other) noexcept
    : bigtiff_(other.bigtiff_),
      little_endian_(other.little_endian_),
      file_size_(other.file_size_),
      fd_(other.fd_),
      pages_(std::move(other.pages_)),
      tiles_pool_(std::move(other.tiles_pool_)),
      strips_pool_(std::move(other.strips_pool_)),
      single_pool_(std::move(other.single_pool_)),
      offsets_arena_(std::move(other.offsets_arena_)),
      bytecounts_arena_(std::move(other.bytecounts_arena_)),
      child_pages_arena_(std::move(other.child_pages_arena_)) {
  // Transfer ownership of fd
  other.fd_ = -1;
}

TiffIndex& TiffIndex::operator=(TiffIndex&& other) noexcept {
  if (this != &other) {
    // Clean up existing fd
    if (fd_ >= 0) {
      aifocore::portable_close(fd_);
    }

    // Move data
    bigtiff_ = other.bigtiff_;
    little_endian_ = other.little_endian_;
    file_size_ = other.file_size_;
    fd_ = other.fd_;
    pages_ = std::move(other.pages_);
    tiles_pool_ = std::move(other.tiles_pool_);
    strips_pool_ = std::move(other.strips_pool_);
    single_pool_ = std::move(other.single_pool_);
    offsets_arena_ = std::move(other.offsets_arena_);
    bytecounts_arena_ = std::move(other.bytecounts_arena_);
    child_pages_arena_ = std::move(other.child_pages_arena_);

    // Transfer ownership of fd
    other.fd_ = -1;
  }
  return *this;
}

}  // namespace simpletiff
