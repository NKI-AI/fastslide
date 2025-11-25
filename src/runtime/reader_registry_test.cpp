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

#include "fastslide/runtime/reader_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "fastslide/runtime/format_descriptor.h"
#include "fastslide/slide_reader.h"

namespace fastslide {
namespace runtime {

// ============================================================================
// Mock Format Descriptors for Testing
// ============================================================================

/// @brief Create a mock MRXS format descriptor
FormatDescriptor CreateMockMrxsDescriptor() {
  FormatDescriptor desc;
  desc.format_name = "MRXS";
  desc.primary_extension = ".mrxs";
  desc.aliases = {".slidedat.ini"};
  desc.capabilities = SetCapability(0, FormatCapability::kPyramidal);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kTiled);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kCompressed);
  desc.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock MRXS factory");
  };
  return desc;
}

/// @brief Create a mock QPTIFF format descriptor
FormatDescriptor CreateMockQptiffDescriptor() {
  FormatDescriptor desc;
  desc.format_name = "QPTIFF";
  desc.primary_extension = ".qptiff";
  desc.aliases = {".tif", ".tiff"};
  desc.capabilities = SetCapability(0, FormatCapability::kPyramidal);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kTiled);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kSpectral);
  desc.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock QPTIFF factory");
  };
  return desc;
}

/// @brief Create a mock SVS format descriptor
FormatDescriptor CreateMockSvsDescriptor() {
  FormatDescriptor desc;
  desc.format_name = "SVS";
  desc.primary_extension = ".svs";
  desc.capabilities = SetCapability(0, FormatCapability::kPyramidal);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kTiled);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kCompressed);
  desc.capabilities =
      SetCapability(desc.capabilities, FormatCapability::kAssociatedImages);
  desc.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock SVS factory");
  };
  return desc;
}

// ============================================================================
// ReaderRegistry Basic Tests
// ============================================================================

TEST(ReaderRegistryTest, EmptyRegistry) {
  ReaderRegistry registry;

  EXPECT_TRUE(registry.ListFormats().empty());
  EXPECT_TRUE(registry.GetSupportedExtensions().empty());
  EXPECT_FALSE(registry.SupportsExtension(".mrxs"));
  EXPECT_EQ(registry.GetFormat(".mrxs"), nullptr);
}

TEST(ReaderRegistryTest, RegisterSingleFormat) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());

  auto formats = registry.ListFormats();
  EXPECT_EQ(formats.size(), 1);
  EXPECT_EQ(formats[0], "MRXS");

  EXPECT_TRUE(registry.SupportsExtension(".mrxs"));
  EXPECT_FALSE(registry.SupportsExtension(".svs"));

  auto* format = registry.GetFormat(".mrxs");
  ASSERT_NE(format, nullptr);
  EXPECT_EQ(format->format_name, "MRXS");
  EXPECT_EQ(format->primary_extension, ".mrxs");
}

TEST(ReaderRegistryTest, RegisterMultipleFormats) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());
  registry.RegisterFormat(CreateMockQptiffDescriptor());
  registry.RegisterFormat(CreateMockSvsDescriptor());

  auto formats = registry.ListFormats();
  EXPECT_EQ(formats.size(), 3);

  // Check all formats are present (order not guaranteed)
  EXPECT_TRUE(std::find(formats.begin(), formats.end(), "MRXS") !=
              formats.end());
  EXPECT_TRUE(std::find(formats.begin(), formats.end(), "QPTIFF") !=
              formats.end());
  EXPECT_TRUE(std::find(formats.begin(), formats.end(), "SVS") !=
              formats.end());
}

TEST(ReaderRegistryTest, RegisterFormatsBulk) {
  ReaderRegistry registry;

  std::vector<FormatDescriptor> descriptors;
  descriptors.push_back(CreateMockMrxsDescriptor());
  descriptors.push_back(CreateMockQptiffDescriptor());
  descriptors.push_back(CreateMockSvsDescriptor());

  registry.RegisterFormats(std::move(descriptors));

  auto formats = registry.ListFormats();
  EXPECT_EQ(formats.size(), 3);
}

TEST(ReaderRegistryTest, ReplaceExistingFormat) {
  ReaderRegistry registry;

  // Create first MRXS descriptor without aliases
  FormatDescriptor mrxs1;
  mrxs1.format_name = "MRXS";
  mrxs1.primary_extension = ".mrxs";
  mrxs1.capabilities = SetCapability(0, FormatCapability::kPyramidal);
  mrxs1.factory = [](std::shared_ptr<ITileCache> cache,
                     std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock MRXS factory");
  };

  registry.RegisterFormat(mrxs1);

  EXPECT_EQ(registry.ListFormats().size(), 1);

  // Register again with same extension (should replace)
  FormatDescriptor mrxs2;
  mrxs2.format_name = "MRXS_V2";
  mrxs2.primary_extension = ".mrxs";
  mrxs2.capabilities = SetCapability(0, FormatCapability::kPyramidal);
  mrxs2.factory = [](std::shared_ptr<ITileCache> cache,
                     std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock MRXS V2 factory");
  };

  registry.RegisterFormat(mrxs2);

  // Still only 1 format
  EXPECT_EQ(registry.ListFormats().size(), 1);

  // But it's the new one
  auto* format = registry.GetFormat(".mrxs");
  ASSERT_NE(format, nullptr);
  EXPECT_EQ(format->format_name, "MRXS_V2");
}

TEST(ReaderRegistryTest, ClearRegistry) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());
  registry.RegisterFormat(CreateMockQptiffDescriptor());

  EXPECT_EQ(registry.ListFormats().size(), 2);

  registry.Clear();

  EXPECT_TRUE(registry.ListFormats().empty());
  EXPECT_FALSE(registry.SupportsExtension(".mrxs"));
}

// ============================================================================
// Extension Normalization Tests
// ============================================================================

TEST(ReaderRegistryTest, ExtensionNormalizationLowercase) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());

  // All these should work (case-insensitive, with/without leading dot)
  EXPECT_TRUE(registry.SupportsExtension(".mrxs"));
  EXPECT_TRUE(registry.SupportsExtension(".MRXS"));
  EXPECT_TRUE(registry.SupportsExtension("mrxs"));
  EXPECT_TRUE(registry.SupportsExtension("MRXS"));
  EXPECT_TRUE(registry.SupportsExtension(".Mrxs"));
}

TEST(ReaderRegistryTest, ExtensionNormalizationLeadingDot) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());

  auto* format1 = registry.GetFormat(".mrxs");
  auto* format2 = registry.GetFormat("mrxs");

  EXPECT_EQ(format1, format2);
  EXPECT_NE(format1, nullptr);
}

TEST(ReaderRegistryTest, GetSupportedExtensionsSorted) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockSvsDescriptor());

  // Create MRXS and QPTIFF without aliases to keep count simple
  FormatDescriptor mrxs;
  mrxs.format_name = "MRXS";
  mrxs.primary_extension = ".mrxs";
  mrxs.capabilities = SetCapability(0, FormatCapability::kPyramidal);
  mrxs.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock");
  };
  registry.RegisterFormat(mrxs);

  FormatDescriptor qptiff;
  qptiff.format_name = "QPTIFF";
  qptiff.primary_extension = ".qptiff";
  qptiff.capabilities = SetCapability(0, FormatCapability::kSpectral);
  qptiff.factory = [](std::shared_ptr<ITileCache> cache,
                      std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock");
  };
  registry.RegisterFormat(qptiff);

  auto extensions = registry.GetSupportedExtensions();

  // Should be sorted
  EXPECT_GT(extensions.size(), 0);
  EXPECT_TRUE(std::is_sorted(extensions.begin(), extensions.end()));

  // Should contain all primary extensions (normalized)
  EXPECT_TRUE(std::find(extensions.begin(), extensions.end(), ".mrxs") !=
              extensions.end());
  EXPECT_TRUE(std::find(extensions.begin(), extensions.end(), ".qptiff") !=
              extensions.end());
  EXPECT_TRUE(std::find(extensions.begin(), extensions.end(), ".svs") !=
              extensions.end());
}

// ============================================================================
// Capability Tests
// ============================================================================

TEST(ReaderRegistryTest, SupportsCapability) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());
  registry.RegisterFormat(CreateMockQptiffDescriptor());

  // MRXS supports Compressed
  EXPECT_TRUE(
      registry.SupportsCapability(".mrxs", FormatCapability::kCompressed));
  EXPECT_FALSE(
      registry.SupportsCapability(".mrxs", FormatCapability::kSpectral));

  // QPTIFF supports Spectral
  EXPECT_TRUE(
      registry.SupportsCapability(".qptiff", FormatCapability::kSpectral));
  EXPECT_FALSE(
      registry.SupportsCapability(".qptiff", FormatCapability::kCompressed));

  // Unknown extension returns false
  EXPECT_FALSE(
      registry.SupportsCapability(".unknown", FormatCapability::kPyramidal));
}

TEST(ReaderRegistryTest, ListFormatsByCapability) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());
  registry.RegisterFormat(CreateMockQptiffDescriptor());
  registry.RegisterFormat(CreateMockSvsDescriptor());

  // All support Pyramidal
  auto pyramidal =
      registry.ListFormatsByCapability(FormatCapability::kPyramidal);
  EXPECT_EQ(pyramidal.size(), 3);

  // MRXS and SVS support Compressed
  auto compressed =
      registry.ListFormatsByCapability(FormatCapability::kCompressed);
  EXPECT_EQ(compressed.size(), 2);
  EXPECT_TRUE(std::find(compressed.begin(), compressed.end(), "MRXS") !=
              compressed.end());
  EXPECT_TRUE(std::find(compressed.begin(), compressed.end(), "SVS") !=
              compressed.end());

  // Only QPTIFF supports Spectral
  auto spectral = registry.ListFormatsByCapability(FormatCapability::kSpectral);
  EXPECT_EQ(spectral.size(), 1);
  EXPECT_EQ(spectral[0], "QPTIFF");

  // Only SVS has associated images
  auto assoc =
      registry.ListFormatsByCapability(FormatCapability::kAssociatedImages);
  EXPECT_EQ(assoc.size(), 1);
  EXPECT_EQ(assoc[0], "SVS");

  // Check capability that isn't set on any format
  auto random_access =
      registry.ListFormatsByCapability(FormatCapability::kRandomAccess);
  EXPECT_TRUE(random_access.empty());
}

TEST(ReaderRegistryTest, MultipleCapabilityCheck) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());

  auto* format = registry.GetFormat(".mrxs");
  ASSERT_NE(format, nullptr);

  // Check multiple capabilities
  EXPECT_TRUE(
      registry.SupportsCapability(".mrxs", FormatCapability::kPyramidal));
  EXPECT_TRUE(registry.SupportsCapability(".mrxs", FormatCapability::kTiled));
  EXPECT_TRUE(
      registry.SupportsCapability(".mrxs", FormatCapability::kCompressed));
}

// ============================================================================
// Reader Creation Tests
// ============================================================================

TEST(ReaderRegistryTest, CreateReaderUnsupportedExtension) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());

  auto result = registry.CreateReader("slide.unknown");

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  // Message should contain "No reader registered"
  EXPECT_NE(result.status().message().find("No reader registered"),
            std::string::npos);
}

TEST(ReaderRegistryTest, CreateReaderWithExtension) {
  ReaderRegistry registry;

  // Create a working factory
  FormatDescriptor desc;
  desc.format_name = "TestFormat";
  desc.primary_extension = ".test";
  desc.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    // Return error to verify factory was called
    return absl::InternalError("Test factory called for: " +
                               std::string(filename));
  };

  registry.RegisterFormat(desc);

  auto result = registry.CreateReader("slide.test");

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  EXPECT_NE(result.status().message().find("slide.test"), std::string::npos);
}

TEST(ReaderRegistryTest, CreateReaderCaseInsensitiveExtension) {
  ReaderRegistry registry;

  FormatDescriptor desc;
  desc.format_name = "TestFormat";
  desc.primary_extension = ".test";
  desc.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::InternalError("Factory called");
  };

  registry.RegisterFormat(desc);

  // All these should trigger the factory
  auto result1 = registry.CreateReader("slide.TEST");
  auto result2 = registry.CreateReader("slide.Test");
  auto result3 = registry.CreateReader("slide.test");

  EXPECT_FALSE(result1.ok());
  EXPECT_FALSE(result2.ok());
  EXPECT_FALSE(result3.ok());

  // All should have called the factory
  EXPECT_NE(result1.status().message().find("Factory called"),
            std::string::npos);
  EXPECT_NE(result2.status().message().find("Factory called"),
            std::string::npos);
  EXPECT_NE(result3.status().message().find("Factory called"),
            std::string::npos);
}

TEST(ReaderRegistryTest, CreateReaderWithNullCache) {
  ReaderRegistry registry;

  // Track whether nullptr was passed correctly
  bool null_cache_received = false;

  FormatDescriptor desc;
  desc.format_name = "TestFormat";
  desc.primary_extension = ".test";
  desc.factory = [&null_cache_received](std::shared_ptr<ITileCache> cache,
                                        std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    // Verify nullptr cache was passed
    if (cache == nullptr) {
      null_cache_received = true;
    }
    return absl::InternalError("Test");
  };

  registry.RegisterFormat(desc);

  // Pass nullptr cache (default parameter)
  auto result = registry.CreateReader("slide.test");

  EXPECT_TRUE(null_cache_received);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(ReaderRegistryTest, ConcurrentRegistration) {
  ReaderRegistry registry;

  // Register formats concurrently from multiple threads
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&registry, i]() {
      FormatDescriptor desc;
      desc.format_name = "Format" + std::to_string(i);
      desc.primary_extension = ".fmt" + std::to_string(i);
      desc.capabilities = SetCapability(0, FormatCapability::kPyramidal);
      desc.factory = [](std::shared_ptr<ITileCache> cache,
                        std::string_view filename)
          -> absl::StatusOr<std::unique_ptr<SlideReader>> {
        return absl::UnimplementedError("Mock");
      };
      registry.RegisterFormat(desc);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Should have all 10 formats
  EXPECT_EQ(registry.ListFormats().size(), 10);
}

TEST(ReaderRegistryTest, ConcurrentAccess) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());
  registry.RegisterFormat(CreateMockQptiffDescriptor());

  // Access registry concurrently from multiple threads
  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int i = 0; i < 100; ++i) {
    threads.emplace_back([&registry, &success_count]() {
      if (registry.SupportsExtension(".mrxs")) {
        success_count++;
      }
      auto formats = registry.ListFormats();
      if (formats.size() == 2) {
        success_count++;
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All threads should have succeeded
  EXPECT_EQ(success_count, 200);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ReaderRegistryTest, EmptyExtension) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());

  EXPECT_FALSE(registry.SupportsExtension(""));
  EXPECT_EQ(registry.GetFormat(""), nullptr);
}

TEST(ReaderRegistryTest, MultiDotExtension) {
  ReaderRegistry registry;

  FormatDescriptor desc;
  desc.format_name = "Archive";
  desc.primary_extension = ".tar.gz";
  desc.capabilities = 0;
  desc.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::UnimplementedError("Mock");
  };

  registry.RegisterFormat(desc);

  EXPECT_TRUE(registry.SupportsExtension(".tar.gz"));
  EXPECT_TRUE(registry.SupportsExtension("tar.gz"));
  EXPECT_TRUE(registry.SupportsExtension(".TAR.GZ"));
}

TEST(ReaderRegistryTest, FilenameWithPath) {
  ReaderRegistry registry;

  FormatDescriptor desc;
  desc.format_name = "TestFormat";
  desc.primary_extension = ".test";
  desc.factory = [](std::shared_ptr<ITileCache> cache,
                    std::string_view filename)
      -> absl::StatusOr<std::unique_ptr<SlideReader>> {
    return absl::InternalError("Called with: " + std::string(filename));
  };

  registry.RegisterFormat(desc);

  auto result = registry.CreateReader("/path/to/slide.test");

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("/path/to/slide.test"),
            std::string::npos);
}

TEST(ReaderRegistryTest, FilenameWithMultipleDots) {
  ReaderRegistry registry;
  registry.RegisterFormat(CreateMockMrxsDescriptor());

  auto result = registry.CreateReader("slide.backup.2024.mrxs");

  // Should extract .mrxs extension correctly
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().code(), absl::StatusCode::kNotFound);
}

}  // namespace runtime
}  // namespace fastslide
