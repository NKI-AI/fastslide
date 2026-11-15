// Copyright 2026 Jonas Teuwen
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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace fastslide {
namespace {

namespace fs = std::filesystem;

/// @brief SHA-256 of "abc", the canonical FIPS 180-4 test vector.
constexpr std::string_view kAbcDigest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

/// @brief Write @p contents to a uniquely named file under the temp dir.
fs::path WriteTempFile(std::string_view name, std::string_view contents) {
  const fs::path path =
      fs::temp_directory_path() /
      (std::string("fastslide_hash_test_") + std::string(name));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  out.close();
  return path;
}

aifocore::Status HashString(QuickHashBuilder& hasher, std::string_view text) {
  return hasher.HashData(reinterpret_cast<const uint8_t*>(text.data()),
                         text.size());
}

TEST(QuickHashBuilderTest, MatchesKnownSha256Vector) {
  QuickHashBuilder hasher;
  ASSERT_TRUE(HashString(hasher, "abc").ok());

  auto digest = hasher.Finalize();
  ASSERT_TRUE(digest.ok()) << digest.status().ToString();
  EXPECT_EQ(digest.value(), kAbcDigest);
}

TEST(QuickHashBuilderTest, DigestIsSixtyFourLowercaseHexDigits) {
  QuickHashBuilder hasher;
  ASSERT_TRUE(HashString(hasher, "some slide bytes").ok());

  auto digest = hasher.Finalize();
  ASSERT_TRUE(digest.ok());
  EXPECT_EQ(digest.value().size(), 64U);
  EXPECT_TRUE(std::all_of(
      digest.value().begin(), digest.value().end(),
      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
      << "Digest must be lowercase hex: " << digest.value();
}

TEST(QuickHashBuilderTest, IncrementalHashingMatchesSingleShot) {
  QuickHashBuilder chunked;
  ASSERT_TRUE(HashString(chunked, "a").ok());
  ASSERT_TRUE(HashString(chunked, "b").ok());
  ASSERT_TRUE(HashString(chunked, "c").ok());

  auto digest = chunked.Finalize();
  ASSERT_TRUE(digest.ok());
  EXPECT_EQ(digest.value(), kAbcDigest);
}

// The invariant that motivated Finalize() returning a Result: a digest over no
// input is the SHA-256 of the empty string, which is identical for every slide
// and so is worse than no answer at all.
TEST(QuickHashBuilderTest, FinalizeOverZeroBytesFails) {
  QuickHashBuilder hasher;

  auto digest = hasher.Finalize();
  ASSERT_FALSE(digest.ok());
  EXPECT_EQ(digest.status().code(), aifocore::StatusCode::kFailedPrecondition);
}

TEST(QuickHashBuilderTest, FinalizeTwiceFails) {
  QuickHashBuilder hasher;
  ASSERT_TRUE(HashString(hasher, "abc").ok());
  ASSERT_TRUE(hasher.Finalize().ok());

  auto second = hasher.Finalize();
  ASSERT_FALSE(second.ok());
  EXPECT_EQ(second.status().code(), aifocore::StatusCode::kFailedPrecondition);
}

TEST(QuickHashBuilderTest, HashDataOfZeroLengthLeavesBuilderEmpty) {
  QuickHashBuilder hasher;
  ASSERT_TRUE(hasher.HashData(nullptr, 0).ok());
  EXPECT_EQ(hasher.BytesHashed(), 0);

  // Still refuses to produce the empty-input digest.
  EXPECT_FALSE(hasher.Finalize().ok());
}

TEST(QuickHashBuilderTest, BytesHashedTracksInput) {
  QuickHashBuilder hasher;
  EXPECT_EQ(hasher.BytesHashed(), 0);

  ASSERT_TRUE(HashString(hasher, "abc").ok());
  EXPECT_EQ(hasher.BytesHashed(), 3);

  ASSERT_TRUE(HashString(hasher, "de").ok());
  EXPECT_EQ(hasher.BytesHashed(), 5);
}

TEST(QuickHashBuilderTest, HashFileMatchesEquivalentBuffer) {
  const fs::path path = WriteTempFile("whole_file.bin", "abc");

  QuickHashBuilder from_file;
  ASSERT_TRUE(from_file.HashFile(path).ok());
  auto file_digest = from_file.Finalize();
  ASSERT_TRUE(file_digest.ok());

  EXPECT_EQ(file_digest.value(), kAbcDigest);
  fs::remove(path);
}

TEST(QuickHashBuilderTest, HashFileOnMissingFileFails) {
  QuickHashBuilder hasher;
  const auto status =
      hasher.HashFile(fs::temp_directory_path() / "fastslide_absent.bin");
  EXPECT_FALSE(status.ok());
}

TEST(QuickHashBuilderTest, HashFilePartReadsTheRequestedWindow) {
  const fs::path path = WriteTempFile("part.bin", "xxxabcyyy");

  QuickHashBuilder hasher;
  ASSERT_TRUE(hasher.HashFilePart(path, /*offset=*/3, /*length=*/3).ok());
  auto digest = hasher.Finalize();
  ASSERT_TRUE(digest.ok());

  EXPECT_EQ(digest.value(), kAbcDigest);
  fs::remove(path);
}

// A short read used to be treated as EOF, which silently produced a digest
// over fewer bytes than the caller asked for.
TEST(QuickHashBuilderTest, HashFilePartPastEndOfFileFails) {
  const fs::path path = WriteTempFile("short.bin", "abc");

  QuickHashBuilder hasher;
  const auto status = hasher.HashFilePart(path, /*offset=*/0, /*length=*/64);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), aifocore::StatusCode::kOutOfRange);

  fs::remove(path);
}

TEST(QuickHashBuilderTest, DifferentInputsProduceDifferentDigests) {
  QuickHashBuilder left;
  ASSERT_TRUE(HashString(left, "slide-a").ok());
  auto left_digest = left.Finalize();
  ASSERT_TRUE(left_digest.ok());

  QuickHashBuilder right;
  ASSERT_TRUE(HashString(right, "slide-b").ok());
  auto right_digest = right.Finalize();
  ASSERT_TRUE(right_digest.ok());

  EXPECT_NE(left_digest.value(), right_digest.value());
}

// Concatenation must not be ambiguous: hashing ("ab", "c") and ("a", "bc")
// deliberately agree, but distinct field *values* must not collide. This is
// why the identifier recipes hash NUL-terminated strings.
TEST(QuickHashBuilderTest, NulTerminationSeparatesAdjacentFields) {
  const auto hash_fields = [](std::string_view first, std::string_view second) {
    QuickHashBuilder hasher;
    EXPECT_TRUE(hasher
                    .HashData(reinterpret_cast<const uint8_t*>(first.data()),
                              first.size() + 1)
                    .ok());
    EXPECT_TRUE(hasher
                    .HashData(reinterpret_cast<const uint8_t*>(second.data()),
                              second.size() + 1)
                    .ok());
    auto digest = hasher.Finalize();
    EXPECT_TRUE(digest.ok());
    return digest.value();
  };

  EXPECT_NE(hash_fields("ab", "c"), hash_fields("a", "bc"));
}

}  // namespace
}  // namespace fastslide
