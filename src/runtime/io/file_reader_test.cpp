// Copyright 2026 Jonas Teuwen. All Rights Reserved.
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

/// @file file_reader_test.cpp
/// @brief Tests for FileReader::ReadAt.

#include "fastslide/runtime/io/file_reader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fastslide::runtime::io {
namespace {

namespace fs = std::filesystem;

class FileReaderReadAtTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = fs::temp_directory_path() /
            ("fastslide_file_reader_test_" +
             std::to_string(std::random_device{}()) + ".bin");
    data_.resize(1 << 16);
    for (size_t i = 0; i < data_.size(); ++i) {
      data_[i] = static_cast<uint8_t>((i * 31) ^ (i >> 8));
    }
    std::ofstream out(path_, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data_.data()),
              static_cast<std::streamsize>(data_.size()));
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove(path_, ec);
  }

  fs::path path_;
  std::vector<uint8_t> data_;
};

TEST_F(FileReaderReadAtTest, ReadsAtOffsetWithoutMovingThePosition) {
  auto reader = FileReader::Open(path_, "rb");
  ASSERT_TRUE(reader.ok());
  ASSERT_TRUE(reader->Seek(100).ok());

  auto bytes = reader->ReadAt(4097, 300);
  ASSERT_TRUE(bytes.ok());
  EXPECT_EQ(*bytes,
            std::vector<uint8_t>(data_.begin() + 4097, data_.begin() + 4397));
  auto pos = reader->Tell();
  ASSERT_TRUE(pos.ok());
  EXPECT_EQ(*pos, 100);
}

TEST_F(FileReaderReadAtTest, ReadsUpToTheEndOfTheFile) {
  auto reader = FileReader::Open(path_, "rb");
  ASSERT_TRUE(reader.ok());
  auto bytes = reader->ReadAt(static_cast<int64_t>(data_.size()) - 10, 10);
  ASSERT_TRUE(bytes.ok());
  EXPECT_EQ(*bytes, std::vector<uint8_t>(data_.end() - 10, data_.end()));
}

TEST_F(FileReaderReadAtTest, FailsOnAShortRead) {
  auto reader = FileReader::Open(path_, "rb");
  ASSERT_TRUE(reader.ok());
  EXPECT_FALSE(
      reader->ReadAt(static_cast<int64_t>(data_.size()) - 10, 11).ok());
  EXPECT_FALSE(reader->ReadAt(static_cast<int64_t>(data_.size()) + 1, 1).ok());
}

TEST_F(FileReaderReadAtTest, ConcurrentReadsShareOneReader) {
  auto reader = FileReader::Open(path_, "rb");
  ASSERT_TRUE(reader.ok());
  std::vector<std::thread> threads;
  std::vector<int> failures(8, 0);
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t] {
      std::mt19937 rng(t);
      for (int i = 0; i < 500; ++i) {
        const size_t len = 1 + rng() % 4096;
        const size_t off = rng() % (data_.size() - len);
        auto bytes = reader->ReadAt(static_cast<int64_t>(off), len);
        if (!bytes.ok() ||
            *bytes != std::vector<uint8_t>(data_.begin() + off,
                                           data_.begin() + off + len)) {
          ++failures[t];
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  for (int t = 0; t < 8; ++t) {
    EXPECT_EQ(failures[t], 0) << "thread " << t;
  }
}

}  // namespace
}  // namespace fastslide::runtime::io
