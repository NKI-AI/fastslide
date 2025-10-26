Adding a New Reader
===================

This guide walks through implementing support for a new whole slide image format in FastSlide.

.. raw:: html

   <div class="api-section">
       <h3>🔧 Step-by-Step Implementation Guide</h3>
       <p>Learn how to extend FastSlide with support for new slide formats</p>
   </div>

Overview
--------

Adding a new reader involves:

1. **Analyze the format** specification and sample files
2. **Create reader class** inheriting from ``TiffBasedReader`` or ``SlideReader``
3. **Implement core methods** for metadata parsing and tile reading
4. **Add format registration** to the plugin system
5. **Write comprehensive tests** with sample files
6. **Update documentation** and examples

Prerequisites
-------------

- Understanding of the file format structure
- Sample files for testing and validation
- C++20 development environment set up
- Basic understanding of FastSlide architecture

Step 1: Analyze the Format
--------------------------

Before implementing, thoroughly understand your format:

**File Structure Analysis**
   - Is it TIFF-based or completely custom?
   - How are pyramid levels organized?
   - Where is metadata stored?
   - How are tiles encoded and compressed?

**Create Analysis Document**
   
.. code-block:: cpp

   // Create: src/readers/myformat/format_analysis.md
   /*
   MyFormat File Structure Analysis
   ===============================
   
   File Extension: .myf
   MIME Type: application/x-myformat
   
   Structure:
   - Header: 256 bytes (format signature, version, metadata offset)
   - Metadata: JSON or XML (slide properties, level info) 
   - Pyramid Levels: Multiple resolution levels
   - Tile Data: Compressed tiles (JPEG, PNG, or custom)
   
   Key Characteristics:
   - Multi-level pyramid: YES/NO
   - Associated images: YES/NO (thumbnails, labels)
   - Multi-channel: YES/NO
   - Compression: JPEG/PNG/LZW/Custom
   - Coordinate system: Top-left origin, micron-based
   */

Step 2: Create Reader Class Structure
-------------------------------------

Choose Your Base Class
~~~~~~~~~~~~~~~~~~~~~~

**Option A: TIFF-Based Format**

If your format is built on TIFF:

.. code-block:: cpp

   // include/fastslide/readers/myformat/myformat_reader.h
   #pragma once
   
   #include "fastslide/readers/tiff_based_reader.h"
   #include "fastslide/core/slide_descriptor.h"
   
   namespace fastslide {
   
   /**
    * @brief Reader for MyFormat (.myf) files
    * 
    * MyFormat is a TIFF-based format with custom metadata and 
    * multi-level pyramid structure.
    */
   class MyFormatReader : public TiffBasedReader {
   public:
       /**
        * @brief Create MyFormat reader from file path
        * @param file_path Path to .myf file
        * @return StatusOr containing reader instance or error
        */
       static absl::StatusOr<std::unique_ptr<MyFormatReader>> 
       Create(const std::string& file_path);
       
   protected:
       /**
        * @brief Validate file format and structure
        * @return Status indicating validation result
        */
       absl::Status ValidateFile() override;
       
       /**
        * @brief Load slide metadata and build level information
        * @return Status indicating success or failure
        */
       absl::Status LoadMetadata() override;
       
       /**
        * @brief Execute tile reading plan
        * @param plan Batch tile plan to execute
        * @return Image result or error status
        */
       absl::StatusOr<Image> ExecutePlan(
           const core::BatchTilePlan& plan) override;
       
   private:
       struct MyFormatMetadata {
           std::string version;
           std::vector<LevelInfo> levels;
           std::unordered_map<std::string, std::string> properties;
       };
       
       absl::Status ParseMyFormatHeader();
       absl::Status LoadMyFormatMetadata();
       absl::StatusOr<std::vector<uint8_t>> ReadTileData(
           int level, int tile_x, int tile_y);
       
       MyFormatMetadata metadata_;
       std::string file_path_;
   };
   
   } // namespace fastslide

**Option B: Custom Format (Non-TIFF)**

For completely custom formats:

.. code-block:: cpp

   // include/fastslide/readers/myformat/myformat_reader.h
   #pragma once
   
   #include "fastslide/slide_reader.h"
   
   namespace fastslide {
   
   class MyFormatReader : public SlideReader {
   public:
       static absl::StatusOr<std::unique_ptr<MyFormatReader>> 
       Create(const std::string& file_path);
       
       // Implement SlideReader interface
       absl::StatusOr<Image> ReadRegion(const RegionSpec& spec) override;
       SlideProperties GetProperties() const override;
       int GetLevelCount() const override;
       LevelInfo GetLevelInfo(int level) const override;
       // ... other required methods
       
   private:
       // Format-specific implementation details
   };
   
   } // namespace fastslide

Step 3: Implement Core Methods
------------------------------

File Validation
~~~~~~~~~~~~~~~

Always validate the file format first:

.. code-block:: cpp

   // src/readers/myformat/myformat_reader.cpp
   absl::Status MyFormatReader::ValidateFile() {
       std::ifstream file(file_path_, std::ios::binary);
       if (!file.is_open()) {
           return absl::NotFoundError("Cannot open file: " + file_path_);
       }
       
       // Read and validate format signature
       char signature[8];
       file.read(signature, sizeof(signature));
       
       if (std::memcmp(signature, "MYFORMAT", 8) != 0) {
           return absl::InvalidArgumentError("Invalid MyFormat signature");
       }
       
       // Validate version
       uint32_t version;
       file.read(reinterpret_cast<char*>(&version), sizeof(version));
       
       if (version < MIN_SUPPORTED_VERSION || version > MAX_SUPPORTED_VERSION) {
           return absl::InvalidArgumentError(absl::StrCat(
               "Unsupported version: ", version));
       }
       
       return absl::OkStatus();
   }

Metadata Loading
~~~~~~~~~~~~~~~~

Parse format-specific metadata:

.. code-block:: cpp

   absl::Status MyFormatReader::LoadMetadata() {
       std::ifstream file(file_path_, std::ios::binary);
       
       // Seek to metadata section
       uint64_t metadata_offset;
       file.seekg(16);  // After header
       file.read(reinterpret_cast<char*>(&metadata_offset), sizeof(metadata_offset));
       file.seekg(metadata_offset);
       
       // Read metadata size
       uint32_t metadata_size;
       file.read(reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size));
       
       // Read metadata content
       std::vector<char> metadata_buffer(metadata_size);
       file.read(metadata_buffer.data(), metadata_size);
       
       // Parse metadata (JSON example)
       try {
           json metadata_json = json::parse(metadata_buffer.begin(), 
                                           metadata_buffer.end());
           
           // Extract slide properties
           metadata_.version = metadata_json["version"];
           
           // Build level information
           for (const auto& level_json : metadata_json["levels"]) {
               LevelInfo level_info{
                   .width = level_json["width"],
                   .height = level_json["height"],
                   .downsample = level_json["downsample"],
                   .tile_width = level_json["tile_width"],
                   .tile_height = level_json["tile_height"]
               };
               metadata_.levels.push_back(level_info);
           }
           
           // Store properties
           for (const auto& [key, value] : metadata_json["properties"].items()) {
               metadata_.properties[key] = value;
           }
           
       } catch (const json::exception& e) {
           return absl::InvalidArgumentError(
               absl::StrCat("Failed to parse metadata: ", e.what()));
       }
       
       return absl::OkStatus();
   }

Tile Reading Implementation
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Implement the core tile reading logic:

.. code-block:: cpp

   absl::StatusOr<Image> MyFormatReader::ExecutePlan(
       const core::BatchTilePlan& plan) {
       
       // Create output image
       const auto& output_spec = plan.output_spec;
       auto output_image = Image::CreateBlankImage(
           output_spec.width, output_spec.height, 
           ImageFormat::kRGB, DataType::kUInt8);
       
       // Process each tile in the plan
       for (const auto& tile_op : plan.tile_ops) {
           // Read tile data from file
           auto tile_data_or = ReadTileData(tile_op.source_level, 
                                           tile_op.tile_x, tile_op.tile_y);
           if (!tile_data_or.ok()) {
               return tile_data_or.status();
           }
           
           // Decode tile (format-specific)
           auto tile_image_or = DecodeTile(tile_data_or.value());
           if (!tile_image_or.ok()) {
               return tile_image_or.status();
           }
           
           // Apply transformations and paste into output
           auto transformed_or = ApplyTransform(tile_image_or.value(), 
                                               tile_op.transform);
           if (!transformed_or.ok()) {
               return transformed_or.status();
           }
           
           output_image.Paste(transformed_or.value(), 
                             tile_op.dest_x, tile_op.dest_y);
       }
       
       return output_image;
   }

   absl::StatusOr<std::vector<uint8_t>> MyFormatReader::ReadTileData(
       int level, int tile_x, int tile_y) {
       
       // Calculate tile offset in file
       const auto& level_info = metadata_.levels[level];
       uint64_t tiles_per_row = (level_info.width + level_info.tile_width - 1) 
                               / level_info.tile_width;
       uint64_t tile_index = tile_y * tiles_per_row + tile_x;
       
       // Look up tile in index (format-specific)
       uint64_t tile_offset = GetTileOffset(level, tile_index);
       uint32_t tile_size = GetTileSize(level, tile_index);
       
       // Read compressed tile data
       std::ifstream file(file_path_, std::ios::binary);
       file.seekg(tile_offset);
       
       std::vector<uint8_t> compressed_data(tile_size);
       file.read(reinterpret_cast<char*>(compressed_data.data()), tile_size);
       
       if (!file.good()) {
           return absl::DataLossError("Failed to read tile data");
       }
       
       return compressed_data;
   }

Step 4: Format Registration
---------------------------

Register with the Plugin System
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Create format plugin registration:

.. code-block:: cpp

   // src/readers/myformat/myformat_format_plugin.cpp
   #include "fastslide/runtime/reader_registry.h"
   #include "fastslide/readers/myformat/myformat_reader.h"
   
   namespace fastslide::formats::myformat {
   
   class MyFormatPlugin : public runtime::FormatDescriptor {
   public:
       std::string GetName() const override {
           return "MyFormat";
       }
       
       std::vector<std::string> GetSupportedExtensions() const override {
           return {".myf", ".myformat"};
       }
       
       std::set<runtime::FormatCapability> GetCapabilities() const override {
           return {
               runtime::FormatCapability::kMultiLevel,
               runtime::FormatCapability::kAssociatedImages,
               runtime::FormatCapability::kMetadata
           };
       }
       
       absl::StatusOr<std::unique_ptr<SlideReader>> CreateReader(
           const std::string& file_path) const override {
           return MyFormatReader::Create(file_path);
       }
   };
   
   // Register the format
   FASTSLIDE_REGISTER_FORMAT(MyFormatPlugin);
   
   } // namespace fastslide::formats::myformat

Update Build Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

Add your reader to the Bazel BUILD file:

.. code-block:: python

   # BUILD.bazel additions
   cc_library(
       name = "myformat_reader",
       srcs = [
           "src/readers/myformat/myformat_reader.cpp",
           "src/readers/myformat/myformat_format_plugin.cpp", 
       ],
       hdrs = [
           "include/fastslide/readers/myformat/myformat_reader.h",
           "include/fastslide/readers/myformat/myformat_format_plugin.h",
       ],
       deps = [
           ":slide_reader",
           ":tiff_based_reader", # If TIFF-based
           "@abseil-cpp//absl/status:statusor",
           "@nlohmann_json//:json", # If using JSON
       ],
       visibility = ["//visibility:public"],
   )

Step 5: Comprehensive Testing
-----------------------------

Unit Tests
~~~~~~~~~~

.. code-block:: cpp

   // tests/readers/myformat/myformat_reader_test.cpp
   #include <gtest/gtest.h>
   #include "fastslide/readers/myformat/myformat_reader.h"
   
   class MyFormatReaderTest : public ::testing::Test {
   protected:
       void SetUp() override {
           // Create test data or use sample files
           test_file_path_ = "testdata/sample.myf";
       }
       
       std::string test_file_path_;
   };
   
   TEST_F(MyFormatReaderTest, CreateReaderSuccess) {
       auto reader_or = MyFormatReader::Create(test_file_path_);
       ASSERT_TRUE(reader_or.ok()) << reader_or.status();
       
       auto reader = std::move(reader_or).value();
       EXPECT_NE(reader, nullptr);
   }
   
   TEST_F(MyFormatReaderTest, ValidateFileFormat) {
       auto reader_or = MyFormatReader::Create(test_file_path_);
       ASSERT_TRUE(reader_or.ok());
       
       auto reader = std::move(reader_or).value();
       
       // Test slide properties
       const auto properties = reader->GetProperties();
       EXPECT_GT(properties.base_dimensions.width, 0);
       EXPECT_GT(properties.base_dimensions.height, 0);
       
       // Test level information
       EXPECT_GT(reader->GetLevelCount(), 0);
       const auto level_info = reader->GetLevelInfo(0);
       EXPECT_EQ(level_info.width, properties.base_dimensions.width);
   }
   
   TEST_F(MyFormatReaderTest, ReadRegionBasic) {
       auto reader_or = MyFormatReader::Create(test_file_path_);
       ASSERT_TRUE(reader_or.ok());
       auto reader = std::move(reader_or).value();
       
       // Test reading a small region
       RegionSpec spec{
           .bounds = {0, 0, 256, 256},
           .level = 0
       };
       
       auto image_or = reader->ReadRegion(spec);
       ASSERT_TRUE(image_or.ok()) << image_or.status();
       
       const auto& image = image_or.value();
       EXPECT_EQ(image.GetWidth(), 256);
       EXPECT_EQ(image.GetHeight(), 256);
       EXPECT_EQ(image.GetChannels(), 3);  // RGB
   }

Integration Tests  
~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   TEST_F(MyFormatReaderTest, MultiThreadedReading) {
       auto reader_or = MyFormatReader::Create(test_file_path_);
       ASSERT_TRUE(reader_or.ok());
       auto reader = std::move(reader_or).value();
       
       const int num_threads = 4;
       std::vector<std::thread> workers;
       std::vector<bool> results(num_threads, false);
       
       for (int i = 0; i < num_threads; ++i) {
           workers.emplace_back([&, i]() {
               RegionSpec spec{
                   .bounds = {i * 256, 0, 256, 256},
                   .level = 0
               };
               
               auto image_or = reader->ReadRegion(spec);
               results[i] = image_or.ok();
           });
       }
       
       for (auto& worker : workers) {
           worker.join();
       }
       
       // All threads should succeed
       for (bool result : results) {
           EXPECT_TRUE(result);
       }
   }

Python Integration Tests
~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   # tests/python/test_myformat_python.py
   import pytest
   import fastslide
   import numpy as np
   
   class TestMyFormatPython:
       def setup_method(self):
           self.slide = fastslide.FastSlide.from_file_path("testdata/sample.myf")
       
       def test_basic_properties(self):
           assert self.slide.format == "MyFormat"
           assert isinstance(self.slide.dimensions, tuple)
           assert len(self.slide.dimensions) == 2
           assert all(d > 0 for d in self.slide.dimensions)
       
       def test_read_region(self):
           region = self.slide.read_region((0, 0), 0, (256, 256))
           
           assert isinstance(region, np.ndarray)
           assert region.shape == (256, 256, 3)
           assert region.dtype == np.uint8
       
       def test_level_navigation(self):
           assert self.slide.level_count > 0
           assert len(self.slide.level_dimensions) == self.slide.level_count
           
           # Test coordinate conversion
           level0_coords = (1000, 1000)
           for level in range(min(3, self.slide.level_count)):
               native_coords = self.slide.convert_level0_to_level_native(
                   *level0_coords, level)
               
               back_coords = self.slide.convert_level_native_to_level0(
                   *native_coords, level)
               
               # Should round-trip correctly (within tolerance)
               assert abs(back_coords[0] - level0_coords[0]) < 2
               assert abs(back_coords[1] - level0_coords[1]) < 2

Step 6: Documentation Integration
---------------------------------

Update Format Documentation
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: rst

   # Add to docs/source/api/cpp_api.rst:
   
   MyFormat Reader
   ~~~~~~~~~~~~~~~
   
   .. doxygenclass:: fastslide::MyFormatReader
      :members:
      :protected-members:
   
   Specialized reader for MyFormat (.myf) files with support for:
   
   - Multi-level pyramid structure
   - Custom metadata format
   - Efficient tile caching
   - Thread-safe concurrent access
   
   **Example Usage:**
   
   .. code-block:: cpp
   
      auto reader_or = MyFormatReader::Create("slide.myf");
      if (reader_or.ok()) {
          auto reader = std::move(reader_or).value();
          // Use reader...
      }

Common Implementation Patterns
------------------------------

Metadata Caching
~~~~~~~~~~~~~~~~~

Cache expensive metadata operations:

.. code-block:: cpp

   class MyFormatReader : public TiffBasedReader {
   private:
       mutable std::mutex metadata_mutex_;
       mutable std::optional<ParsedMetadata> cached_metadata_;
       
       const ParsedMetadata& GetCachedMetadata() const {
           std::lock_guard<std::mutex> lock(metadata_mutex_);
           if (!cached_metadata_) {
               cached_metadata_ = ParseMetadataExpensive();
           }
           return *cached_metadata_;
       }
   };

Error Context
~~~~~~~~~~~~~

Provide rich error context:

.. code-block:: cpp

   absl::Status MyFormatReader::ReadTileData(int level, int x, int y) {
       if (level < 0 || level >= GetLevelCount()) {
           return absl::OutOfRangeError(absl::StrCat(
               "Invalid level ", level, " (valid range: 0-", 
               GetLevelCount() - 1, ")"));
       }
       
       const auto& level_info = metadata_.levels[level];
       if (x < 0 || y < 0 || 
           x >= level_info.tiles_x || y >= level_info.tiles_y) {
           return absl::OutOfRangeError(absl::StrCat(
               "Invalid tile coordinates (", x, ",", y, ") for level ", level,
               " (valid range: 0-", level_info.tiles_x - 1, ",0-", 
               level_info.tiles_y - 1, ")"));
       }
       
       // Continue with implementation...
   }

Memory Management
~~~~~~~~~~~~~~~~~

Use RAII for all resources:

.. code-block:: cpp

   class MyFormatReader : public TiffBasedReader {
   private:
       class FileHandle {
       public:
           explicit FileHandle(const std::string& path) 
               : file_(path, std::ios::binary) {
               if (!file_.is_open()) {
                   throw std::runtime_error("Cannot open file: " + path);
               }
           }
           
           ~FileHandle() = default;  // RAII cleanup
           
           std::ifstream& stream() { return file_; }
           
       private:
           std::ifstream file_;
       };
       
       std::unique_ptr<FileHandle> file_handle_;
   };

Step 7: Performance Optimization
--------------------------------

Profile-Guided Optimization
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Always profile your implementation:

.. code-block:: cpp

   // Add performance timing
   #include <chrono>
   
   absl::StatusOr<Image> MyFormatReader::ReadRegion(const RegionSpec& spec) {
       auto start = std::chrono::high_resolution_clock::now();
       
       auto result = ExecutePlan(BuildPlan(spec));
       
       auto end = std::chrono::high_resolution_clock::now();
       auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
           end - start).count();
       
       LOG(INFO) << "ReadRegion took " << duration << " microseconds";
       
       return result;
   }

Benchmarking Framework
~~~~~~~~~~~~~~~~~~~~~~

Create benchmarks for your reader:

.. code-block:: cpp

   // benchmarks/myformat_benchmark.cpp
   #include <benchmark/benchmark.h>
   #include "fastslide/readers/myformat/myformat_reader.h"
   
   static void BM_MyFormatReadRegion(benchmark::State& state) {
       auto reader_or = MyFormatReader::Create("testdata/large.myf");
       ASSERT_TRUE(reader_or.ok());
       auto reader = std::move(reader_or).value();
       
       RegionSpec spec{
           .bounds = {0, 0, 1024, 1024},
           .level = static_cast<int>(state.range(0))
       };
       
       for (auto _ : state) {
           auto image_or = reader->ReadRegion(spec);
           benchmark::DoNotOptimize(image_or);
       }
   }
   
   BENCHMARK(BM_MyFormatReadRegion)
       ->Range(0, 3)  // Test levels 0-3
       ->Unit(benchmark::kMillisecond);

Troubleshooting Common Issues
-----------------------------

Build Errors
~~~~~~~~~~~~

**Issue**: Linker errors with undefined symbols
   **Solution**: Check Bazel dependencies and ensure all required libraries are linked

**Issue**: Template compilation errors  
   **Solution**: Verify C++20 feature usage and compiler compatibility

Runtime Errors  
~~~~~~~~~~~~~~

**Issue**: Segmentation faults during reading
   **Solution**: Check buffer bounds and ensure proper memory alignment

**Issue**: Incorrect image colors or dimensions
   **Solution**: Verify pixel format conversion and coordinate system mapping

**Issue**: Performance problems with large slides
   **Solution**: Profile memory usage and implement tile-level caching

Integration Issues
~~~~~~~~~~~~~~~~~~

**Issue**: Python bindings not working
   **Solution**: Ensure reader is properly registered and exported in pybind11 module

**Issue**: Format not detected automatically
   **Solution**: Check file extension registration and format validation logic

Validation Checklist
---------------------

Before submitting your new reader:

- [ ] **Functionality**: All core methods implemented and working
- [ ] **Error Handling**: Graceful failure with informative error messages  
- [ ] **Thread Safety**: Safe for concurrent access from multiple threads
- [ ] **Memory Safety**: No leaks, use-after-free, or buffer overflows
- [ ] **Performance**: Reasonable performance for typical use cases
- [ ] **Testing**: Comprehensive unit and integration test coverage
- [ ] **Documentation**: API documentation and usage examples
- [ ] **Python Bindings**: Automatic availability in Python API
- [ ] **Platform Support**: Works on macOS and Linux
- [ ] **Format Compliance**: Correctly handles all format variations

Next Steps
----------

Once your reader is complete:

1. **Submit Pull Request**: Include implementation, tests, and documentation
2. **Code Review**: Address feedback from maintainers
3. **Integration Testing**: Ensure compatibility with existing features
4. **Performance Validation**: Benchmark against similar formats
5. **Documentation Update**: Add format to supported formats list
6. **Release Planning**: Include in next version release notes

See :doc:`external_reader` for information on creating external plugin readers.
