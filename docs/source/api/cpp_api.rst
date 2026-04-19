C++ API Reference
==================

The FastSlide C++ API provides high-performance, memory-safe access to whole slide images using modern C++20 features.

.. note::
   **📊 Doxygen Documentation Available**
   
   For the complete C++ API reference with interactive dependency graphs, inheritance diagrams, 
   and detailed call/caller graphs, see the `Doxygen Documentation <../doxygen/index.html>`_.
   
   This page provides a quick overview with Python cross-references. The Doxygen documentation
   offers comprehensive C++ details with beautiful SVG visualizations.

.. cpp:namespace:: fastslide

Core Classes
------------

SlideReader
~~~~~~~~~~~

.. raw:: html

   <div class="inheritance-showcase">
       <h4>🏗️ Class Inheritance Hierarchy</h4>
       <a href="../doxygen/classfastslide_1_1_slide_reader.html" target="_blank" class="doxygen-direct-link">
           📊 View Interactive Inheritance Diagram in Doxygen
       </a>
   </div>

.. doxygenclass:: fastslide::SlideReader
   :members:
   :protected-members:

.. inheritance-diagram:: fastslide::SlideReader fastslide::TiffBasedReader fastslide::MrxsReader fastslide::QpTiffReader fastslide::AperioReader
   :parts: 1

The main slide reader class that provides access to whole slide images.

**Example Usage:**

.. code-block:: cpp

   #include <fastslide/slide_reader.h>
   #include <iostream>
   
   auto reader_or = fastslide::SlideReader::Open("slide.mrxs");
   if (!reader_or.ok()) {
       std::cerr << "Failed to open slide: " << reader_or.status() << std::endl;
       return -1;
   }
   
   auto reader = std::move(reader_or).value();
   
   // Get slide properties
   const auto properties = reader->GetProperties();
   std::cout << "Dimensions: " << properties.base_dimensions.width 
             << "x" << properties.base_dimensions.height << std::endl;
   
   // Read a region
   fastslide::RegionSpec region_spec{
       .bounds = {0, 0, 1024, 1024},
       .level = 0
   };
   
   auto image_or = reader->ReadRegion(region_spec);
   if (image_or.ok()) {
       const auto& image = image_or.value();
       std::cout << "Read region: " << image.GetWidth() << "x" << image.GetHeight() << std::endl;
   }

Image
~~~~~

.. doxygenclass:: fastslide::Image
   :members:

High-performance image container with support for multiple data types and channel configurations.

Metadata
~~~~~~~~

.. doxygenclass:: fastslide::Metadata
   :members:

.. doxygenclass:: fastslide::core::SlideProperties
   :members:

.. doxygenclass:: fastslide::core::LevelInfo
   :members:

.. doxygenclass:: fastslide::core::ChannelMetadata
   :members:

Coordinate Systems
~~~~~~~~~~~~~~~~~~

.. doxygenclass:: fastslide::core::RegionSpec
   :members:

.. doxygenclass:: fastslide::core::SlideBounds
   :members:

.. doxygenclass:: fastslide::core::TileCoordinate
   :members:

Reader Implementations
----------------------

Base Classes
~~~~~~~~~~~~

.. raw:: html

   <div class="inheritance-showcase">
       <h4>📋 TIFF-Based Reader Hierarchy</h4>
       <a href="../doxygen/classfastslide_1_1_tiff_based_reader.html" target="_blank" class="doxygen-direct-link">
           📊 View in Doxygen with Dependencies
       </a>
   </div>

.. doxygenclass:: fastslide::TiffBasedReader
   :members:
   :protected-members:

.. inheritance-diagram:: fastslide::TiffBasedReader fastslide::MrxsReader fastslide::QpTiffReader fastslide::AperioReader
   :parts: 2

Format-Specific Readers
~~~~~~~~~~~~~~~~~~~~~~~

.. raw:: html

   <div class="format-reader-grid">
       <div class="reader-card">
           <h4>🔬 MRXS Reader</h4>
           <p>3DHistech Pannoramic format</p>
           <a href="../doxygen/classfastslide_1_1_mrxs_reader.html" target="_blank" class="doxygen-direct-link">Doxygen Details</a>
       </div>
       <div class="reader-card">
           <h4>🎯 QPTIFF Reader</h4>
           <p>High-performance TIFF variant</p>
           <a href="../doxygen/classfastslide_1_1_qp_tiff_reader.html" target="_blank" class="doxygen-direct-link">Doxygen Details</a>
       </div>
       <div class="reader-card">
           <h4>📊 Aperio Reader</h4>
           <p>Aperio SVS format support</p>
           <a href="../doxygen/classfastslide_1_1_aperio_reader.html" target="_blank" class="doxygen-direct-link">Doxygen Details</a>
       </div>
   </div>

.. doxygenclass:: fastslide::MrxsReader
   :members:
   :protected-members:

MRXS (3DHistech) format reader with support for hierarchical and non-hierarchical data.

.. inheritance-diagram:: fastslide::MrxsReader
   :parts: 3
   :caption: MRXS Reader Class Hierarchy

.. doxygenclass:: fastslide::QpTiffReader
   :members:
   :protected-members:

QPTIFF format reader optimized for high-performance access.

.. inheritance-diagram:: fastslide::QpTiffReader
   :parts: 3
   :caption: QPTIFF Reader Class Hierarchy

.. doxygenclass:: fastslide::AperioReader
   :members:
   :protected-members:

Aperio SVS format reader.

.. inheritance-diagram:: fastslide::AperioReader
   :parts: 3
   :caption: Aperio Reader Class Hierarchy

Cache System
------------

.. raw:: html

   <div class="inheritance-showcase">
       <h4>💾 Cache System Architecture</h4>
       <a href="../doxygen/classfastslide_1_1_i_tile_cache.html" target="_blank" class="doxygen-direct-link">
           📊 Interactive Cache Dependencies
       </a>
   </div>

.. inheritance-diagram:: fastslide::ITileCache fastslide::LRUTileCache fastslide::GlobalTileCache fastslide::TileCache
   :parts: 2
   :caption: Tile Cache Inheritance Hierarchy

.. doxygenclass:: fastslide::ITileCache
   :members:

.. doxygenclass:: fastslide::LRUTileCache
   :members:

.. doxygenclass:: fastslide::GlobalTileCache
   :members:

Utilities
---------

TIFF Utilities
~~~~~~~~~~~~~~

.. doxygenclass:: fastslide::TiffFile
   :members:

.. doxygenclass:: fastslide::TiffDirectoryProcessor
   :members:

.. doxygenclass:: fastslide::tiff::TiffCacheService
   :members:

Resampling
~~~~~~~~~~

.. doxygennamespace:: fastslide::resample
   :content-only:

Runtime System
~~~~~~~~~~~~~~

.. doxygenclass:: fastslide::runtime::ReaderRegistry
   :members:

.. doxygenclass:: fastslide::FormatDescriptor
   :members:

.. doxygenclass:: fastslide::runtime::PluginLoader
   :members:

Error Handling
--------------

FastSlide uses ``absl::Status`` and ``absl::StatusOr<T>`` for error handling instead of exceptions.

**Example:**

.. code-block:: cpp

   auto reader_or = fastslide::SlideReader::Open("slide.svs");
   if (!reader_or.ok()) {
       // Handle error
       std::cerr << "Error: " << reader_or.status().message() << std::endl;
       return;
   }
   
   auto reader = std::move(reader_or).value();
   // Use reader...

C++ API Examples
----------------

Basic Reading
~~~~~~~~~~~~~

.. code-block:: cpp

   #include <fastslide/slide_reader.h>
   #include <fastslide/image.h>
   
   // Open slide
   auto reader_or = fastslide::SlideReader::Open("slide.mrxs");
   FASTSLIDE_RETURN_IF_ERROR(reader_or.status());
   auto reader = std::move(reader_or).value();
   
   // Create region specification
   fastslide::RegionSpec spec{
       .bounds = {1000, 1000, 2048, 2048},  // x, y, width, height
       .level = 0,                           // Full resolution
       .channels = {}                        // All channels
   };
   
   // Read region
   auto image_or = reader->ReadRegion(spec);
   FASTSLIDE_RETURN_IF_ERROR(image_or.status());
   auto image = std::move(image_or).value();

Memory Management
~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include <fastslide/slide_reader.h>
   #include <memory>
   
   class SlideProcessor {
   private:
       std::unique_ptr<fastslide::SlideReader> reader_;
       
   public:
       absl::Status LoadSlide(const std::string& path) {
           auto reader_or = fastslide::SlideReader::Open(path);
           if (!reader_or.ok()) {
               return reader_or.status();
           }
           
           reader_ = std::move(reader_or).value();
           return absl::OkStatus();
       }
       
       absl::StatusOr<fastslide::Image> ProcessRegion(
           const fastslide::RegionSpec& spec) {
           if (!reader_) {
               return absl::FailedPreconditionError("No slide loaded");
           }
           
           return reader_->ReadRegion(spec);
       }
   };

Multi-threading
~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include <fastslide/slide_reader.h>
   #include <thread>
   #include <vector>
   
   void ProcessTiles(const std::string& slide_path) {
       auto reader_or = fastslide::SlideReader::Open(slide_path);
       if (!reader_or.ok()) return;
       
       auto reader = std::move(reader_or).value();
       const int num_threads = 4;
       std::vector<std::thread> workers;
       
       for (int i = 0; i < num_threads; ++i) {
           workers.emplace_back([&reader, i]() {
               // Each thread can safely read different regions
               fastslide::RegionSpec spec{
                   .bounds = {i * 1000, 0, 1000, 1000},
                   .level = 0
               };
               
               auto image_or = reader->ReadRegion(spec);
               if (image_or.ok()) {
                   // Process image...
               }
           });
       }
       
       for (auto& worker : workers) {
           worker.join();
       }
   }

Advanced Caching
~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include <fastslide/slide_reader.h>
   #include <fastslide/utilities/cache.h>
   
   void OptimizedReading() {
       auto reader_or = fastslide::SlideReader::Open("large_slide.mrxs");
       FASTSLIDE_RETURN_IF_ERROR(reader_or.status());
       auto reader = std::move(reader_or).value();
       
       // Create custom cache with specific capacity
       auto cache = fastslide::GlobalTileCache::Create(1000);  // 1000 tiles
       reader->SetTileCache(cache);
       
       // Read overlapping regions - will benefit from caching
       std::vector<fastslide::Image> regions;
       for (int i = 0; i < 10; ++i) {
           fastslide::RegionSpec spec{
               .bounds = {i * 256, 0, 512, 512},  // 50% overlap
               .level = 1
           };
           
           auto image_or = reader->ReadRegion(spec);
           if (image_or.ok()) {
               regions.push_back(std::move(image_or).value());
           }
       }
       
       // Check cache performance
       const auto stats = cache->GetStats();
       std::cout << "Cache hit ratio: " << stats.hit_ratio << std::endl;
   }

Type Safety Features
--------------------

Modern C++20 Features
~~~~~~~~~~~~~~~~~~~~~~

FastSlide uses modern C++20 features for type safety and performance:

- **Concepts**: Type checking at compile time
- **Structured Bindings**: Clean tuple unpacking  
- **Smart Pointers**: Automatic memory management
- **absl::StatusOr**: Safe error handling without exceptions
- **std::span**: Safe array views
- **std::string_view**: Efficient string references

.. code-block:: cpp

   // Modern C++20 usage example
   auto [width, height] = reader->GetDimensions();
   
   // Structured binding with error handling
   if (auto image_or = reader->ReadRegion(spec); image_or.ok()) {
       const auto& image = image_or.value();
       auto [img_width, img_height] = image.GetDimensions();
   }

Memory Safety
~~~~~~~~~~~~~

All FastSlide APIs follow RAII principles:

- No raw ``new``/``delete`` usage
- Automatic resource cleanup
- Exception safety guarantees  
- Thread-safe shared resources
