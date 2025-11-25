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

#include "simpletiff/lzw.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace simpletiff {
namespace {

// Helper function to read binary test data file
std::vector<uint8_t> ReadTestDataFile(const char* filename) {
  // Try multiple possible paths (depending on where test is run from)
  const char* paths[] = {nullptr,  // Will be set to filename with path prefix
                         nullptr, nullptr};

  std::string path1 = std::string("testdata/") + filename;
  std::string path2 = std::string("../testdata/") + filename;
  std::string path3 = std::string("../../testdata/") + filename;

  paths[0] = path1.c_str();
  paths[1] = path2.c_str();
  paths[2] = path3.c_str();

  FILE* f = nullptr;
  for (const char* path : paths) {
    f = fopen(path, "rb");
    if (f)
      break;
  }

  if (!f) {
    return {};
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(size);
  fread(data.data(), 1, size, f);
  fclose(f);
  return data;
}

// Test case from Python tifffile/imagecodecs
TEST(LzwTest, PythonReferenceCase) {
  // Input from Python docstring
  const uint8_t input[] = {0x80, 0x1c, 0xcc, 0x27, 0x91, 0x01, 0xa0, 0xc2, 0x6d,
                           0x36, 0x99, 0x4e, 0x42, 0x03, 0xc9, 0xbe, 0x0b, 0x07,
                           0x84, 0xc2, 0xcd, 0xa6, 0x38, 0x7c, 0x22, 0x14, 0x20,
                           0x33, 0xc3, 0xa0, 0xd1, 0x63, 0x94, 0x02, 0x02};

  // Expected output: "say hammer yo hammer mc hammer go hammer"
  const char* expected = "say hammer yo hammer mc hammer go hammer";
  const size_t expected_len = 40;

  std::vector<uint8_t> output;
  bool result =
      DecompressLzw(std::span<const uint8_t>(input, sizeof(input)), output);

  ASSERT_TRUE(result) << "LZW decompression failed";
  ASSERT_EQ(output.size(), expected_len) << "Output size mismatch";

  // Compare output
  for (size_t i = 0; i < expected_len; ++i) {
    EXPECT_EQ(output[i], static_cast<uint8_t>(expected[i]))
        << "Mismatch at position " << i;
  }

  // Print actual output for debugging
  std::string actual_str(output.begin(), output.end());
  EXPECT_EQ(actual_str, std::string(expected));
}

// Test cases from imagecodecs test_lzw_msb
TEST(LzwTest, ImagecodecsTestMSB_Case1) {
  const uint8_t input[] = {
      0x80, 0x1c, 0xcc, 0x27, 0x91, 0x01, 0xa0, 0xc2, 0x6d, 0x36, 0x99, 0x4e,
      0x42, 0x03, 0xc9, 0xbe, 0x0b, 0x07, 0x84, 0xc2, 0xcd, 0xa6, 0x38, 0x7c,
      0x22, 0x14, 0x20, 0x33, 0xc3, 0xa0, 0xd1, 0x63, 0x94, 0x02, 0x02, 0x80};

  const char* expected = "say hammer yo hammer mc hammer go hammer";

  std::vector<uint8_t> output;
  bool result =
      DecompressLzw(std::span<const uint8_t>(input, sizeof(input)), output);

  ASSERT_TRUE(result);
  EXPECT_EQ(output.size(), strlen(expected));
  EXPECT_EQ(std::string(output.begin(), output.end()), std::string(expected));
}

TEST(LzwTest, ImagecodecsTestMSB_Case2) {
  const uint8_t input[] = {0x80, 0x18, 0x4d, 0xc6, 0x41, 0x01, 0xd0, 0xd0, 0x65,
                           0x10, 0x1c, 0x8c, 0xa7, 0x33, 0xa0, 0x80, 0xc7, 0x02,
                           0x10, 0x19, 0xcd, 0xe2, 0x08, 0x14, 0x10, 0xe0, 0x6c,
                           0x30, 0x9e, 0x60, 0x10, 0x10, 0x80};

  const char* expected = "and the rest can go and play";

  std::vector<uint8_t> output;
  bool result =
      DecompressLzw(std::span<const uint8_t>(input, sizeof(input)), output);

  ASSERT_TRUE(result);
  EXPECT_EQ(output.size(), strlen(expected));
  EXPECT_EQ(std::string(output.begin(), output.end()), std::string(expected));
}

TEST(LzwTest, ImagecodecsTestMSB_Case3) {
  const uint8_t input[] = {0x80, 0x18, 0xcc, 0x26, 0xe1, 0x39,
                           0xd0, 0x40, 0x74, 0x37, 0x9d, 0x4c,
                           0x66, 0x88, 0x39, 0xa0, 0xd2, 0x73};

  const char* expected = "can't touch this";

  std::vector<uint8_t> output;
  bool result =
      DecompressLzw(std::span<const uint8_t>(input, sizeof(input)), output);

  ASSERT_TRUE(result);
  EXPECT_EQ(output.size(), strlen(expected));
  EXPECT_EQ(std::string(output.begin(), output.end()), std::string(expected));
}

TEST(LzwTest, ImagecodecsTestMSB_EmptyOutput) {
  const uint8_t input[] = {0x80, 0x40, 0x40};

  std::vector<uint8_t> output;
  bool result =
      DecompressLzw(std::span<const uint8_t>(input, sizeof(input)), output);

  ASSERT_TRUE(result);
  EXPECT_EQ(output.size(), 0);
}

// Test LZW without EOI marker (from imagecodecs test_lzw_decode_image_noeoi)
TEST(LzwTest, ImagecodecsImageNoEOI) {
  // Read the LZW-compressed data without EOI marker (512x512 uint16)
  auto encoded = ReadTestDataFile("image_noeoi.lzw.bin");
  if (encoded.empty()) {
    GTEST_SKIP() << "Test file image_noeoi.lzw.bin not found";
    return;
  }

  // Decompress LZW
  std::vector<uint8_t> decoded;
  bool result = DecompressLzw(std::span<const uint8_t>(encoded), decoded);
  ASSERT_TRUE(result) << "LZW decompression failed";

  // Read expected output
  auto expected = ReadTestDataFile("image_noeoi.bin");
  ASSERT_FALSE(expected.empty()) << "Failed to read image_noeoi.bin";

  // Compare sizes
  EXPECT_EQ(decoded.size(), expected.size())
      << "Decoded size: " << decoded.size()
      << ", Expected size: " << expected.size();

  // Compare first and last bytes for quick verification
  if (!decoded.empty() && !expected.empty()) {
    EXPECT_EQ(decoded[0], expected[0]) << "First byte mismatch";
    EXPECT_EQ(decoded[decoded.size() - 1], expected[expected.size() - 1])
        << "Last byte mismatch";
  }

  // Full comparison (only if sizes match to avoid crash)
  if (decoded.size() == expected.size()) {
    for (size_t i = 0; i < expected.size(); ++i) {
      if (decoded[i] != expected[i]) {
        FAIL() << "Mismatch at byte " << i << ": got " << (int)decoded[i]
               << ", expected " << (int)expected[i];
        break;
      }
    }
  }
}

// Test corrupt LZW stream (from imagecodecs test_lzw_corrupt)
TEST(LzwTest, ImagecodecsCorruptStream) {
  auto encoded = ReadTestDataFile("corrupt.lzw.bin");
  if (encoded.empty()) {
    GTEST_SKIP() << "Test file corrupt.lzw.bin not found";
    return;
  }

  std::vector<uint8_t> decoded;
  // This should fail gracefully, not crash
  bool result = DecompressLzw(std::span<const uint8_t>(encoded), decoded);

  // We expect this to fail (return false) rather than crash
  // The imagecodecs test expects a RuntimeError
  EXPECT_FALSE(result) << "Corrupt stream should not decompress successfully";
}

}  // namespace
}  // namespace simpletiff
