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

#include "fastslide/readers/dicom/dicom_magic.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>

namespace fastslide::dicom {
namespace {

constexpr std::array<unsigned char, kDicomMagicLength> kDicomMagic = {'D', 'I',
                                                                      'C', 'M'};

}  // namespace

bool BufferHasDicomMagic(std::span<const unsigned char> buffer) {
  if (buffer.size() < kDicomMagicHeaderSize) {
    return false;
  }
  return std::memcmp(buffer.data() + kDicomMagicOffset, kDicomMagic.data(),
                     kDicomMagic.size()) == 0;
}

bool HasDicomMagic(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }

  std::array<unsigned char, kDicomMagicHeaderSize> header{};
  stream.read(reinterpret_cast<char*>(header.data()),
              static_cast<std::streamsize>(header.size()));

  if (stream.gcount() < static_cast<std::streamsize>(kDicomMagicHeaderSize)) {
    return false;
  }
  return BufferHasDicomMagic(std::span<const unsigned char>(header));
}

}  // namespace fastslide::dicom
