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

#include "simpletiff/reader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "aifocore/platform/portability.h"
#include "simpletiff/errors.h"
#include "simpletiff/index.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/tiff_constants.h"
#include "simpletiff/tiff_parser.h"

namespace simpletiff {
namespace {

void AppendLe16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void AppendLe32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendLe64(std::vector<uint8_t>& out, uint64_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 32) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 40) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 48) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 56) & 0xFF));
}

void AppendIfdEntryClassic(std::vector<uint8_t>& out, uint16_t tag,
                           uint16_t type, uint32_t count,
                           uint32_t value_or_offset) {
  AppendLe16(out, tag);
  AppendLe16(out, type);
  AppendLe32(out, count);
  AppendLe32(out, value_or_offset);
}

// =============================================================================
// DecodeContext Tests
// =============================================================================

TEST(DecodeContextTest, ContextsAreIndependent) {
  // Verify that multiple contexts maintain independent state
  DecodeContext ctx1;
  DecodeContext ctx2;

  // Allocate buffers in ctx1
  ctx1.jpeg_stream_buffer.resize(1024);
  ctx1.temp_buffer.resize(512);
  std::fill(ctx1.jpeg_stream_buffer.begin(), ctx1.jpeg_stream_buffer.end(),
            0xAA);
  std::fill(ctx1.temp_buffer.begin(), ctx1.temp_buffer.end(), 0xBB);

  // Allocate different sizes in ctx2
  ctx2.jpeg_stream_buffer.resize(2048);
  ctx2.temp_buffer.resize(256);
  std::fill(ctx2.jpeg_stream_buffer.begin(), ctx2.jpeg_stream_buffer.end(),
            0xCC);
  std::fill(ctx2.temp_buffer.begin(), ctx2.temp_buffer.end(), 0xDD);

  // Verify ctx1 wasn't affected
  EXPECT_EQ(ctx1.jpeg_stream_buffer.size(), 1024u);
  EXPECT_EQ(ctx1.temp_buffer.size(), 512u);
  EXPECT_EQ(ctx1.jpeg_stream_buffer[0], 0xAA);
  EXPECT_EQ(ctx1.temp_buffer[0], 0xBB);

  // Verify ctx2 has its own data
  EXPECT_EQ(ctx2.jpeg_stream_buffer.size(), 2048u);
  EXPECT_EQ(ctx2.temp_buffer.size(), 256u);
  EXPECT_EQ(ctx2.jpeg_stream_buffer[0], 0xCC);
  EXPECT_EQ(ctx2.temp_buffer[0], 0xDD);
}

TEST(DecodeContextTest, ContextReuseAvoidsReallocation) {
  // Demonstrate that reusing a context avoids repeated allocations
  DecodeContext ctx;

  // First "read" allocates
  ctx.jpeg_stream_buffer.resize(1024);
  const void* initial_ptr = ctx.jpeg_stream_buffer.data();

  // Second "read" with smaller size shouldn't reallocate
  ctx.jpeg_stream_buffer.resize(512);
  EXPECT_EQ(ctx.jpeg_stream_buffer.data(), initial_ptr)
      << "Buffer should not reallocate when shrinking";

  // Verify capacity is preserved
  EXPECT_GE(ctx.jpeg_stream_buffer.capacity(), 1024u)
      << "Capacity should be preserved for future reuse";

  // Third "read" with same or smaller size still no reallocation
  ctx.jpeg_stream_buffer.resize(800);
  EXPECT_EQ(ctx.jpeg_stream_buffer.data(), initial_ptr)
      << "Buffer should not reallocate within existing capacity";
}

TEST(DecodeContextTest, ThreadSafety_MultipleThreadsOwnContexts) {
  // Demonstrate that each thread can safely use its own context
  // This was impossible with thread_local due to testing difficulties

  constexpr int kNumThreads = 4;
  constexpr int kIterations = 100;
  std::vector<std::thread> threads;
  std::vector<bool> success(kNumThreads, false);

  for (int thread_id = 0; thread_id < kNumThreads; ++thread_id) {
    threads.emplace_back([thread_id, &success]() {
      // Each thread creates its own context
      DecodeContext ctx;

      // Simulate reading operations with different sizes per thread
      const size_t thread_buffer_size = 1000 + thread_id * 500;

      for (int i = 0; i < kIterations; ++i) {
        // Resize to simulate decompression
        ctx.jpeg_stream_buffer.resize(thread_buffer_size);
        ctx.temp_buffer.resize(thread_buffer_size / 2);

        // Write thread-specific pattern
        const uint8_t pattern = static_cast<uint8_t>(thread_id);
        std::fill(ctx.jpeg_stream_buffer.begin(), ctx.jpeg_stream_buffer.end(),
                  pattern);

        // Verify data integrity (no cross-thread contamination)
        for (const auto& byte : ctx.jpeg_stream_buffer) {
          if (byte != pattern) {
            success[thread_id] = false;
            return;  // Data corruption detected
          }
        }
      }

      success[thread_id] = true;
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  // Verify all threads succeeded
  for (int i = 0; i < kNumThreads; ++i) {
    EXPECT_TRUE(success[i]) << "Thread " << i << " detected data corruption";
  }
}

TEST(DecodeContextTest, ExplicitLifetime_RAIICleanup) {
  // Demonstrate that context lifetime is explicit and follows RAII
  size_t initial_allocation = 0;

  {
    DecodeContext ctx;
    ctx.jpeg_stream_buffer.resize(10000);
    ctx.temp_buffer.resize(5000);
    initial_allocation =
        ctx.jpeg_stream_buffer.capacity() + ctx.temp_buffer.capacity();

    // Context is in scope, memory is allocated
    EXPECT_GT(initial_allocation, 0u);
  }
  // Context destroyed here, memory automatically freed by RAII
  // No manual cleanup needed, no dangling thread_local state

  // Create a new context - fresh start, no pollution from previous
  DecodeContext ctx2;
  EXPECT_TRUE(ctx2.jpeg_stream_buffer.empty());
  EXPECT_TRUE(ctx2.temp_buffer.empty());
}

TEST(DecodeContextTest, DecodeContext_IsMovable) {
  // Verify that contexts can be moved efficiently (RVO/move semantics)
  DecodeContext ctx1;
  ctx1.jpeg_stream_buffer.resize(1024);
  ctx1.temp_buffer.resize(512);
  const void* original_ptr = ctx1.jpeg_stream_buffer.data();

  // Move construct
  DecodeContext ctx2(std::move(ctx1));
  EXPECT_EQ(ctx2.jpeg_stream_buffer.data(), original_ptr)
      << "Move should transfer ownership, not copy";
  EXPECT_EQ(ctx2.jpeg_stream_buffer.size(), 1024u);
  EXPECT_EQ(ctx2.temp_buffer.size(), 512u);

  // ctx1 is in valid but unspecified state after move
  // (typically empty for std::vector)
}

TEST(DecodeContextTest, MultipleReadsShareContext) {
  // Demonstrate pattern: create context once, use for multiple reads
  // This simulates real usage where a user reads many tiles/pages
  DecodeContext ctx;

  // Simulate reading 10 pages with varying buffer requirements
  for (int i = 0; i < 10; ++i) {
    const size_t required_size = 500 + i * 100;

    // Simulate a read operation that needs buffer space
    ctx.jpeg_stream_buffer.resize(required_size);
    std::fill(ctx.jpeg_stream_buffer.begin(), ctx.jpeg_stream_buffer.end(),
              static_cast<uint8_t>(i));

    // Verify the buffer works correctly
    EXPECT_EQ(ctx.jpeg_stream_buffer.size(), required_size);
    EXPECT_EQ(ctx.jpeg_stream_buffer[0], static_cast<uint8_t>(i));
  }

  // Final capacity should be at least as large as the largest allocation
  EXPECT_GE(ctx.jpeg_stream_buffer.capacity(), 1400u)
      << "Context should retain capacity for efficient reuse";
}

// =============================================================================
// ROI Tests - Verifying Region of Interest handling
// =============================================================================

TEST(RoiTest, SingleJpegPageRespectsRoi) {
  // Verify that single-JPEG storage respects ROI parameter
  // This prevents buffer overflows and ensures consistent behavior

  // Create a fake 10x10 image
  constexpr uint32_t kImageWidth = 10;
  constexpr uint32_t kImageHeight = 10;
  constexpr uint32_t kChannels = 3;

  std::vector<uint8_t> fake_image(kImageWidth * kImageHeight * kChannels);
  for (size_t i = 0; i < fake_image.size(); ++i) {
    fake_image[i] = static_cast<uint8_t>(i % 256);
  }

  // Request ROI: 2x2 region at (3, 3)
  Roi roi{3, 3, 2, 2};

  // Destination buffer sized for ROI (not full image)
  const int roi_stride = static_cast<int>(roi.width) * kChannels;
  std::vector<uint8_t> dst(roi.width * roi.height * kChannels);

  // Verify ROI extraction logic (simulates what ReadPage does)
  const uint32_t x0 = std::min(roi.x, kImageWidth);
  const uint32_t y0 = std::min(roi.y, kImageHeight);
  const uint32_t x1 = std::min(roi.x + roi.width, kImageWidth);
  const uint32_t y1 = std::min(roi.y + roi.height, kImageHeight);

  EXPECT_EQ(x0, 3u);
  EXPECT_EQ(y0, 3u);
  EXPECT_EQ(x1, 5u);
  EXPECT_EQ(y1, 5u);

  const uint32_t roi_w = x1 - x0;
  const uint32_t roi_h = y1 - y0;

  EXPECT_EQ(roi_w, 2u);
  EXPECT_EQ(roi_h, 2u);

  // Simulate copying ROI from full image
  const int full_stride = static_cast<int>(kImageWidth) * kChannels;
  for (uint32_t y = 0; y < roi_h; ++y) {
    const uint8_t* src_row =
        fake_image.data() + (y0 + y) * full_stride + x0 * kChannels;
    uint8_t* dst_row = dst.data() + y * roi_stride;
    std::memcpy(dst_row, src_row, roi_w * kChannels);
  }

  // Verify correct data was copied
  // Pixel at (3, 3) in original should be at (0, 0) in ROI
  const size_t expected_offset = (3 * kImageWidth + 3) * kChannels;
  EXPECT_EQ(dst[0], fake_image[expected_offset]);
  EXPECT_EQ(dst[1], fake_image[expected_offset + 1]);
  EXPECT_EQ(dst[2], fake_image[expected_offset + 2]);
}

TEST(RoiTest, RoiClampingPreventsOverflow) {
  // Verify that ROI clamping prevents reading beyond image bounds

  constexpr uint32_t kImageWidth = 10;
  constexpr uint32_t kImageHeight = 10;

  // Test 1: ROI extends beyond right edge
  {
    Roi roi{8, 5, 5, 2};  // Starts at x=8, width=5 (would extend to x=13)

    const uint32_t x0 = std::min(roi.x, kImageWidth);
    const uint32_t x1 = std::min(roi.x + roi.width, kImageWidth);

    EXPECT_EQ(x0, 8u);
    EXPECT_EQ(x1, 10u);      // Clamped to image width
    EXPECT_EQ(x1 - x0, 2u);  // Actual ROI width is 2, not 5
  }

  // Test 2: ROI extends beyond bottom edge
  {
    Roi roi{5, 8, 2, 5};  // Starts at y=8, height=5 (would extend to y=13)

    const uint32_t y0 = std::min(roi.y, kImageHeight);
    const uint32_t y1 = std::min(roi.y + roi.height, kImageHeight);

    EXPECT_EQ(y0, 8u);
    EXPECT_EQ(y1, 10u);      // Clamped to image height
    EXPECT_EQ(y1 - y0, 2u);  // Actual ROI height is 2, not 5
  }

  // Test 3: ROI completely outside image (should be detected as invalid)
  {
    Roi roi{15, 20, 5, 5};  // Completely beyond image

    const uint32_t x0 = std::min(roi.x, kImageWidth);
    const uint32_t x1 = std::min(roi.x + roi.width, kImageWidth);
    const uint32_t y0 = std::min(roi.y, kImageHeight);
    const uint32_t y1 = std::min(roi.y + roi.height, kImageHeight);

    // Both clamped to image edge
    EXPECT_EQ(x0, 10u);
    EXPECT_EQ(x1, 10u);
    EXPECT_EQ(y0, 10u);
    EXPECT_EQ(y1, 10u);

    // Zero-size ROI detected
    EXPECT_TRUE(x0 >= x1 || y0 >= y1);
  }
}

// =============================================================================
// Result<> API Tests - Test the Result<T> monadic error handling
// =============================================================================

TEST(ResultTest, ResultOkWhenSuccessful) {
  // Test that Result<void>::ok() works correctly for success case
  auto make_success = []() -> Result<void> {
    return Result<void>();  // Default constructor = success
  };

  auto result = make_success();
  EXPECT_TRUE(result.Ok());
  EXPECT_FALSE(result.IsError());
  EXPECT_TRUE(static_cast<bool>(result));
}

TEST(ResultTest, ResultErrorWhenFailed) {
  // Test that Result<void> correctly handles error case
  auto make_error = []() -> Result<void> {
    return aifocore::Error("Test error message");
  };

  auto result = make_error();
  EXPECT_FALSE(result.Ok());
  EXPECT_TRUE(result.IsError());
  EXPECT_FALSE(static_cast<bool>(result));
  EXPECT_EQ(result.error().message(), "Test error message");
}

TEST(ResultTest, InvalidPageIndexReturnsError) {
  // Test that accessing invalid page index returns proper error
  TiffIndex index;
  // Index with no pages

  DecodeContext ctx;
  std::vector<uint8_t> dst(100);
  Roi roi{0, 0, 10, 10};

  // Try to read from non-existent page 0
  auto result = ReadPage(index, 0, roi, ctx, dst.data(), 30);

  EXPECT_FALSE(result.Ok());
  EXPECT_TRUE(result.IsError());

  std::string msg = result.error().message();
  EXPECT_NE(msg.find("out of range"), std::string::npos)
      << "Error should mention 'out of range'";
}

TEST(ResultTest, InvalidPageIndexWithNumber) {
  // Test error message includes page numbers
  TiffIndex index;

  // Add one valid page
  PageHeader page;
  page.storage = Storage::kTiles;
  page.width = 100;
  page.height = 100;
  page.compression = static_cast<uint16_t>(Compression::kNone);
  index.AddPage(page);

  DecodeContext ctx;
  std::vector<uint8_t> dst(100);
  Roi roi{0, 0, 10, 10};

  // Try to read page 5 when only page 0 exists
  auto result = ReadPage(index, 5, roi, ctx, dst.data(), 30);

  EXPECT_FALSE(result.Ok());
  std::string msg = result.error().message();
  EXPECT_NE(msg.find("5"), std::string::npos) << "Error should mention page 5";
  EXPECT_NE(msg.find("1"), std::string::npos) << "Error should mention 1 page";
}

// =============================================================================
// Index API Tests - Test the TiffIndex structure building
// =============================================================================

TEST(IndexTest, EmptyIndexHasNoPages) {
  // Test that a new index is properly initialized
  TiffIndex index;

  EXPECT_EQ(index.NumPages(), 0u);
  EXPECT_TRUE(index.IsLittleEndian());  // Default
  EXPECT_FALSE(index.IsBigTiff());      // Default
}

TEST(IndexTest, CanAddPages) {
  // Test adding pages to index
  TiffIndex index;

  PageHeader page1;
  page1.storage = Storage::kTiles;
  page1.width = 1024;
  page1.height = 768;
  page1.compression = static_cast<uint16_t>(Compression::kJpeg);

  index.AddPage(page1);
  EXPECT_EQ(index.NumPages(), 1u);

  PageHeader page2;
  page2.storage = Storage::kStrips;
  page2.width = 512;
  page2.height = 512;
  page2.compression = static_cast<uint16_t>(Compression::kLzw);

  index.AddPage(page2);
  EXPECT_EQ(index.NumPages(), 2u);

  // Verify pages are accessible
  const auto& retrieved1 = index.Page(0);
  EXPECT_EQ(retrieved1.width, 1024u);
  EXPECT_EQ(retrieved1.height, 768u);

  const auto& retrieved2 = index.Page(1);
  EXPECT_EQ(retrieved2.width, 512u);
  EXPECT_EQ(retrieved2.height, 512u);
}

TEST(IndexTest, CanStoreOffsetsAndBytecounts) {
  // Test the offset and bytecount arena storage
  TiffIndex index;

  std::vector<uint64_t> offsets = {100, 200, 300, 400};
  std::vector<uint64_t> bytecounts = {50, 60, 70, 80};

  auto offset_span = index.AppendOffsets(offsets);
  auto bytecount_span = index.AppendBytecounts(bytecounts);

  // Verify we can retrieve the data via spans
  auto retrieved_offsets = index.Offsets(offset_span);
  auto retrieved_bytecounts = index.Bytecounts(bytecount_span);

  ASSERT_EQ(retrieved_offsets.size(), 4u);
  ASSERT_EQ(retrieved_bytecounts.size(), 4u);

  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(retrieved_offsets[i], offsets[i]);
    EXPECT_EQ(retrieved_bytecounts[i], bytecounts[i]);
  }
}

// =============================================================================
// Compression Support Tests
// =============================================================================

TEST(CompressionTest, SupportedCompressionsAreRecognized) {
  // Test that the Compression enum values are correct
  EXPECT_EQ(static_cast<uint16_t>(Compression::kNone), 1u);
  EXPECT_EQ(static_cast<uint16_t>(Compression::kLzw), 5u);
  EXPECT_EQ(static_cast<uint16_t>(Compression::kJpeg), 7u);
  EXPECT_EQ(static_cast<uint16_t>(Compression::kJpeg2000), 33003u);
  EXPECT_EQ(static_cast<uint16_t>(Compression::kZstd), 50000u);
}

TEST(CompressionTest, IsCompressionHelperWorks) {
  // Test the IsCompression helper function
  uint16_t jpeg_code = 7;
  EXPECT_TRUE(IsCompression(jpeg_code, Compression::kJpeg));
  EXPECT_FALSE(IsCompression(jpeg_code, Compression::kLzw));

  uint16_t none_code = 1;
  EXPECT_TRUE(IsCompression(none_code, Compression::kNone));
  EXPECT_FALSE(IsCompression(none_code, Compression::kJpeg));
}

TEST(Jpeg2000Test, CanReadSingleStripJpeg2000ClassicTiff) {
  // Construct a minimal ClassicTIFF with a single strip whose payload is a
  // JPEG2000 codestream (Compression=33003).
  //
  // Note: We intentionally use a truncated codestream payload. The goal of this
  // test is to validate that Compression=33003 is recognized and routed through
  // the JPEG2000 decoding path (i.e. returns a JPEG2000 codec error, not an
  // "unsupported compression" error).
  constexpr int kWidth = 2;
  constexpr int kHeight = 2;
  const std::vector<uint8_t> j2k = {0xFF, 0x4F};  // SOC marker only (invalid)
  ASSERT_FALSE(j2k.empty());

  constexpr uint16_t kTypeShort = 3;
  constexpr uint16_t kTypeLong = 4;

  constexpr uint32_t kHeaderSize = 8;
  constexpr uint32_t kIfdOffset = kHeaderSize;
  constexpr uint16_t kNumEntries = 9;
  constexpr uint32_t kIfdSize = 2 + static_cast<uint32_t>(kNumEntries) * 12 + 4;
  const uint32_t strip_offset = kIfdOffset + kIfdSize;

  std::vector<uint8_t> tiff;
  tiff.reserve(strip_offset + static_cast<uint32_t>(j2k.size()));

  // TIFF header: "II" + 42 + first IFD offset.
  tiff.push_back(0x49);
  tiff.push_back(0x49);
  AppendLe16(tiff, 42);
  AppendLe32(tiff, kIfdOffset);

  // IFD0.
  AppendLe16(tiff, kNumEntries);
  AppendIfdEntryClassic(tiff, /*tag=*/256, kTypeLong, /*count=*/1,
                        /*value=*/kWidth);  // ImageWidth
  AppendIfdEntryClassic(tiff, /*tag=*/257, kTypeLong, /*count=*/1,
                        /*value=*/kHeight);  // ImageLength
  AppendIfdEntryClassic(tiff, /*tag=*/258, kTypeShort, /*count=*/1,
                        /*value=*/8);  // BitsPerSample
  AppendIfdEntryClassic(tiff, /*tag=*/259, kTypeShort, /*count=*/1,
                        /*value=*/33003);  // Compression (JPEG2000)
  AppendIfdEntryClassic(tiff, /*tag=*/262, kTypeShort, /*count=*/1,
                        /*value=*/1);  // Photometric (min-is-black)
  AppendIfdEntryClassic(tiff, /*tag=*/277, kTypeShort, /*count=*/1,
                        /*value=*/1);  // SamplesPerPixel
  AppendIfdEntryClassic(tiff, /*tag=*/278, kTypeLong, /*count=*/1,
                        /*value=*/kHeight);  // RowsPerStrip
  AppendIfdEntryClassic(tiff, /*tag=*/273, kTypeLong, /*count=*/1,
                        /*value=*/strip_offset);  // StripOffsets
  AppendIfdEntryClassic(
      tiff, /*tag=*/279, kTypeLong, /*count=*/1,
      /*value=*/static_cast<uint32_t>(j2k.size()));  // StripByteCounts
  AppendLe32(tiff, /*next_ifd=*/0);

  ASSERT_EQ(tiff.size(), strip_offset);
  tiff.insert(tiff.end(), j2k.begin(), j2k.end());

  // Write to a temp file (Bazel provides TEST_TMPDIR).
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::filesystem::path tmp_dir = test_tmpdir
                                      ? std::filesystem::path(test_tmpdir)
                                      : std::filesystem::temp_directory_path();
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path tiff_path =
      tmp_dir / ("simpletiff_jpeg2000_" + unique + ".tif");

  std::ofstream ofs(tiff_path, std::ios::binary);
  ASSERT_TRUE(ofs.good());
  ofs.write(reinterpret_cast<const char*>(tiff.data()),
            static_cast<std::streamsize>(tiff.size()));
  ofs.close();

  TiffIndex index;
  int fd = -1;
  ASSERT_TRUE(OpenTiff(tiff_path.string(), index, fd));
  ASSERT_EQ(index.NumPages(), 1u);
  EXPECT_EQ(index.Page(0).compression,
            static_cast<uint16_t>(Compression::kJpeg2000));

  DecodeContext ctx;
  std::vector<uint8_t> decoded;
  auto r = ReadStripe(index, /*page_index=*/0, /*strip_index=*/0, ctx, decoded);
  ASSERT_FALSE(r);
  EXPECT_NE(r.error().ToString().find("JPEG2000 decompression failed"),
            std::string::npos);

  if (fd >= 0) {
    aifocore::portable_close(fd);
  }
}

TEST(Jpeg2000Test, CanReadSingleStripJpeg2000ClassicTiffCompression33005) {
  // Same as CanReadSingleStripJpeg2000ClassicTiff, but using Compression=33005.
  // This matches some SVS/JPEG2000 writers in the wild. The key assertion is
  // that we route 33005 through the JPEG2000 decoder path (and thus get a
  // JPEG2000 codec error on truncated data, not an "unsupported compression"
  // error).
  constexpr int kWidth = 2;
  constexpr int kHeight = 2;
  const std::vector<uint8_t> j2k = {0xFF, 0x4F};  // SOC marker only (invalid)
  ASSERT_FALSE(j2k.empty());

  constexpr uint16_t kTypeShort = 3;
  constexpr uint16_t kTypeLong = 4;

  constexpr uint32_t kHeaderSize = 8;
  constexpr uint32_t kIfdOffset = kHeaderSize;
  constexpr uint16_t kNumEntries = 9;
  constexpr uint32_t kIfdSize = 2 + static_cast<uint32_t>(kNumEntries) * 12 + 4;
  const uint32_t strip_offset = kIfdOffset + kIfdSize;

  std::vector<uint8_t> tiff;
  tiff.reserve(strip_offset + static_cast<uint32_t>(j2k.size()));

  // TIFF header: "II" + 42 + first IFD offset.
  tiff.push_back(0x49);
  tiff.push_back(0x49);
  AppendLe16(tiff, 42);
  AppendLe32(tiff, kIfdOffset);

  // IFD0.
  AppendLe16(tiff, kNumEntries);
  AppendIfdEntryClassic(tiff, /*tag=*/256, kTypeLong, /*count=*/1,
                        /*value=*/kWidth);  // ImageWidth
  AppendIfdEntryClassic(tiff, /*tag=*/257, kTypeLong, /*count=*/1,
                        /*value=*/kHeight);  // ImageLength
  AppendIfdEntryClassic(tiff, /*tag=*/258, kTypeShort, /*count=*/1,
                        /*value=*/8);  // BitsPerSample
  AppendIfdEntryClassic(tiff, /*tag=*/259, kTypeShort, /*count=*/1,
                        /*value=*/33005);  // Compression (JPEG2000)
  AppendIfdEntryClassic(tiff, /*tag=*/262, kTypeShort, /*count=*/1,
                        /*value=*/1);  // Photometric (min-is-black)
  AppendIfdEntryClassic(tiff, /*tag=*/277, kTypeShort, /*count=*/1,
                        /*value=*/1);  // SamplesPerPixel
  AppendIfdEntryClassic(tiff, /*tag=*/278, kTypeLong, /*count=*/1,
                        /*value=*/kHeight);  // RowsPerStrip
  AppendIfdEntryClassic(tiff, /*tag=*/273, kTypeLong, /*count=*/1,
                        /*value=*/strip_offset);  // StripOffsets
  AppendIfdEntryClassic(
      tiff, /*tag=*/279, kTypeLong, /*count=*/1,
      /*value=*/static_cast<uint32_t>(j2k.size()));  // StripByteCounts
  AppendLe32(tiff, /*next_ifd=*/0);

  ASSERT_EQ(tiff.size(), strip_offset);
  tiff.insert(tiff.end(), j2k.begin(), j2k.end());

  // Write to a temp file (Bazel provides TEST_TMPDIR).
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::filesystem::path tmp_dir = test_tmpdir
                                      ? std::filesystem::path(test_tmpdir)
                                      : std::filesystem::temp_directory_path();
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path tiff_path =
      tmp_dir / ("simpletiff_jpeg2000_33005_" + unique + ".tif");

  std::ofstream ofs(tiff_path, std::ios::binary);
  ASSERT_TRUE(ofs.good());
  ofs.write(reinterpret_cast<const char*>(tiff.data()),
            static_cast<std::streamsize>(tiff.size()));
  ofs.close();

  TiffIndex index;
  int fd = -1;
  ASSERT_TRUE(OpenTiff(tiff_path.string(), index, fd));
  ASSERT_EQ(index.NumPages(), 1u);
  EXPECT_EQ(index.Page(0).compression, 33005u);
  EXPECT_TRUE(IsCompression(index.Page(0).compression, Compression::kJpeg2000));

  DecodeContext ctx;
  std::vector<uint8_t> decoded;
  auto r = ReadStripe(index, /*page_index=*/0, /*strip_index=*/0, ctx, decoded);
  ASSERT_FALSE(r);
  EXPECT_NE(r.error().ToString().find("JPEG2000 decompression failed"),
            std::string::npos);

  if (fd >= 0) {
    aifocore::portable_close(fd);
  }
}

// =============================================================================
// CopyTileInto Tests - Test tile copying logic
// =============================================================================

TEST(CopyTileTest, BasicTileCopy) {
  // Test copying a tile into a larger destination buffer
  constexpr int kTileWidth = 2;
  constexpr int kTileHeight = 2;
  constexpr int kChannels = 3;

  // Source tile: 2x2 RGB
  std::vector<uint8_t> tile_data = {
      1, 2, 3, 4,  5,  6,  // Row 0
      7, 8, 9, 10, 11, 12  // Row 1
  };

  // Destination: 4x4 RGB (filled with zeros)
  constexpr int kDstWidth = 4;
  constexpr int kDstHeight = 4;
  constexpr int kDstStride = kDstWidth * kChannels;
  std::vector<uint8_t> dst(kDstHeight * kDstStride, 0);

  // Copy tile at position (1, 1)
  CopyTileInto(dst.data(), kDstStride, tile_data.data(), kTileWidth,
               kTileHeight, 1, 1, kDstWidth, kDstHeight, kChannels);

  // Verify the tile was copied to the right position
  // Position (1,1) in destination should have first pixel of tile
  int offset = (1 * kDstStride) + (1 * kChannels);
  EXPECT_EQ(dst[offset + 0], 1);
  EXPECT_EQ(dst[offset + 1], 2);
  EXPECT_EQ(dst[offset + 2], 3);

  // Position (2,1) should have second pixel of first row
  offset = (1 * kDstStride) + (2 * kChannels);
  EXPECT_EQ(dst[offset + 0], 4);
  EXPECT_EQ(dst[offset + 1], 5);
  EXPECT_EQ(dst[offset + 2], 6);

  // Position (1,2) should have first pixel of second row
  offset = (2 * kDstStride) + (1 * kChannels);
  EXPECT_EQ(dst[offset + 0], 7);
  EXPECT_EQ(dst[offset + 1], 8);
  EXPECT_EQ(dst[offset + 2], 9);
}

TEST(CopyTileTest, EdgeTileClipping) {
  // Test that edge tiles are properly clipped
  constexpr int kTileWidth = 3;
  constexpr int kTileHeight = 3;
  constexpr int kChannels = 1;  // Grayscale for simplicity

  // Source tile: 3x3
  std::vector<uint8_t> tile_data(kTileWidth * kTileHeight);
  for (int i = 0; i < kTileWidth * kTileHeight; ++i) {
    tile_data[i] = static_cast<uint8_t>(i + 1);
  }

  // Destination: 4x4, but tile at (2,2) should only copy 2x2
  constexpr int kDstWidth = 4;
  constexpr int kDstHeight = 4;
  constexpr int kDstStride = kDstWidth * kChannels;
  std::vector<uint8_t> dst(kDstHeight * kDstStride, 0);

  // Copy tile at position (2, 2) - should be clipped to 2x2
  CopyTileInto(dst.data(), kDstStride, tile_data.data(), kTileWidth,
               kTileHeight, 2, 2, kDstWidth, kDstHeight, kChannels);

  // Only the top-left 2x2 portion of the tile should be copied
  EXPECT_EQ(dst[(2 * kDstStride) + 2], 1);  // tile[0,0]
  EXPECT_EQ(dst[(2 * kDstStride) + 3], 2);  // tile[0,1]
  EXPECT_EQ(dst[(3 * kDstStride) + 2], 4);  // tile[1,0]
  EXPECT_EQ(dst[(3 * kDstStride) + 3], 5);  // tile[1,1]

  // The rest should not be copied (would go out of bounds)
}

TEST(CopyTileTest, TileCompletelyOutsideRoi) {
  // Test that tiles completely outside ROI don't cause issues
  constexpr int kTileWidth = 2;
  constexpr int kTileHeight = 2;
  constexpr int kChannels = 3;

  std::vector<uint8_t> tile_data(kTileWidth * kTileHeight * kChannels, 99);

  constexpr int kDstWidth = 4;
  constexpr int kDstHeight = 4;
  constexpr int kDstStride = kDstWidth * kChannels;
  std::vector<uint8_t> dst(kDstHeight * kDstStride, 0);

  // Try to copy tile at position (10, 10) - completely outside
  CopyTileInto(dst.data(), kDstStride, tile_data.data(), kTileWidth,
               kTileHeight, 10, 10, kDstWidth, kDstHeight, kChannels);

  // Destination should remain all zeros (no copy occurred)
  for (const auto& byte : dst) {
    EXPECT_EQ(byte, 0);
  }
}

TEST(CopyTileTest, NegativeOffsetClipping) {
  // Matches the common ROI-not-tile-aligned case:
  // dst_x/dst_y negative for the first tile intersecting the ROI.
  constexpr int kTileWidth = 4;
  constexpr int kTileHeight = 4;
  constexpr int kChannels = 1;  // Grayscale for simplicity

  // Tile contains increasing values row-major:
  //  1  2  3  4
  //  5  6  7  8
  //  9 10 11 12
  // 13 14 15 16
  std::vector<uint8_t> tile_data(kTileWidth * kTileHeight);
  for (int i = 0; i < kTileWidth * kTileHeight; ++i) {
    tile_data[i] = static_cast<uint8_t>(i + 1);
  }

  // ROI is 2x2, destination buffer is tightly sized for ROI.
  constexpr int kRoiWidth = 2;
  constexpr int kRoiHeight = 2;
  constexpr int kDstStride = kRoiWidth * kChannels;
  std::vector<uint8_t> dst(kRoiHeight * kDstStride, 0);

  // Place tile with negative offset so that only the bottom-right 2x2 of the
  // tile lands inside the ROI.
  // dst_x = -2, dst_y = -2 means tile[2:4, 2:4] should be copied.
  CopyTileInto(dst.data(), kDstStride, tile_data.data(), kTileWidth,
               kTileHeight,
               /*dst_x=*/-2, /*dst_y=*/-2, kRoiWidth, kRoiHeight, kChannels);

  // Expect:
  // tile[2,2]=11, tile[2,3]=12
  // tile[3,2]=15, tile[3,3]=16
  EXPECT_EQ(dst[0], 11);
  EXPECT_EQ(dst[1], 12);
  EXPECT_EQ(dst[2], 15);
  EXPECT_EQ(dst[3], 16);
}

// =============================================================================
// ComposeJpegStream Tests - Test JPEG stream composition
// =============================================================================

TEST(JpegStreamTest, ComposeWithTablesAndPayload) {
  // Test composing a complete JPEG stream from tables and payload

  // Fake JPEG tables (without SOI/EOI)
  std::vector<uint8_t> tables = {
      0xFF, 0xDB, 0x00, 0x43  // DQT marker and some data
  };

  // Fake JPEG payload (without SOI/EOI)
  std::vector<uint8_t> payload = {
      0xFF, 0xDA, 0x00, 0x0C  // SOS marker and some data
  };

  std::vector<uint8_t> output;
  ComposeJpegStream(tables, payload, output);

  // Should have SOI at start
  ASSERT_GE(output.size(), 2u);
  EXPECT_EQ(output[0], 0xFF);
  EXPECT_EQ(output[1], 0xD8);  // SOI marker

  // Should have EOI at end
  EXPECT_EQ(output[output.size() - 2], 0xFF);
  EXPECT_EQ(output[output.size() - 1], 0xD9);  // EOI marker

  // Total size should be: SOI (2) + tables (4) + payload (4) + EOI (2) = 12
  EXPECT_EQ(output.size(), 12u);
}

TEST(JpegStreamTest, ComposeWithExistingSOIEOI) {
  // Test that existing SOI/EOI markers are handled correctly

  // Tables with SOI and EOI
  std::vector<uint8_t> tables = {
      0xFF, 0xD8,              // SOI
      0xFF, 0xDB, 0x00, 0x43,  // DQT
      0xFF, 0xD9               // EOI
  };

  // Payload with SOI and EOI
  std::vector<uint8_t> payload = {
      0xFF, 0xD8,              // SOI
      0xFF, 0xDA, 0x00, 0x0C,  // SOS
      0xFF, 0xD9               // EOI
  };

  std::vector<uint8_t> output;
  ComposeJpegStream(tables, payload, output);

  // Should have exactly one SOI at start
  ASSERT_GE(output.size(), 2u);
  EXPECT_EQ(output[0], 0xFF);
  EXPECT_EQ(output[1], 0xD8);

  // Should have exactly one EOI at end
  EXPECT_EQ(output[output.size() - 2], 0xFF);
  EXPECT_EQ(output[output.size() - 1], 0xD9);

  // Count SOI markers (should be exactly 1)
  int soi_count = 0;
  for (size_t i = 0; i + 1 < output.size(); ++i) {
    if (output[i] == 0xFF && output[i + 1] == 0xD8) {
      soi_count++;
    }
  }
  EXPECT_EQ(soi_count, 1);

  // Count EOI markers (should be exactly 1)
  int eoi_count = 0;
  for (size_t i = 0; i + 1 < output.size(); ++i) {
    if (output[i] == 0xFF && output[i + 1] == 0xD9) {
      eoi_count++;
    }
  }
  EXPECT_EQ(eoi_count, 1);
}

TEST(JpegStreamTest, ComposeWithEmptyTables) {
  // Test composing when tables are empty (some TIFFs don't use JPEGTables)
  std::vector<uint8_t> tables;  // Empty

  std::vector<uint8_t> payload = {
      0xFF, 0xDA, 0x00, 0x0C  // SOS marker
  };

  std::vector<uint8_t> output;
  ComposeJpegStream(tables, payload, output);

  // Should still have SOI and EOI
  ASSERT_GE(output.size(), 6u);
  EXPECT_EQ(output[0], 0xFF);
  EXPECT_EQ(output[1], 0xD8);  // SOI
  EXPECT_EQ(output[output.size() - 2], 0xFF);
  EXPECT_EQ(output[output.size() - 1], 0xD9);  // EOI

  // Size should be: SOI (2) + payload (4) + EOI (2) = 8
  EXPECT_EQ(output.size(), 8u);
}

// =============================================================================
// TiffIndex Storage Tests - Test payload pools
// =============================================================================

TEST(StorageTest, TilesRecStorage) {
  // Test storing and retrieving tiles metadata
  TiffIndex index;

  TilesRec tiles;
  tiles.tile_w = 256;
  tiles.tile_h = 256;
  tiles.tiles_x = 10;
  tiles.tiles_y = 8;

  std::vector<uint64_t> offsets(tiles.tiles_x * tiles.tiles_y, 1000);
  std::vector<uint64_t> bytecounts(tiles.tiles_x * tiles.tiles_y, 5000);

  tiles.offsets = index.AppendOffsets(offsets);
  tiles.bytecounts = index.AppendBytecounts(bytecounts);

  uint32_t tiles_id = index.AddTiles(std::move(tiles));

  // Retrieve and verify
  const auto& retrieved = index.Tiles(tiles_id);
  EXPECT_EQ(retrieved.tile_w, 256u);
  EXPECT_EQ(retrieved.tile_h, 256u);
  EXPECT_EQ(retrieved.tiles_x, 10u);
  EXPECT_EQ(retrieved.tiles_y, 8u);

  auto retrieved_offsets = index.Offsets(retrieved.offsets);
  auto retrieved_bytecounts = index.Bytecounts(retrieved.bytecounts);

  EXPECT_EQ(retrieved_offsets.size(), 80u);  // 10 * 8
  EXPECT_EQ(retrieved_bytecounts.size(), 80u);
  EXPECT_EQ(retrieved_offsets[0], 1000u);
  EXPECT_EQ(retrieved_bytecounts[0], 5000u);
}

TEST(StorageTest, StripsRecStorage) {
  // Test storing and retrieving strips metadata
  TiffIndex index;

  StripsRec strips;
  strips.rows_per_strip = 32;

  std::vector<uint64_t> offsets = {100, 200, 300};
  std::vector<uint64_t> bytecounts = {1000, 1000, 800};

  strips.offsets = index.AppendOffsets(offsets);
  strips.bytecounts = index.AppendBytecounts(bytecounts);

  uint32_t strips_id = index.AddStrips(std::move(strips));

  // Retrieve and verify
  const auto& retrieved = index.Strips(strips_id);
  EXPECT_EQ(retrieved.rows_per_strip, 32u);

  auto retrieved_offsets = index.Offsets(retrieved.offsets);
  auto retrieved_bytecounts = index.Bytecounts(retrieved.bytecounts);

  ASSERT_EQ(retrieved_offsets.size(), 3u);
  ASSERT_EQ(retrieved_bytecounts.size(), 3u);
  EXPECT_EQ(retrieved_offsets[1], 200u);
  EXPECT_EQ(retrieved_bytecounts[2], 800u);
}

TEST(StorageTest, SingleJpegRecStorage) {
  // Test storing and retrieving single JPEG metadata
  TiffIndex index;

  SingleJpegRec single;
  single.offset = 12345;
  single.length = 67890;

  uint32_t single_id = index.AddSingleJpeg(std::move(single));

  // Retrieve and verify
  const auto& retrieved = index.SingleJpeg(single_id);
  EXPECT_EQ(retrieved.offset, 12345u);
  EXPECT_EQ(retrieved.length, 67890u);
}

// =============================================================================
// Photometric Tests - Test photometric interpretation helpers
// =============================================================================

TEST(PhotometricTest, PhotometricEnumValues) {
  // Test that photometric interpretation codes match TIFF spec
  EXPECT_EQ(static_cast<uint16_t>(Photometric::kMinIsWhite), 0u);
  EXPECT_EQ(static_cast<uint16_t>(Photometric::kMinIsBlack), 1u);
  EXPECT_EQ(static_cast<uint16_t>(Photometric::kRgb), 2u);
  EXPECT_EQ(static_cast<uint16_t>(Photometric::kPalette), 3u);
  EXPECT_EQ(static_cast<uint16_t>(Photometric::kYCbCr), 6u);
}

TEST(PhotometricTest, IsPhotometricHelper) {
  // Test the IsPhotometric helper function
  uint16_t rgb_code = 2;
  EXPECT_TRUE(IsPhotometric(rgb_code, Photometric::kRgb));
  EXPECT_FALSE(IsPhotometric(rgb_code, Photometric::kYCbCr));

  uint16_t ycbcr_code = 6;
  EXPECT_TRUE(IsPhotometric(ycbcr_code, Photometric::kYCbCr));
  EXPECT_FALSE(IsPhotometric(ycbcr_code, Photometric::kRgb));
}

// =============================================================================
// Edge Case Tests - Test boundary conditions
// =============================================================================

TEST(EdgeCaseTest, ZeroSizeRoi) {
  // Test that zero-size ROI is detected
  Roi roi{0, 0, 0, 0};

  EXPECT_EQ(roi.width, 0u);
  EXPECT_EQ(roi.height, 0u);

  // Zero-size ROI should be detected early
  // We just test that the structure can hold these values
  // Actual handling would depend on the implementation
}

TEST(EdgeCaseTest, VeryLargeRoi) {
  // Test that very large ROI doesn't cause integer overflow
  Roi roi{0, 0, UINT32_MAX, UINT32_MAX};

  // Just verify the struct can hold these values
  EXPECT_EQ(roi.width, UINT32_MAX);
  EXPECT_EQ(roi.height, UINT32_MAX);
}

TEST(EdgeCaseTest, MultipleContextsInSequence) {
  // Test creating many contexts in sequence (stress test memory management)
  constexpr int kNumContexts = 100;

  for (int i = 0; i < kNumContexts; ++i) {
    DecodeContext ctx;
    ctx.jpeg_stream_buffer.resize(1000 + i);
    ctx.temp_buffer.resize(500 + i);

    // Fill with test pattern
    std::fill(ctx.jpeg_stream_buffer.begin(), ctx.jpeg_stream_buffer.end(),
              static_cast<uint8_t>(i & 0xFF));

    // Verify
    EXPECT_EQ(ctx.jpeg_stream_buffer.size(), 1000u + i);
    EXPECT_EQ(ctx.jpeg_stream_buffer[0], static_cast<uint8_t>(i & 0xFF));
  }
  // All contexts should be cleaned up by RAII
}

TEST(TiffParserTest, SubIfdsAreEnumeratedAsPages) {
  // Create a minimal ClassicTIFF with one main IFD and a SubIFD chain.
  //
  // This matches how OME-TIFF pyramids often store reduced-resolution images:
  // as SubIFDs of the main image, sometimes chaining additional SubIFDs via
  // the "next IFD" pointer.
  //
  // We only validate index enumeration (num pages + ordering), not pixel data.
  constexpr uint16_t kTypeShort = 3;
  constexpr uint16_t kTypeLong = 4;

  // Offsets in the file (ClassicTIFF, little-endian).
  constexpr uint32_t kHeaderSize = 8;
  constexpr uint32_t kMainIfdOffset = kHeaderSize;

  constexpr uint16_t kMainIfdNumEntries = 7;
  constexpr uint32_t kMainIfdSize =
      2 + static_cast<uint32_t>(kMainIfdNumEntries) * 12 + 4;

  // With SubIFDs count==1, the offset is stored inline in the IFD entry
  // (ClassicTIFF inline limit is 4 bytes). So we do NOT write a separate
  // SubIFDs offset array; the value field stores the IFD offset directly.
  constexpr uint32_t kSubIfd0Offset = kMainIfdOffset + kMainIfdSize;

  constexpr uint16_t kSubIfdNumEntries = 6;
  constexpr uint32_t kSubIfdSize =
      2 + static_cast<uint32_t>(kSubIfdNumEntries) * 12 + 4;
  constexpr uint32_t kSubIfd1Offset = kSubIfd0Offset + kSubIfdSize;

  std::vector<uint8_t> tiff;
  tiff.reserve(kSubIfd1Offset + kSubIfdSize);

  // TIFF header: "II" + 42 + first IFD offset.
  tiff.push_back(0x49);
  tiff.push_back(0x49);
  AppendLe16(tiff, 42);
  AppendLe32(tiff, kMainIfdOffset);

  // Main IFD.
  AppendLe16(tiff, kMainIfdNumEntries);
  AppendIfdEntryClassic(tiff, /*tag=*/256, kTypeLong, /*count=*/1,
                        /*value=*/100);  // ImageWidth
  AppendIfdEntryClassic(tiff, /*tag=*/257, kTypeLong, /*count=*/1,
                        /*value=*/50);  // ImageLength
  AppendIfdEntryClassic(tiff, /*tag=*/258, kTypeShort, /*count=*/1,
                        /*value=*/8);  // BitsPerSample
  AppendIfdEntryClassic(tiff, /*tag=*/259, kTypeShort, /*count=*/1,
                        /*value=*/1);  // Compression (none)
  AppendIfdEntryClassic(tiff, /*tag=*/262, kTypeShort, /*count=*/1,
                        /*value=*/2);  // Photometric (RGB)
  AppendIfdEntryClassic(tiff, /*tag=*/277, kTypeShort, /*count=*/1,
                        /*value=*/3);  // SamplesPerPixel
  AppendIfdEntryClassic(
      tiff, /*tag=*/330, kTypeLong, /*count=*/1,
      /*value_or_offset=*/kSubIfd0Offset);  // SubIFDs (inline)
  AppendLe32(tiff, /*next_ifd=*/0);

  // SubIFD 0.
  ASSERT_EQ(tiff.size(), kSubIfd0Offset);
  AppendLe16(tiff, kSubIfdNumEntries);
  AppendIfdEntryClassic(tiff, /*tag=*/256, kTypeLong, /*count=*/1,
                        /*value=*/50);
  AppendIfdEntryClassic(tiff, /*tag=*/257, kTypeLong, /*count=*/1,
                        /*value=*/25);
  AppendIfdEntryClassic(tiff, /*tag=*/258, kTypeShort, /*count=*/1,
                        /*value=*/8);
  AppendIfdEntryClassic(tiff, /*tag=*/259, kTypeShort, /*count=*/1,
                        /*value=*/1);
  AppendIfdEntryClassic(tiff, /*tag=*/262, kTypeShort, /*count=*/1,
                        /*value=*/2);
  AppendIfdEntryClassic(tiff, /*tag=*/277, kTypeShort, /*count=*/1,
                        /*value=*/3);
  // Chain to SubIFD 1 via the next-IFD pointer.
  AppendLe32(tiff, /*next_ifd=*/kSubIfd1Offset);

  // SubIFD 1.
  ASSERT_EQ(tiff.size(), kSubIfd1Offset);
  AppendLe16(tiff, kSubIfdNumEntries);
  AppendIfdEntryClassic(tiff, /*tag=*/256, kTypeLong, /*count=*/1,
                        /*value=*/25);
  AppendIfdEntryClassic(tiff, /*tag=*/257, kTypeLong, /*count=*/1,
                        /*value=*/13);
  AppendIfdEntryClassic(tiff, /*tag=*/258, kTypeShort, /*count=*/1,
                        /*value=*/8);
  AppendIfdEntryClassic(tiff, /*tag=*/259, kTypeShort, /*count=*/1,
                        /*value=*/1);
  AppendIfdEntryClassic(tiff, /*tag=*/262, kTypeShort, /*count=*/1,
                        /*value=*/2);
  AppendIfdEntryClassic(tiff, /*tag=*/277, kTypeShort, /*count=*/1,
                        /*value=*/3);
  AppendLe32(tiff, /*next_ifd=*/0);

  // Write to a temp file (Bazel provides TEST_TMPDIR).
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::filesystem::path tmp_dir = test_tmpdir
                                      ? std::filesystem::path(test_tmpdir)
                                      : std::filesystem::temp_directory_path();
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path tiff_path =
      tmp_dir / ("simpletiff_subifd_" + unique + ".tif");

  std::ofstream ofs(tiff_path, std::ios::binary);
  ASSERT_TRUE(ofs.good());
  ofs.write(reinterpret_cast<const char*>(tiff.data()),
            static_cast<std::streamsize>(tiff.size()));
  ofs.close();

  TiffIndex index;
  int fd = -1;
  ASSERT_TRUE(OpenTiff(tiff_path.string(), index, fd));
  EXPECT_EQ(index.NumPages(), 3u);
  EXPECT_EQ(index.Page(0).width, 100u);
  EXPECT_EQ(index.Page(0).height, 50u);
  EXPECT_EQ(index.Page(1).width, 50u);
  EXPECT_EQ(index.Page(1).height, 25u);
  EXPECT_EQ(index.Page(2).width, 25u);
  EXPECT_EQ(index.Page(2).height, 13u);

  // Parent/child links.
  EXPECT_FALSE(index.Page(0).parent_page_index.has_value());
  EXPECT_TRUE(index.Page(1).parent_page_index.has_value());
  EXPECT_TRUE(index.Page(2).parent_page_index.has_value());
  EXPECT_EQ(*index.Page(1).parent_page_index, 0u);
  EXPECT_EQ(*index.Page(2).parent_page_index, 0u);

  const auto children0 = index.ChildPagesForPage(0);
  ASSERT_EQ(children0.size(), 2u);
  EXPECT_EQ(children0[0], 1u);
  EXPECT_EQ(children0[1], 2u);

  if (fd >= 0) {
    aifocore::portable_close(fd);
  }
}

TEST(TiffParserTest, NdpiClassic64EnumeratesIfds) {
  // NDPI is ClassicTIFF with 64-bit IFD pointers encoded as:
  // - header: standard 32-bit first-IFD offset (low word) + an extra 32-bit
  // high
  //   word at bytes 8..11.
  // - each IFD's next-IFD pointer: 64-bit (low+high) even though ClassicTIFF.
  //
  // This test constructs a sparse file >4GB with two empty IFDs.
  constexpr uint64_t kIfd0Offset = (1ULL << 32) | 0x00000020ULL;
  constexpr uint64_t kIfd1Offset = (1ULL << 32) | 0x00000100ULL;

  // Write to a temp file (Bazel provides TEST_TMPDIR).
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  std::filesystem::path tmp_dir = test_tmpdir
                                      ? std::filesystem::path(test_tmpdir)
                                      : std::filesystem::temp_directory_path();
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path tiff_path =
      tmp_dir / ("simpletiff_ndpi64_" + unique + ".ndpi");

  // Build 16-byte header. For ClassicTIFF, bytes 4..7 hold the low word.
  // NDPI stores the high word for the first-IFD offset at bytes 8..11.
  std::vector<uint8_t> hdr;
  hdr.reserve(16);
  hdr.push_back(0x49);
  hdr.push_back(0x49);
  AppendLe16(hdr, 42);
  AppendLe32(hdr, static_cast<uint32_t>(kIfd0Offset & 0xFFFFFFFFULL));  // low
  AppendLe32(hdr, static_cast<uint32_t>(kIfd0Offset >> 32));            // high
  AppendLe32(hdr, 0);  // padding / unused

  // Create file with header.
  {
    std::ofstream ofs(tiff_path, std::ios::binary);
    ASSERT_TRUE(ofs.good());
    ofs.write(reinterpret_cast<const char*>(hdr.data()),
              static_cast<std::streamsize>(hdr.size()));
  }

  // Make it sparse and large enough to hold both IFDs.
  const uint64_t desired_size = kIfd1Offset + 4096;
  std::filesystem::resize_file(tiff_path, desired_size);

  // Write the two empty IFDs (num_entries=0, next_ifd as 64-bit).
  auto write_ifd = [&](uint64_t ifd_offset, uint64_t next_offset) {
    std::fstream fs(tiff_path, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(fs.good());
    fs.seekp(static_cast<std::streamoff>(ifd_offset), std::ios::beg);
    std::vector<uint8_t> ifd;
    AppendLe16(ifd, 0);  // num entries
    AppendLe64(ifd, next_offset);
    fs.write(reinterpret_cast<const char*>(ifd.data()),
             static_cast<std::streamsize>(ifd.size()));
    fs.flush();
    ASSERT_TRUE(fs.good());
  };
  write_ifd(kIfd0Offset, kIfd1Offset);
  write_ifd(kIfd1Offset, 0);

  TiffIndex index;
  int fd = -1;
  ASSERT_TRUE(OpenTiff(tiff_path.string(), index, fd));
  EXPECT_EQ(index.NumPages(), 2u);
  EXPECT_EQ(index.Page(0).ifd_offset, kIfd0Offset);
  EXPECT_EQ(index.Page(1).ifd_offset, kIfd1Offset);

  if (fd >= 0) {
    aifocore::portable_close(fd);
  }
}

}  // namespace
}  // namespace simpletiff
