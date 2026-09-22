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
#include "fastslide/readers/tiff_quickhash.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fastslide/utilities/hash.h"
#include "simpletiff/index.h"
#include "simpletiff/tiff_parser.h"

namespace fastslide {
namespace {

namespace fs = std::filesystem;

constexpr uint16_t kTypeShort = 3;
constexpr uint16_t kTypeLong = 4;
constexpr uint16_t kTypeAscii = 2;

constexpr uint16_t kTagDocumentName = 269;
constexpr uint16_t kTagImageDescription = 270;
constexpr uint16_t kTagMake = 271;
constexpr uint16_t kTagModel = 272;
constexpr uint16_t kTagSoftware = 305;
constexpr uint16_t kTagDateTime = 306;
constexpr uint16_t kTagArtist = 315;
constexpr uint16_t kTagHostComputer = 316;
constexpr uint16_t kTagCopyright = 33432;

/// @brief The nine ASCII tags OpenSlide folds into quickhash-1, and the
///        property names it hashes them under, in hashing order.
constexpr std::pair<uint16_t, std::string_view> kHashedTags[] = {
    {kTagImageDescription, "tiff.ImageDescription"},
    {kTagMake, "tiff.Make"},
    {kTagModel, "tiff.Model"},
    {kTagSoftware, "tiff.Software"},
    {kTagDateTime, "tiff.DateTime"},
    {kTagArtist, "tiff.Artist"},
    {kTagHostComputer, "tiff.HostComputer"},
    {kTagCopyright, "tiff.Copyright"},
    {kTagDocumentName, "tiff.DocumentName"},
};

void PushU16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFF));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void PushU32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
  }
}

/// @brief Description of a single-page TIFF to synthesise.
struct TiffSpec {
  uint16_t width = 4;
  uint16_t height = 4;
  uint16_t rows_per_strip = 2;
  /// @brief Strip payloads, in order. Uncompressed, since quickhash reads the
  ///        raw bytes without decoding them.
  std::vector<std::vector<uint8_t>> strips = {{1, 2, 3, 4, 5, 6, 7, 8},
                                              {9, 10, 11, 12, 13, 14, 15, 16}};
  /// @brief ASCII tags to emit. Omitted tags are genuinely absent from the IFD.
  std::map<uint16_t, std::string> ascii_tags = {
      {kTagImageDescription, "Aperio Image Library"},
      {kTagMake, "TestMake"},
      {kTagModel, "TestModel"},
      {kTagSoftware, "TestSoftware v1"},
      {kTagDateTime, "2026:09:22 10:00:00"},
      {kTagArtist, "TestArtist"},
      {kTagHostComputer, "TestHost"},
      {kTagCopyright, "TestCopyright"},
      {kTagDocumentName, "TestDocument"},
  };
};

/// @brief Write @p spec as a classic little-endian TIFF at @p path.
///
/// Emits exactly one stripped IFD: enough for simpletiff to index the page and
/// for the quickhash to find both raw strip bytes and the ASCII tags.
void WriteTiff(const TiffSpec& spec, const fs::path& path) {
  struct Entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;  // Inline value, or an offset patched in below.
    bool is_offset = false;
  };

  std::vector<Entry> entries;
  entries.push_back({256, kTypeShort, 1, spec.width});
  entries.push_back({257, kTypeShort, 1, spec.height});
  entries.push_back({258, kTypeShort, 1, 8});  // BitsPerSample
  entries.push_back({259, kTypeShort, 1, 1});  // Compression: none
  entries.push_back({262, kTypeShort, 1, 1});  // Photometric: BlackIsZero
  for (const auto& [tag, text] : spec.ascii_tags) {
    // A value of four bytes or fewer lives in the entry itself; anything
    // longer is an offset into the heap. Getting this wrong is invisible for
    // long strings and silently corrupts short ones.
    const auto byte_count = static_cast<uint32_t>(text.size() + 1);
    if (byte_count <= 4) {
      uint32_t inline_value = 0;
      for (size_t i = 0; i < text.size(); ++i) {
        inline_value |= static_cast<uint32_t>(static_cast<uint8_t>(text[i]))
                        << (8 * i);
      }
      entries.push_back(
          {tag, kTypeAscii, byte_count, inline_value, /*is_offset=*/false});
    } else {
      entries.push_back({tag, kTypeAscii, byte_count, 0, /*is_offset=*/true});
    }
  }
  const auto strip_count = static_cast<uint32_t>(spec.strips.size());
  entries.push_back({273, kTypeLong, strip_count, 0, /*is_offset=*/true});
  entries.push_back({277, kTypeShort, 1, 1});  // SamplesPerPixel
  entries.push_back({278, kTypeShort, 1, spec.rows_per_strip});
  entries.push_back({279, kTypeLong, strip_count, 0, /*is_offset=*/true});
  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.tag < b.tag; });

  // Header is 8 bytes, the IFD follows immediately, and everything too large
  // to sit inline goes after it.
  const uint32_t ifd_offset = 8;
  const auto ifd_bytes = static_cast<uint32_t>(2 + entries.size() * 12 + 4);
  uint32_t heap_cursor = ifd_offset + ifd_bytes;

  std::vector<uint8_t> heap;
  const auto reserve = [&](size_t bytes) {
    const uint32_t at = heap_cursor;
    heap_cursor += static_cast<uint32_t>(bytes);
    return at;
  };

  for (auto& entry : entries) {
    if (!entry.is_offset) {
      continue;
    }
    if (entry.type == kTypeAscii) {
      const std::string& text = spec.ascii_tags.at(entry.tag);
      entry.value = reserve(text.size() + 1);
      heap.insert(heap.end(), text.begin(), text.end());
      heap.push_back('\0');
    }
  }
  const uint32_t strip_offsets_at = reserve(strip_count * 4);
  const uint32_t strip_counts_at = reserve(strip_count * 4);
  for (auto& entry : entries) {
    if (entry.tag == 273) {
      entry.value = strip_offsets_at;
    } else if (entry.tag == 279) {
      entry.value = strip_counts_at;
    }
  }

  // Strip payloads land last; record where each one starts.
  std::vector<uint32_t> strip_offsets;
  std::vector<uint32_t> strip_lengths;
  for (const auto& strip : spec.strips) {
    strip_offsets.push_back(reserve(strip.size()));
    strip_lengths.push_back(static_cast<uint32_t>(strip.size()));
  }

  std::vector<uint8_t> offsets_blob;
  std::vector<uint8_t> lengths_blob;
  for (size_t i = 0; i < strip_offsets.size(); ++i) {
    PushU32(offsets_blob, strip_offsets[i]);
    PushU32(lengths_blob, strip_lengths[i]);
  }
  heap.insert(heap.end(), offsets_blob.begin(), offsets_blob.end());
  heap.insert(heap.end(), lengths_blob.begin(), lengths_blob.end());
  for (const auto& strip : spec.strips) {
    heap.insert(heap.end(), strip.begin(), strip.end());
  }

  std::vector<uint8_t> out;
  PushU16(out, 0x4949);  // "II": little-endian
  PushU16(out, 42);
  PushU32(out, ifd_offset);
  PushU16(out, static_cast<uint16_t>(entries.size()));
  for (const auto& entry : entries) {
    PushU16(out, entry.tag);
    PushU16(out, entry.type);
    PushU32(out, entry.count);
    PushU32(out, entry.value);
  }
  PushU32(out, 0);  // No next IFD.
  out.insert(out.end(), heap.begin(), heap.end());

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(out.data()),
               static_cast<std::streamsize>(out.size()));
}

/// @brief Digest of @p spec derived straight from the recipe, independently of
///        the production traversal: strip bytes in order, then each property
///        name and value NUL-terminated.
std::string ExpectedDigest(const TiffSpec& spec) {
  QuickHashBuilder hasher;
  for (const auto& strip : spec.strips) {
    EXPECT_TRUE(hasher.HashData(strip).ok());
  }
  for (const auto& [tag, name] : kHashedTags) {
    const auto it = spec.ascii_tags.find(tag);
    const std::string value = it == spec.ascii_tags.end() ? "" : it->second;
    EXPECT_TRUE(hasher
                    .HashData(reinterpret_cast<const uint8_t*>(name.data()),
                              name.size() + 1)
                    .ok());
    EXPECT_TRUE(hasher
                    .HashData(reinterpret_cast<const uint8_t*>(value.c_str()),
                              value.size() + 1)
                    .ok());
  }
  auto digest = hasher.Finalize();
  EXPECT_TRUE(digest.ok());
  return digest.ok() ? digest.value() : std::string();
}

class TiffQuickHashTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ =
        fs::temp_directory_path() /
        fs::path(
            "fastslide_tiff_quickhash_" +
            std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::create_directories(dir_);
  }

  void TearDown() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }

  /// @brief Write @p spec and return its quickhash.
  aifocore::Result<std::string> HashOf(const TiffSpec& spec,
                                       std::string_view name) {
    const fs::path path = dir_ / (std::string(name) + ".tif");
    WriteTiff(spec, path);

    index_ = std::make_unique<simpletiff::TiffIndex>();
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
    if (!simpletiff::OpenTiff(path.string(), *index_, fd_)) {
      return AIFOCORE_MAKE_STATUS(
          aifocore::StatusCode::kInvalidArgument,
          "Synthetic TIFF fixture did not parse: " + path.string());
    }
    return readers::tiff_quickhash::Compute(readers::tiff_quickhash::Spec{
        .index = index_.get(),
        .level_pages = {0},
        .property_page = 0,
    });
  }

  fs::path dir_;
  std::unique_ptr<simpletiff::TiffIndex> index_;
  int fd_ = -1;
};

TEST_F(TiffQuickHashTest, FollowsTheOpenSlideRecipe) {
  const TiffSpec spec;
  auto digest = HashOf(spec, "baseline");
  ASSERT_TRUE(digest.ok()) << digest.status().ToString();
  EXPECT_EQ(digest.value(), ExpectedDigest(spec));
}

TEST_F(TiffQuickHashTest, DigestIsNeverEmptyAndIsLowercaseHex) {
  auto digest = HashOf(TiffSpec{}, "shape");
  ASSERT_TRUE(digest.ok()) << digest.status().ToString();
  ASSERT_FALSE(digest.value().empty());
  EXPECT_EQ(digest.value().size(), 64U);
  EXPECT_TRUE(std::all_of(
      digest.value().begin(), digest.value().end(),
      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
}

TEST_F(TiffQuickHashTest, IdenticalFilesHashIdentically) {
  auto left = HashOf(TiffSpec{}, "left");
  auto right = HashOf(TiffSpec{}, "right");
  ASSERT_TRUE(left.ok());
  ASSERT_TRUE(right.ok());
  EXPECT_EQ(left.value(), right.value());
}

TEST_F(TiffQuickHashTest, PixelDataParticipates) {
  auto baseline = HashOf(TiffSpec{}, "pixels_before");
  ASSERT_TRUE(baseline.ok());

  TiffSpec changed;
  changed.strips[1][3] ^= 0xFF;
  auto after = HashOf(changed, "pixels_after");
  ASSERT_TRUE(after.ok());

  EXPECT_NE(baseline.value(), after.value());
}

// Each of the nine tags must reach the digest. Aperio's hand-rolled quickhash
// used to hash Software as empty no matter what the file said, which is the
// class of bug this catches.
TEST_F(TiffQuickHashTest, EveryHashedTagParticipates) {
  auto baseline = HashOf(TiffSpec{}, "tags_baseline");
  ASSERT_TRUE(baseline.ok());

  for (const auto& [tag, name] : kHashedTags) {
    TiffSpec changed;
    changed.ascii_tags[tag] = "perturbed";
    auto after = HashOf(changed, "tag_" + std::to_string(tag));
    ASSERT_TRUE(after.ok()) << after.status().ToString();
    EXPECT_NE(baseline.value(), after.value())
        << "Changing " << name << " did not change the digest";
  }
}

// OpenSlide hashes a missing tag as the empty string, so a file that omits a
// tag and one that carries it empty must agree.
TEST_F(TiffQuickHashTest, AbsentTagHashesAsEmptyValue) {
  TiffSpec absent;
  absent.ascii_tags.erase(kTagArtist);
  auto without = HashOf(absent, "artist_absent");
  ASSERT_TRUE(without.ok()) << without.status().ToString();

  TiffSpec empty;
  empty.ascii_tags[kTagArtist] = "";
  auto blank = HashOf(empty, "artist_empty");
  ASSERT_TRUE(blank.ok()) << blank.status().ToString();

  EXPECT_EQ(without.value(), blank.value());
}

TEST_F(TiffQuickHashTest, StripOrderIsPartOfTheDigest) {
  auto baseline = HashOf(TiffSpec{}, "strips_ordered");
  ASSERT_TRUE(baseline.ok());

  TiffSpec swapped;
  std::swap(swapped.strips[0], swapped.strips[1]);
  auto after = HashOf(swapped, "strips_swapped");
  ASSERT_TRUE(after.ok());

  EXPECT_NE(baseline.value(), after.value());
}

// The spec is the only thing a reader supplies, so a malformed one must fail
// loudly rather than fall through to a digest over nothing.
TEST(TiffQuickHashSpecTest, RejectsMissingIndex) {
  const auto digest =
      readers::tiff_quickhash::Compute(readers::tiff_quickhash::Spec{
          .index = nullptr, .level_pages = {0}, .property_page = 0});
  ASSERT_FALSE(digest.ok());
  EXPECT_EQ(digest.status().code(), aifocore::StatusCode::kInvalidArgument);
}

TEST(TiffQuickHashSpecTest, RejectsEmptyPageList) {
  const auto digest =
      readers::tiff_quickhash::Compute(readers::tiff_quickhash::Spec{
          .index = nullptr, .level_pages = {}, .property_page = 0});
  ASSERT_FALSE(digest.ok());
  EXPECT_EQ(digest.status().code(), aifocore::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace fastslide
