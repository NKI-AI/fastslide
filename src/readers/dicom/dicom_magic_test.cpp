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

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace fastslide::dicom {
namespace {

namespace fs = std::filesystem;

/// @brief RAII helper that creates a unique temporary directory and removes
/// it (and its contents) on destruction.
class ScopedTempDir {
 public:
  ScopedTempDir() {
    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist;
    auto base = fs::temp_directory_path();
    for (int attempt = 0; attempt < 16; ++attempt) {
      fs::path candidate =
          base / ("fastslide-dicom-magic-test-" + std::to_string(dist(rd)));
      std::error_code ec;
      if (fs::create_directory(candidate, ec) && !ec) {
        path_ = std::move(candidate);
        return;
      }
    }
    throw std::runtime_error("Failed to create temporary test directory");
  }

  ~ScopedTempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }

  ScopedTempDir(const ScopedTempDir&) = delete;
  ScopedTempDir& operator=(const ScopedTempDir&) = delete;

  [[nodiscard]] const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

/// @brief Write @p bytes to @p path, throwing on I/O failure.
void WriteBytes(const fs::path& path, std::span<const unsigned char> bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out) << "Failed to open " << path;
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(out.good());
}

/// @brief Build a minimal valid Part 10 DICOM header: 128 zero bytes plus
/// "DICM" plus a few trailing bytes so the file isn't suspiciously short.
std::vector<unsigned char> MakeValidDicomHeader(std::size_t trailing = 16) {
  std::vector<unsigned char> buf(kDicomMagicHeaderSize + trailing, 0);
  buf[kDicomMagicOffset + 0] = 'D';
  buf[kDicomMagicOffset + 1] = 'I';
  buf[kDicomMagicOffset + 2] = 'C';
  buf[kDicomMagicOffset + 3] = 'M';
  return buf;
}

}  // namespace

TEST(DicomMagicTest, BufferTooShortReturnsFalse) {
  std::array<unsigned char, kDicomMagicOffset> buf{};
  EXPECT_FALSE(BufferHasDicomMagic(std::span<const unsigned char>(buf)));
}

TEST(DicomMagicTest, BufferWithMagicReturnsTrue) {
  auto buf = MakeValidDicomHeader();
  EXPECT_TRUE(BufferHasDicomMagic(std::span<const unsigned char>(buf)));
}

TEST(DicomMagicTest, BufferWithWrongMagicReturnsFalse) {
  auto buf = MakeValidDicomHeader();
  buf[kDicomMagicOffset + 0] = 'X';
  EXPECT_FALSE(BufferHasDicomMagic(std::span<const unsigned char>(buf)));
}

TEST(DicomMagicTest, MagicMustBeAtCorrectOffset) {
  // Put "DICM" at offset 0 instead of 128; should not match.
  std::vector<unsigned char> buf(kDicomMagicHeaderSize, 0);
  buf[0] = 'D';
  buf[1] = 'I';
  buf[2] = 'C';
  buf[3] = 'M';
  EXPECT_FALSE(BufferHasDicomMagic(std::span<const unsigned char>(buf)));
}

TEST(DicomMagicTest, FileWithExtensionlessDicomDetected) {
  ScopedTempDir tmp;
  fs::path file = tmp.path() / "1_Map";  // Mirrors 3DHISTECH naming.
  ASSERT_NO_FATAL_FAILURE(WriteBytes(file, MakeValidDicomHeader()));
  EXPECT_TRUE(HasDicomMagic(file));
}

TEST(DicomMagicTest, ShortFileNotDetected) {
  ScopedTempDir tmp;
  fs::path file = tmp.path() / "short";
  std::vector<unsigned char> tiny{'D', 'I', 'C', 'M'};
  ASSERT_NO_FATAL_FAILURE(WriteBytes(file, tiny));
  EXPECT_FALSE(HasDicomMagic(file));
}

TEST(DicomMagicTest, MissingFileNotDetected) {
  ScopedTempDir tmp;
  fs::path file = tmp.path() / "does_not_exist";
  EXPECT_FALSE(HasDicomMagic(file));
}

TEST(DicomMagicTest, NonDicomFileNotDetected) {
  ScopedTempDir tmp;
  fs::path file = tmp.path() / "garbage.bin";
  std::vector<unsigned char> garbage(kDicomMagicHeaderSize + 4, 0xAB);
  ASSERT_NO_FATAL_FAILURE(WriteBytes(file, garbage));
  EXPECT_FALSE(HasDicomMagic(file));
}

}  // namespace fastslide::dicom
