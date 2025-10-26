Creating External Reader Plugins
=================================

This guide explains how to create external reader plugins that can be loaded at runtime without modifying the FastSlide core library.

.. raw:: html

   <div class="api-section">
       <h3>🔌 Plugin Development Guide</h3>
       <p>Create standalone reader plugins for proprietary or specialized formats</p>
   </div>

Overview
--------

External reader plugins allow you to:

- **Add proprietary format support** without sharing source code
- **Distribute readers independently** of FastSlide releases  
- **Hot-load new formats** without application restart
- **Maintain separate codebases** for specialized formats

Plugin Architecture
-------------------

External readers are implemented as **shared libraries** (.so, .dll, .dylib) that:

1. **Implement** the FastSlide plugin interface
2. **Export** a standard plugin entry point
3. **Register** format capabilities and extensions
4. **Provide** factory methods for reader creation

.. mermaid::

   graph TB
       subgraph fastslide_core["FastSlide Core"]
           Registry[Reader Registry]
           PluginLoader[Plugin Loader]
           Interface[Plugin Interface]
       end
       
       subgraph external_plugin["External Plugin (.so/.dll)"]
           PluginEntry[Plugin Entry Point]
           FormatReader[Format Reader Implementation]  
           MetadataParser[Metadata Parser]
           TileDecoder[Tile Decoder]
       end
       
       subgraph application["Application"]
           App[User Application]
       end
       
       App --> Registry
       Registry --> PluginLoader
       PluginLoader --> PluginEntry
       PluginEntry --> FormatReader
       FormatReader --> Interface
       Interface --> Registry

Step 1: Plugin Project Setup
-----------------------------

Create Plugin Structure
~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   # Create external plugin project
   mkdir fastslide-myformat-plugin
   cd fastslide-myformat-plugin
   
   # Create directory structure
   mkdir -p {src,include,tests,examples}
   
   # Project structure:
   fastslide-myformat-plugin/
   ├── BUILD.bazel                 # Bazel build configuration
   ├── WORKSPACE                   # Bazel workspace (or MODULE.bazel) 
   ├── src/
   │   ├── myformat_reader.cpp     # Reader implementation
   │   └── plugin_entry.cpp        # Plugin entry point
   ├── include/
   │   └── myformat_reader.h       # Reader header
   ├── tests/
   │   └── myformat_test.cpp       # Unit tests
   └── examples/
       └── example.cpp             # Usage examples

Plugin Dependencies
~~~~~~~~~~~~~~~~~~~

Set up your WORKSPACE/MODULE.bazel:

.. code-block:: python

   # MODULE.bazel (Bazel 7+)
   module(name = "fastslide_myformat_plugin")
   
   # Import FastSlide as dependency
   git_repository = use_extension("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
   git_repository(
       name = "fastslide",
       remote = "https://github.com/AI-for-Oncology/fastslide.git",
       branch = "main",
   )
   
   # Other dependencies
   bazel_dep(name = "abseil-cpp", version = "20230802.1")

Step 2: Implement Plugin Interface
----------------------------------

Plugin Entry Point
~~~~~~~~~~~~~~~~~~

Every plugin must implement a standard entry point:

.. code-block:: cpp

   // src/plugin_entry.cpp
   #include "fastslide/runtime/plugin_loader.h"
   #include "myformat_reader.h"
   
   extern "C" {
   
   /**
    * @brief Plugin entry point called by FastSlide
    * @param registry Registry to register formats with
    * @return Status indicating success or failure
    */
   FASTSLIDE_PLUGIN_EXPORT
   fastslide::runtime::PluginInfo GetPluginInfo() {
       return {
           .name = "MyFormat Plugin",
           .version = "1.0.0", 
           .description = "Reader for MyFormat (.myf) files",
           .author = "Your Organization",
           .fastslide_version_required = "0.0.1"
       };
   }
   
   /**
    * @brief Initialize plugin and register formats
    * @param context Plugin loading context
    * @return Status indicating success or failure  
    */
   FASTSLIDE_PLUGIN_EXPORT
   absl::Status InitializePlugin(
       fastslide::runtime::PluginLoadContext* context) {
       
       // Register your format
       auto descriptor = std::make_unique<MyFormatDescriptor>();
       context->RegisterFormat(std::move(descriptor));
       
       return absl::OkStatus();
   }
   
   /**
    * @brief Cleanup plugin resources
    * @return Status indicating success or failure
    */
   FASTSLIDE_PLUGIN_EXPORT  
   absl::Status ShutdownPlugin() {
       // Cleanup any global resources
       return absl::OkStatus();
   }
   
   } // extern "C"

Format Descriptor Implementation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   // src/myformat_reader.cpp  
   #include "myformat_reader.h"
   #include "fastslide/runtime/format_descriptor.h"
   
   class MyFormatDescriptor : public fastslide::runtime::FormatDescriptor {
   public:
       std::string GetName() const override {
           return "MyFormat External Plugin";
       }
       
       std::vector<std::string> GetSupportedExtensions() const override {
           return {".myf", ".myformat"};
       }
       
       std::set<fastslide::runtime::FormatCapability> GetCapabilities() const override {
           return {
               fastslide::runtime::FormatCapability::kMultiLevel,
               fastslide::runtime::FormatCapability::kAssociatedImages,  
               fastslide::runtime::FormatCapability::kMetadata,
               fastslide::runtime::FormatCapability::kThreadSafe
           };
       }
       
       absl::StatusOr<std::unique_ptr<fastslide::SlideReader>> CreateReader(
           const std::string& file_path) const override {
           return MyFormatReader::Create(file_path);
       }
       
       fastslide::runtime::VersionConstraint GetVersionRequirement() const override {
           return fastslide::runtime::VersionConstraint::AtLeast("0.0.1");
       }
   };

Reader Implementation
~~~~~~~~~~~~~~~~~~~~~

Implement your reader following the same patterns as built-in readers:

.. code-block:: cpp

   // include/myformat_reader.h
   #pragma once
   
   #include "fastslide/slide_reader.h"
   #include "fastslide/core/slide_descriptor.h"
   
   class MyFormatReader : public fastslide::SlideReader {
   public:
       static absl::StatusOr<std::unique_ptr<MyFormatReader>> 
       Create(const std::string& file_path);
       
       // SlideReader interface implementation
       absl::StatusOr<fastslide::Image> ReadRegion(
           const fastslide::RegionSpec& spec) override;
       
       fastslide::SlideProperties GetProperties() const override;
       int GetLevelCount() const override;
       fastslide::LevelInfo GetLevelInfo(int level) const override;
       
       std::vector<std::string> GetAssociatedImageNames() const override;
       absl::StatusOr<fastslide::Image> ReadAssociatedImage(
           const std::string& name) const override;
       
   private:
       explicit MyFormatReader(const std::string& file_path);
       
       absl::Status Initialize();
       absl::Status LoadMetadata();
       
       std::string file_path_;
       // Your format-specific data members
   };

Step 3: Build System Configuration
----------------------------------

Bazel BUILD Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   # BUILD.bazel
   load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
   
   cc_library(
       name = "myformat_reader_lib",
       srcs = [
           "src/myformat_reader.cpp",
       ],
       hdrs = [
           "include/myformat_reader.h", 
       ],
       deps = [
           "@fastslide//fastslide:slide_reader",
           "@fastslide//fastslide:runtime",
           "@abseil-cpp//absl/status:statusor",
       ],
       copts = ["-std=c++20"],
       visibility = ["//visibility:private"],
   )
   
   # Plugin shared library
   cc_binary(
       name = "fastslide_myformat_plugin",
       srcs = [
           "src/plugin_entry.cpp",
       ],
       deps = [
           ":myformat_reader_lib",
           "@fastslide//fastslide:runtime",
       ],
       linkshared = True,
       copts = ["-std=c++20"],
       visibility = ["//visibility:public"],
   )
   
   # Tests
   cc_test(
       name = "myformat_test",
       srcs = ["tests/myformat_test.cpp"],
       deps = [
           ":myformat_reader_lib",
           "@googletest//:gtest_main",
       ],
       data = glob(["testdata/**"]),
   )

Alternative Build Systems
~~~~~~~~~~~~~~~~~~~~~~~~~

**CMake Example:**

.. code-block:: cmake

   # CMakeLists.txt
   cmake_minimum_required(VERSION 3.20)
   project(fastslide_myformat_plugin CXX)
   
   set(CMAKE_CXX_STANDARD 20)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
   
   # Find FastSlide
   find_package(fastslide REQUIRED)
   find_package(absl REQUIRED)
   
   # Plugin library
   add_library(fastslide_myformat_plugin SHARED
       src/plugin_entry.cpp
       src/myformat_reader.cpp
   )
   
   target_link_libraries(fastslide_myformat_plugin
       fastslide::runtime
       absl::status
       absl::statusor
   )
   
   # Set plugin properties
   set_target_properties(fastslide_myformat_plugin PROPERTIES
       CXX_VISIBILITY_PRESET hidden
       VISIBILITY_INLINES_HIDDEN ON
   )

Step 4: Plugin Loading and Registration
---------------------------------------

Automatic Discovery
~~~~~~~~~~~~~~~~~~~

FastSlide can automatically discover plugins:

.. code-block:: cpp

   // Application code - load plugins from directory
   #include "fastslide/runtime/plugin_loader.h"
   
   int main() {
       auto& plugin_loader = fastslide::runtime::GetPluginLoader();
       
       // Load all plugins from directory
       auto status = plugin_loader.LoadFromDirectory("./plugins/");
       if (!status.ok()) {
           std::cerr << "Failed to load plugins: " << status << std::endl;
           return 1;
       }
       
       // List loaded plugins
       const auto plugins = plugin_loader.GetLoadedPlugins();
       for (const auto& plugin : plugins) {
           std::cout << "Loaded plugin: " << plugin.name 
                     << " v" << plugin.version << std::endl;
       }
       
       // Now MyFormat files will be automatically supported
       auto reader_or = fastslide::SlideReader::Open("slide.myf");
       // ... use reader normally
   }

Manual Plugin Loading
~~~~~~~~~~~~~~~~~~~~~

For more control over plugin loading:

.. code-block:: cpp

   // Load specific plugin
   auto status = plugin_loader.LoadPlugin("./plugins/libfastslide_myformat_plugin.so");
   if (!status.ok()) {
       std::cerr << "Failed to load MyFormat plugin: " << status << std::endl;
   }

Python Integration
~~~~~~~~~~~~~~~~~~

External plugins are automatically available in Python:

.. code-block:: python

   import fastslide
   
   # Load plugins (done once at application startup)
   fastslide.load_plugins_from_directory("./plugins/")
   
   # MyFormat files now supported automatically
   slide = fastslide.FastSlide.from_file_path("slide.myf")
   print(f"Format: {slide.format}")  # "MyFormat External Plugin"

Step 5: Advanced Plugin Features
---------------------------------

Configuration Support
~~~~~~~~~~~~~~~~~~~~~

Add configuration support to your plugin:

.. code-block:: cpp

   class MyFormatDescriptor : public fastslide::runtime::FormatDescriptor {
   public:
       absl::Status Configure(const fastslide::runtime::PluginConfig& config) override {
           // Handle configuration options
           if (config.HasOption("compression_level")) {
               compression_level_ = config.GetInt("compression_level");
           }
           
           if (config.HasOption("cache_metadata")) {
               cache_metadata_ = config.GetBool("cache_metadata");
           }
           
           return absl::OkStatus();
       }
       
   private:
       int compression_level_ = 6;
       bool cache_metadata_ = true;
   };

Dependency Management
~~~~~~~~~~~~~~~~~~~~~

Handle plugin dependencies properly:

.. code-block:: cpp

   extern "C" {
   
   FASTSLIDE_PLUGIN_EXPORT
   fastslide::runtime::PluginDependencies GetPluginDependencies() {
       return {
           .required_plugins = {},  // No plugin dependencies
           .optional_plugins = {"advanced_codecs"},  // Optional enhancements
           .system_libraries = {"libmyformat.so.1"},  // Required system libs
           .min_fastslide_version = "0.0.1",
           .max_fastslide_version = "0.0.1"  // API compatibility range
       };
   }
   
   } // extern "C"

Custom Cache Integration
~~~~~~~~~~~~~~~~~~~~~~~~

Integrate with FastSlide's caching system:

.. code-block:: cpp

   class MyFormatReader : public fastslide::SlideReader {
   public:
       void SetTileCache(std::shared_ptr<fastslide::ITileCache> cache) override {
           tile_cache_ = cache;
       }
       
   private:
       absl::StatusOr<fastslide::Image> ReadRegionWithCaching(
           const fastslide::RegionSpec& spec) {
           
           // Generate cache key
           auto cache_key = GenerateCacheKey(spec);
           
           if (tile_cache_) {
               // Try cache first
               if (auto cached_tile = tile_cache_->Get(cache_key)) {
                   return *cached_tile;
               }
           }
           
           // Cache miss - read from file
           auto image_or = ReadRegionDirect(spec);
           if (!image_or.ok()) {
               return image_or.status();
           }
           
           // Store in cache
           if (tile_cache_) {
               tile_cache_->Put(cache_key, image_or.value());
           }
           
           return image_or.value();
       }
       
       std::shared_ptr<fastslide::ITileCache> tile_cache_;
   };

Step 6: Distribution and Deployment
-----------------------------------

Plugin Packaging
~~~~~~~~~~~~~~~~

Create distribution packages for your plugin:

.. code-block:: bash

   # Build plugin
   bazel build //fastslide_myformat_plugin:fastslide_myformat_plugin
   
   # Package for distribution
   mkdir -p dist/plugins
   cp bazel-bin/fastslide_myformat_plugin/libfastslide_myformat_plugin.so dist/plugins/
   
   # Create plugin manifest
   cat > dist/plugins/myformat.json << EOF
   {
       "name": "MyFormat Plugin",
       "version": "1.0.0",
       "library": "libfastslide_myformat_plugin.so",
       "supported_extensions": [".myf", ".myformat"],
       "capabilities": ["multi_level", "associated_images", "metadata"],
       "requirements": {
           "fastslide_version": ">=0.0.1",
           "system_libraries": ["libmyformat.so.1"]
       }
   }
   EOF

Installation Instructions
~~~~~~~~~~~~~~~~~~~~~~~~~

**For End Users:**

.. code-block:: bash

   # Install plugin
   mkdir -p ~/.fastslide/plugins
   cp libfastslide_myformat_plugin.so ~/.fastslide/plugins/
   
   # Plugin auto-discovery
   # FastSlide will automatically find and load plugins from:
   # - ~/.fastslide/plugins/
   # - /usr/local/lib/fastslide/plugins/
   # - ./plugins/ (relative to application)

**Programmatic Installation:**

.. code-block:: cpp

   // Load plugin programmatically
   auto& loader = fastslide::runtime::GetPluginLoader();
   auto status = loader.LoadPlugin("/path/to/libfastslide_myformat_plugin.so");
   if (!status.ok()) {
       // Handle error
   }

.. code-block:: python

   # Python
   import fastslide
   
   # Load plugin directory
   status = fastslide.load_plugins_from_directory("~/.fastslide/plugins/")
   if not status:
       print("Warning: Some plugins failed to load")
   
   # Verify format support
   if fastslide.is_supported("test.myf"):
       slide = fastslide.FastSlide.from_file_path("test.myf")

Step 7: Testing External Plugins
---------------------------------

Automated Testing
~~~~~~~~~~~~~~~~~

Create comprehensive tests for your plugin:

.. code-block:: cpp

   // tests/plugin_integration_test.cpp
   #include <gtest/gtest.h>
   #include "fastslide/runtime/plugin_loader.h"
   
   class PluginIntegrationTest : public ::testing::Test {
   protected:
       void SetUp() override {
           // Load the plugin
           auto& loader = fastslide::runtime::GetPluginLoader();
           auto status = loader.LoadPlugin("libfastslide_myformat_plugin.so");
           ASSERT_TRUE(status.ok()) << status;
       }
   };
   
   TEST_F(PluginIntegrationTest, PluginRegistersCorrectly) {
       const auto& registry = fastslide::runtime::GetGlobalRegistry();
       
       // Check format is registered
       EXPECT_TRUE(registry.SupportsExtension(".myf"));
       
       // Check capabilities
       auto format_or = registry.GetFormat(".myf");
       ASSERT_TRUE(format_or.ok());
       
       const auto& format = format_or.value();
       EXPECT_TRUE(format.HasCapability(
           fastslide::runtime::FormatCapability::kMultiLevel));
   }
   
   TEST_F(PluginIntegrationTest, ReaderCreationWorks) {
       auto reader_or = fastslide::SlideReader::Open("testdata/sample.myf");
       ASSERT_TRUE(reader_or.ok()) << reader_or.status();
       
       auto reader = std::move(reader_or).value();
       EXPECT_GT(reader->GetLevelCount(), 0);
       
       const auto properties = reader->GetProperties();
       EXPECT_GT(properties.base_dimensions.width, 0);
       EXPECT_GT(properties.base_dimensions.height, 0);
   }

Performance Testing
~~~~~~~~~~~~~~~~~~~

Benchmark your plugin against built-in readers:

.. code-block:: cpp

   // benchmarks/plugin_benchmark.cpp
   #include <benchmark/benchmark.h>
   
   static void BM_PluginReadRegion(benchmark::State& state) {
       // Load plugin
       auto& loader = fastslide::runtime::GetPluginLoader(); 
       loader.LoadPlugin("libfastslide_myformat_plugin.so");
       
       // Create reader
       auto reader_or = fastslide::SlideReader::Open("benchmark.myf");
       ASSERT_TRUE(reader_or.ok());
       auto reader = std::move(reader_or).value();
       
       fastslide::RegionSpec spec{
           .bounds = {0, 0, 1024, 1024},
           .level = static_cast<int>(state.range(0))
       };
       
       for (auto _ : state) {
           auto image_or = reader->ReadRegion(spec);
           benchmark::DoNotOptimize(image_or);
       }
       
       state.SetItemsProcessed(state.iterations() * 1024 * 1024); // pixels
   }
   
   BENCHMARK(BM_PluginReadRegion)->Range(0, 2);

Step 8: Advanced Plugin Patterns
---------------------------------

Multi-File Format Support
~~~~~~~~~~~~~~~~~~~~~~~~~

Handle formats with multiple associated files:

.. code-block:: cpp

   class MyFormatReader : public fastslide::SlideReader {
   private:
       struct FormatFiles {
           std::string main_file;      // .myf
           std::string index_file;     // .myi  
           std::string metadata_file;  // .mym
           std::vector<std::string> tile_files; // .myt files
       };
       
       absl::StatusOr<FormatFiles> DiscoverFormatFiles(
           const std::string& main_path) {
           
           FormatFiles files;
           files.main_file = main_path;
           
           std::filesystem::path base_path(main_path);
           base_path.replace_extension();
           
           // Look for associated files
           files.index_file = base_path.string() + ".myi";
           files.metadata_file = base_path.string() + ".mym";
           
           // Discover tile files
           for (int i = 0; /* until no more files */; ++i) {
               std::string tile_file = base_path.string() + 
                                      absl::StrCat(".myt", i);
               if (std::filesystem::exists(tile_file)) {
                   files.tile_files.push_back(tile_file);
               } else {
                   break;
               }
           }
           
           return files;
       }
   };

Network-Based Readers  
~~~~~~~~~~~~~~~~~~~~~

For cloud or network-based slide access:

.. code-block:: cpp

   class CloudFormatReader : public fastslide::SlideReader {
   private:
       class NetworkClient {
       public:
           absl::StatusOr<std::vector<uint8_t>> HttpGet(const std::string& url);
           absl::StatusOr<std::vector<uint8_t>> HttpPost(
               const std::string& url, const std::vector<uint8_t>& data);
       };
       
       absl::StatusOr<fastslide::Image> ReadRegion(
           const fastslide::RegionSpec& spec) override {
           
           // Build API request
           std::string url = absl::StrCat(
               base_url_, "/tiles?level=", spec.level,
               "&x=", spec.bounds.x, "&y=", spec.bounds.y,
               "&width=", spec.bounds.width, "&height=", spec.bounds.height);
           
           // Make HTTP request
           auto response_or = client_.HttpGet(url);
           if (!response_or.ok()) {
               return response_or.status();
           }
           
           // Decode response
           return DecodeImageResponse(response_or.value());
       }
       
       std::string base_url_;
       NetworkClient client_;
   };

Step 9: Plugin Distribution
---------------------------

Plugin Marketplace
~~~~~~~~~~~~~~~~~~

Consider creating a plugin marketplace entry:

.. code-block:: yaml

   # plugin_manifest.yaml
   name: "MyFormat Reader Plugin"
   version: "1.0.0"
   description: "High-performance reader for MyFormat digital pathology files"
   
   author:
     name: "Your Organization"
     email: "support@yourorg.com"
     website: "https://yourorg.com"
   
   compatibility:
     fastslide_version: ">=0.0.1"
     platforms: ["linux-x64", "macos-arm64"]
   
   capabilities:
     - "multi_level_pyramids"
     - "associated_images"
     - "metadata_extraction"
     - "thread_safe_reading"
   
   supported_formats:
     - extension: ".myf"
       description: "MyFormat slide files"
       mime_type: "application/x-myformat"
   
   installation:
     artifacts:
       - platform: "linux-x64"
         url: "https://releases.yourorg.com/plugins/linux/libfastslide_myformat.so"
         checksum: "sha256:abcd1234..."
       - platform: "macos-arm64"  
         url: "https://releases.yourorg.com/plugins/macos/libfastslide_myformat.dylib"
         checksum: "sha256:efgh5678..."

Continuous Integration
~~~~~~~~~~~~~~~~~~~~~~

Set up CI/CD for your plugin:

.. code-block:: yaml

   # .github/workflows/plugin-ci.yml
   name: Plugin CI
   
   on: [push, pull_request]
   
   jobs:
     build-and-test:
       strategy:
         matrix:
           os: [ubuntu-latest, macos-latest]
           
       runs-on: ${{ matrix.os }}
       
       steps:
       - uses: actions/checkout@v3
       
       - name: Setup Bazel
         uses: bazelbuild/setup-bazelisk@v2
         
       - name: Build Plugin
         run: bazel build //fastslide_myformat_plugin:fastslide_myformat_plugin
         
       - name: Run Tests
         run: bazel test //fastslide_myformat_plugin:all
         
       - name: Upload Plugin Artifact
         uses: actions/upload-artifact@v3
         with:
           name: plugin-${{ matrix.os }}
           path: bazel-bin/fastslide_myformat_plugin/

Security Considerations
-----------------------

Plugin Sandboxing
~~~~~~~~~~~~~~~~~

Consider security implications of loading external code:

.. code-block:: cpp

   // Plugin security validation
   class SecurePluginLoader {
   private:
       absl::Status ValidatePluginSecurity(const std::string& plugin_path) {
           // Check plugin signature
           if (!VerifyCodeSignature(plugin_path)) {
               return absl::PermissionDeniedError("Plugin not signed");
           }
           
           // Validate plugin manifest
           auto manifest_or = LoadPluginManifest(plugin_path);
           if (!manifest_or.ok()) {
               return manifest_or.status();
           }
           
           // Check allowed capabilities
           const auto& manifest = manifest_or.value();
           if (manifest.requires_network && !allow_network_plugins_) {
               return absl::PermissionDeniedError(
                   "Network plugins not allowed");
           }
           
           return absl::OkStatus();
       }
       
       bool allow_network_plugins_ = false;
   };

Resource Limits
~~~~~~~~~~~~~~~

Implement resource limits for plugin safety:

.. code-block:: cpp

   class MyFormatReader : public fastslide::SlideReader {
   private:
       static constexpr size_t MAX_MEMORY_USAGE = 1024 * 1024 * 1024; // 1GB
       static constexpr std::chrono::seconds MAX_OPERATION_TIME{30};
       
       absl::StatusOr<fastslide::Image> ReadRegionWithLimits(
           const fastslide::RegionSpec& spec) {
           
           // Check memory usage
           if (GetCurrentMemoryUsage() > MAX_MEMORY_USAGE) {
               return absl::ResourceExhaustedError("Memory limit exceeded");
           }
           
           // Set timeout for operation
           auto deadline = std::chrono::steady_clock::now() + MAX_OPERATION_TIME;
           
           return ReadRegionWithDeadline(spec, deadline);
       }
   };

Troubleshooting Plugin Issues
-----------------------------

Common Problems
~~~~~~~~~~~~~~~

**Plugin Not Loading**
   - Check library dependencies with ``ldd`` (Linux) or ``otool -L`` (macOS)
   - Verify plugin exports with ``nm`` or ``objdump``
   - Check FastSlide version compatibility

**Runtime Crashes**  
   - Ensure proper C++ exception handling at plugin boundaries
   - Check for memory alignment issues
   - Validate all pointer parameters

**Performance Issues**
   - Profile plugin with standard tools (perf, Instruments, Visual Studio)
   - Check for memory leaks with AddressSanitizer
   - Optimize hot paths identified by profiling

Debug Logging
~~~~~~~~~~~~~

Add comprehensive logging to your plugin:

.. code-block:: cpp

   #include <absl/log/log.h>
   
   absl::Status MyFormatReader::LoadMetadata() {
       LOG(INFO) << "Loading metadata from " << file_path_;
       
       auto start = std::chrono::steady_clock::now();
       
       // ... metadata loading logic ...
       
       auto duration = std::chrono::steady_clock::now() - start;
       LOG(INFO) << "Metadata loading took " 
                 << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() 
                 << "ms";
       
       return absl::OkStatus();
   }

Best Practices Summary
----------------------

**Design Principles:**
- Keep plugins **stateless** where possible
- Use **RAII** for all resource management  
- Implement **graceful error handling** with informative messages
- Follow **thread-safety** guidelines
- **Profile early** and optimize performance-critical paths

**API Consistency:**
- Match built-in reader **behavior patterns**
- Use consistent **coordinate systems** and data types
- Follow **naming conventions** from existing readers
- Provide **comprehensive error messages** with context

**Quality Assurance:**
- **Test thoroughly** with edge cases and malformed files
- **Validate** against reference implementations if available
- **Benchmark performance** against alternative tools
- **Check memory usage** under load

**Distribution:**
- Create **clear installation instructions**
- Provide **sample files** for testing
- Include **comprehensive documentation**
- Set up **continuous integration** for multiple platforms

Next Steps
----------

After completing your external reader plugin:

1. **Test Integration**: Verify it works with existing FastSlide applications
2. **Performance Validation**: Benchmark against built-in readers
3. **Documentation**: Create user guide and API documentation  
4. **Community Sharing**: Consider contributing back to the community
5. **Maintenance**: Plan for updates and FastSlide version compatibility

For questions about plugin development, see:
- :doc:`../architecture` - Understanding the plugin architecture
- :doc:`new_reader` - Built-in reader development patterns  
- FastSlide GitHub Issues - Community support and discussions
