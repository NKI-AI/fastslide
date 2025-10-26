FastSlide Architecture
======================

FastSlide is designed as a high-performance, extensible whole slide image reader with a layered architecture that separates concerns and enables easy extension.

.. raw:: html

   <div class="architecture-diagram">
       <h3>🏗️ System Architecture Overview</h3>
   </div>

Architecture Layers
-------------------

.. mermaid::

   graph TB
       subgraph python_api["Python API Layer"]
           PyAPI[Python Bindings<br/>pybind11]
           PyCache[Python Cache Wrappers]
           PyUtils[Python Utilities]
       end
       
       subgraph c_api["C API Layer"] 
           CAPI[C Interface]
           CUtils[C Utilities]
       end
       
       subgraph cpp_core["C++ Core Layer"]
           SlideReader[SlideReader]
           Image[Image Processing]
           Cache[Tile Cache System]
           Registry[Reader Registry]
       end
       
       subgraph format_layer["Format Layer"]
           MRXS[MRXS Reader]
           Aperio[Aperio Reader]
           QPTIFF[QPTIFF Reader] 
           TiffBase[TIFF Base Reader]
       end
       
       subgraph runtime_layer["Runtime Layer"]
           PluginLoader[Plugin Loader]
           Dependencies[Dependency Manager]
           TileWriter[Tile Writer]
       end
       
       subgraph utilities_layer["Utilities Layer"]
           TiffUtils[TIFF Utilities]
           SpatialIndex[Spatial Index]
           Resample[Resampling]
           Colors[Color Management]
       end
       
       PyAPI --> CAPI
       PyCache --> Cache
       CAPI --> SlideReader
       SlideReader --> Registry
       Registry --> MRXS
       Registry --> Aperio  
       Registry --> QPTIFF
       MRXS --> TiffBase
       Aperio --> TiffBase
       QPTIFF --> TiffBase
       TiffBase --> TiffUtils
       Cache --> SpatialIndex
       SlideReader --> Image
       Image --> Resample
       Image --> Colors
       Registry --> PluginLoader
       PluginLoader --> Dependencies

Core Components
---------------

1. **Reader Registry System**
   
   Central registry that manages format-specific readers using the factory pattern.

   .. doxygenclass:: fastslide::runtime::ReaderRegistry
      :no-link:

2. **Plugin Architecture**
   
   Extensible plugin system for adding new formats.

   .. doxygenclass:: fastslide::runtime::PluginLoader
      :no-link:

3. **Tile Cache System**
   
   Multi-level caching with LRU eviction and memory management.

   .. doxygenclass:: fastslide::runtime::ITileCache
      :no-link:

4. **Image Processing Pipeline**
   
   High-performance image processing with SIMD optimization.

   .. doxygenclass:: fastslide::Image
      :no-link:

Data Flow Architecture
----------------------

.. mermaid::

   sequenceDiagram
       participant App as Application
       participant Reader as SlideReader
       participant Cache as TileCache
       participant Format as Format Reader
       participant TIFF as TIFF Layer
       participant File as File System
       
       App->>Reader: ReadRegion(spec)
       Reader->>Cache: CheckCache(tile_key)
       alt Cache Hit
           Cache-->>Reader: Cached Tile
       else Cache Miss
           Reader->>Format: ReadTile(coordinates)
           Format->>TIFF: ReadTiffTile()
           TIFF->>File: Read bytes
           File-->>TIFF: Raw data
           TIFF-->>Format: Decoded tile
           Format->>Format: Apply transformations
           Format-->>Reader: Processed tile
           Reader->>Cache: StoreTile(tile_key, tile)
       end
       Reader->>Reader: Compose final image
       Reader-->>App: Image result

Threading Model
---------------

FastSlide uses a **thread-safe, lock-free design** where possible:

**Read Operations**
   - Multiple threads can read simultaneously from the same slide
   - Each thread gets its own I/O handles via ``TIFFHandlePool``
   - No locks needed for read-only operations

**Cache System** 
   - Thread-safe LRU cache with fine-grained locking
   - Lock-free fast path for cache hits
   - Write locks only during cache updates

**Format Readers**
   - Stateless design - no shared mutable state
   - Thread-local I/O buffers where needed
   - Immutable metadata cached after initialization

Memory Management
-----------------

Memory Safety Principles
~~~~~~~~~~~~~~~~~~~~~~~~

1. **RAII Everywhere**: All resources automatically cleaned up
2. **Smart Pointers**: ``std::unique_ptr`` for ownership, ``std::shared_ptr`` for shared resources  
3. **No Raw Pointers**: All pointers are managed automatically
4. **Span-based APIs**: Use ``std::span`` for safe array access
5. **String Views**: ``std::string_view`` for efficient string handling

Memory Layout
~~~~~~~~~~~~~

.. code-block:: cpp

   // Images use aligned memory for SIMD operations
   class Image {
       std::unique_ptr<uint8_t[], AlignedDeleter> data_;  // 32-byte aligned
       ImageDimensions dimensions_;                        // Width, height
       ImageFormat format_;                               // RGB, RGBA, etc.
   };

Cache Architecture
~~~~~~~~~~~~~~~~~~

The multi-level cache system:

.. mermaid::

   graph LR
       Reader[Reader] --> L1
       L1[L1: Hot Tiles<br/>LRU Cache]
       L2[L2: Compressed<br/>Storage]
       L3[L3: Disk Cache<br/>Future]
       FileSystem[File System]
       
       L1 -.-> L2
       L2 -.-> L3
       L3 -.-> FileSystem

Plugin System Architecture  
--------------------------

The plugin system enables runtime loading of new format readers.

**Plugin Lifecycle:**

1. **Discovery**: ``PluginLoader`` scans for available readers
2. **Registration**: Formats register capabilities and extensions  
3. **Factory**: ``ReaderRegistry`` creates readers based on file extension
4. **Initialization**: Reader-specific initialization and validation

Planning vs Execution Pipeline
------------------------------

All format readers in FastSlide follow a **two-stage pipeline architecture** that separates analysis (planning) from I/O (execution). This design enables testing, optimization, and caching while maintaining clean separation of concerns.

Why Two Stages?
~~~~~~~~~~~~~~~

The separation of planning from execution provides several critical benefits:

**1. Testability**
   - Plan generation can be unit tested without real files or I/O
   - Mock executors can validate planning logic in isolation
   - Execution can be tested with synthetic plans

**2. Performance Analysis**
   - Plans provide cost estimates before executing
   - Enable "what-if" analysis for different strategies
   - Clear separation between planning overhead and I/O time

**3. Optimization Opportunities**
   - Plans can be reordered for better I/O patterns (sequential access)
   - Tile operations can be deduplicated across multiple requests
   - Cache-aware planning (skip operations for cached tiles)

**4. Caching**
   - Plans themselves can be cached for repeated region reads
   - Cache key: ``(filename, level, region)`` → ``TilePlan``
   - Significant speedup for viewer applications (panning/zooming)

**5. Batching**
   - Multiple requests can be merged into a single batch plan
   - Shared tiles read only once
   - Enables efficient batch processing pipelines

Two-Stage Pipeline Design
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. mermaid::

   graph LR
       Request[TileRequest] --> PlanBuilder[Plan Builder]
       PlanBuilder --> TilePlan[TilePlan]
       TilePlan --> Executor[Tile Executor]
       Executor --> Output[TileWriter]
       
       style Request fill:#e1f5ff
       style TilePlan fill:#fff4e1
       style Output fill:#e8f5e9

**Stage 1: Planning** (``PrepareRequest`` → Plan Builder)

The planning stage analyzes what needs to be read without performing any I/O:

**Input:**
  - ``core::TileRequest``: Specifies pyramid level, region bounds (x, y, width, height), and options
  
**Process:**
  - Validate request parameters (level bounds, region coordinates)
  - Query format metadata (tile dimensions, channels, compression)
  - Enumerate tiles that intersect the requested region
  - Calculate coordinate transforms and clipping rectangles
  - Estimate execution costs (I/O bytes, decode time, cache hits)
  
**Output:**
  - ``core::TilePlan``: Complete execution plan containing:
    
    - ``operations``: Vector of ``TileReadOp`` descriptors (what to read, where to write)
    - ``output``: ``OutputSpec`` (dimensions, channels, pixel format, background color)
    - ``cost``: ``TilePlan::Cost`` (estimated time, total bytes, tiles to decode)
    - ``actual_region``: Final region after boundary clipping

**Stage 2: Execution** (Tile Executor)

The execution stage performs I/O and decoding based on the pre-computed plan:

**Input:**
  - ``core::TilePlan``: Pre-computed plan from builder
  - ``runtime::TileWriter``: Output buffer manager
  - Format-specific metadata (optional, e.g., TIFF structure info)
  
**Process:**
  - Read tiles from disk (potentially in parallel)
  - Decode compressed data (JPEG, PNG, LZW, etc.)
  - Apply transforms (crop, scale, blend for overlaps)
  - Write decoded data to output buffer
  - Handle errors gracefully (log warnings, fill with background)
  
**Output:**
  - Populated ``TileWriter`` buffer → ``Image`` via ``Finalize()``

Data Structures
~~~~~~~~~~~~~~~

The pipeline uses well-defined data structures for communication between stages.

**core::TileRequest** - Input to planning stage:

.. code-block:: cpp

   struct TileRequest {
     int level;                             // Pyramid level (0 = full resolution)
     TileCoordinate tile_coord;             // Tile grid position (x, y)
     std::vector<size_t> channel_indices;   // Channels to extract (empty = all)
     
     // Optional: precise region bounds with fractional coordinates
     std::optional<FractionalRegionBounds> region_bounds;
   };
   
   struct FractionalRegionBounds {
     double x, y;           // Fractional pixel coordinates
     double width, height;  // Region dimensions
   };

**core::TileReadOp** - Single tile operation descriptor:

.. code-block:: cpp

   struct TileReadOp {
     int level;                       // Pyramid level
     TileCoordinate tile_coord;       // Tile grid position (x, y)
     
     // Format-specific source identification
     uint32_t source_id;              // TIFF page, DAT file index, etc.
     uint64_t byte_offset;            // Offset within source
     uint32_t byte_size;              // Compressed size (estimate)
     
     // Coordinate transforms
     TileTransform transform;
       transform.source;              // Crop rectangle within tile
       transform.dest;                // Destination rectangle in output
     
     // Optional blending metadata (MRXS only)
     std::optional<BlendMetadata> blend_metadata;
       fractional_x, fractional_y;   // Subpixel positioning
       weight;                        // Confidence/coverage weight
       mode;                          // kOverwrite, kAverage, etc.
   };

**core::TilePlan** - Output from planning stage:

.. code-block:: cpp

   struct TilePlan {
     TileRequest request;              // Original request
     std::vector<TileReadOp> operations;  // Tile operations to execute
     OutputSpec output;                // Output buffer specification
     RegionSpec actual_region;         // Clamped region (may differ from request)
     
     Cost cost;                        // Execution cost estimates
       size_t total_bytes_to_read;     // Total I/O in bytes
       size_t total_tiles;             // Number of tiles
       size_t tiles_to_decode;         // Tiles needing decompression
       size_t tiles_from_cache;        // Tiles available in cache
       double estimated_time_ms;       // Rough time estimate
   };

**core::OutputSpec** - Output buffer specification:

.. code-block:: cpp

   struct OutputSpec {
     ImageDimensions dimensions;       // Output width × height
     uint32_t channels;                // Number of channels (3 for RGB)
     PixelFormat pixel_format;         // kUInt8, kUInt16, kFloat32
     PlanarConfig planar_config;       // kContiguous or kSeparate
     Background background;            // Fill color for missing tiles
   };

Execution Patterns
~~~~~~~~~~~~~~~~~~

Format readers implement different execution strategies based on their characteristics:

**Sequential Execution**
   - Single-threaded execution in calling thread
   - Used when: few tiles (≤8 for Aperio), channel dependencies (QPTIFF)
   - Benefits: No thread pool overhead, better cache locality
   - Avoids: Nested parallelism deadlocks

**Parallel Execution**
   - Submit operations to global thread pool
   - Used when: many tiles (>8), independent operations
   - Benefits: CPU parallelism for decoding, concurrent I/O
   - Thread safety: Mutex-protected writes to output buffer

**Handle Pool Pattern** (TIFF-based formats)
   - Maintain multiple file handles per reader
   - Each thread acquires its own handle from pool
   - Benefits: Zero contention, directory caching optimization
   - Used by: Aperio, QPTIFF

Memory Management
~~~~~~~~~~~~~~~~~

The pipeline uses careful memory management to avoid allocations in hot paths:

**Thread-Local Buffers**
   - Tile decode buffers allocated once per thread
   - Reused across all tiles processed by that thread
   - Eliminates per-tile allocation overhead

**Output Buffer Strategies**
   - **Overwrite**: Direct write for non-overlapping formats (Aperio, QPTIFF)
   - **Weighted Blending**: Accumulate + weight buffer for overlapping formats (MRXS)
   - ``TileWriter`` selects strategy based on ``BlendMetadata``

**Zero-Copy Where Possible**
   - Use ``std::span`` for buffer views (no copying)
   - Move semantics for large structures
   - RAII ensures automatic cleanup

Cost Estimation
~~~~~~~~~~~~~~~

The planning stage estimates execution costs for optimization and monitoring:

**I/O Cost**
   - Sum of ``byte_size`` across all operations
   - Accounts for compression (JPEG smaller than raw)
   - Used for: Progress reporting, timeout calculation

**Decode Cost**
   - ``tiles_to_decode`` count
   - Rough time estimate based on compression type
   - JPEG decode: ~1-2ms per 240×240 tile
   - LZW decode: ~0.5ms per tile

**Cache Hit Rate**
   - ``tiles_from_cache`` (if cache queried during planning)
   - Adjusts cost estimates based on cached data
   - Enables cache-aware query optimization

Example Planning Algorithm Pseudocode
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generic planning algorithm used by most formats:

.. code-block:: text

   function BuildPlan(request, reader):
     // 1. Validate inputs
     if request.level < 0 or request.level >= reader.level_count:
       return error("Invalid level")
     
     // 2. Get format metadata
     level_info = reader.GetLevelInfo(request.level)
     tile_layout = QueryTileLayout(level_info)  // Format-specific
     
     // 3. Determine region bounds
     x, y, width, height = request.region_bounds or (0, 0, level_info.dimensions)
     
     // 4. Enumerate intersecting tiles
     operations = []
     for tile in FindIntersectingTiles(tile_layout, x, y, width, height):
       // Calculate clipping
       tile_bounds = (tile.x * tile.width, tile.y * tile.height, tile.width, tile.height)
       intersection = Intersect(tile_bounds, (x, y, width, height))
       
       if intersection.area > 0:
         op = TileReadOp()
         op.source_id = tile.source_id
         op.byte_offset = tile.byte_offset
         op.byte_size = tile.byte_size
         op.transform.source = intersection relative to tile origin
         op.transform.dest = intersection relative to region origin
         operations.append(op)
     
     // 5. Create output specification
     output = OutputSpec()
     output.dimensions = (width, height)
     output.channels = level_info.channels
     output.pixel_format = kUInt8  // Or higher bit depth
     
     // 6. Estimate costs
     cost.total_tiles = len(operations)
     cost.total_bytes_to_read = sum(op.byte_size for op in operations)
     cost.tiles_to_decode = cost.total_tiles  // Assume no cache
     
     // 7. Return plan
     return TilePlan(operations, output, cost)

Example Execution Algorithm Pseudocode
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generic execution algorithm with parallel strategy selection:

.. code-block:: text

   function ExecutePlan(plan, writer):
     if plan.operations.empty():
       writer.FillWithBackground(plan.output.background)
       return success
     
     // Choose execution strategy
     if plan.operations.size() <= SEQUENTIAL_THRESHOLD:  // e.g., 8
       return ExecuteSequential(plan, writer)
     else:
       return ExecuteParallel(plan, writer)
   
   function ExecuteSequential(plan, writer):
     for op in plan.operations:
       tile_data = ReadAndDecodeTile(op)  // Format-specific
       cropped = ExtractSubRegion(tile_data, op.transform.source)
       writer.WriteTile(op, cropped)  // No mutex needed
     return success
   
   function ExecuteParallel(plan, writer):
     thread_pool = GetGlobalThreadPool()
     mutex = Mutex()
     
     futures = thread_pool.submit_all(plan.operations, lambda op:
       tile_data = ReadAndDecodeTile(op)  // Thread-local buffers
       cropped = ExtractSubRegion(tile_data, op.transform.source)
       
       lock(mutex):  // Protect shared writer
         writer.WriteTile(op, cropped)
     )
     
     futures.wait()  // Synchronize all threads
     return success

Key Data Structures
~~~~~~~~~~~~~~~~~~~

**core::TileReadOp** - Single tile operation descriptor:

.. code-block:: cpp

   struct TileReadOp {
     int level;                      // Pyramid level
     TileCoordinate tile_coord;      // Tile grid position (x, y)
     
     // Format-specific source identification
     uint32_t source_id;             // TIFF page, DAT file index, etc.
     uint64_t byte_offset;           // Offset within source
     uint32_t byte_size;             // Compressed size (estimate)
     
     // Coordinate transforms
     TileTransform transform;
       transform.source;             // Crop rectangle within tile
       transform.dest;               // Destination rectangle in output
     
     // Optional blending metadata (MRXS only)
     std::optional<BlendMetadata> blend_metadata;
       fractional_x, fractional_y;  // Subpixel positioning
       weight;                       // Confidence/coverage weight
       mode;                         // kOverwrite, kAverage, etc.
   };

**core::OutputSpec** - Output buffer specification:

.. code-block:: cpp

   struct OutputSpec {
     ImageDimensions dimensions;     // Output width × height
     uint32_t channels;              // Number of channels (3 for RGB)
     PixelFormat pixel_format;       // kUInt8, kUInt16, kFloat32
     Background background;          // Fill color for missing tiles
   };

**core::TilePlan::Cost** - Execution cost estimates:

.. code-block:: cpp

   struct Cost {
     size_t total_bytes_to_read;    // Total I/O in bytes
     size_t total_tiles;             // Number of tiles to process
     size_t tiles_to_decode;         // Tiles needing decompression
     size_t tiles_from_cache;        // Tiles available in cache
     double estimated_time_ms;       // Rough time estimate
   };

Pipeline Flow Diagram
~~~~~~~~~~~~~~~~~~~~~

.. mermaid::

   sequenceDiagram
       participant App as Application
       participant Reader as SlideReader
       participant Builder as PlanBuilder
       participant Executor as TileExecutor
       participant Cache as TileCache
       participant Disk as Disk I/O
       
       App->>Reader: ReadRegion(request)
       Reader->>Builder: BuildPlan(request)
       
       Note over Builder: Analyze region<br/>Enumerate tiles<br/>Calculate transforms
       
       Builder-->>Reader: TilePlan
       Reader->>Executor: ExecutePlan(plan)
       
       loop For each tile operation
           Executor->>Cache: CheckCache(tile_key)
           alt Cache Hit
               Cache-->>Executor: Cached tile data
           else Cache Miss
               Executor->>Disk: Read compressed tile
               Disk-->>Executor: Compressed data
               Note over Executor: Decode (JPEG/PNG/LZW)
               Executor->>Cache: Store decoded tile
           end
           Executor->>Executor: Apply transforms<br/>(crop, scale, blend)
       end
       
       Executor-->>Reader: Populated buffer
       Reader-->>App: Image

Format-Specific Planning Strategies
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Different formats implement planning differently based on their structure:

.. list-table:: Format Planning Comparison
   :header-rows: 1
   :widths: 15 20 30 20 15

   * - Format
     - Plan Builder
     - Special Handling
     - Parallel Execution
     - Complexity
   * - **Aperio SVS**
     - AperioPlanBuilder
     - Tiled vs stripped TIFF, handle pool
     - Yes (>8 tiles)
     - Medium
   * - **MRXS**
     - MrxsPlanBuilder
     - Overlapping tiles, spatial index, fractional positioning
     - Yes (always)
     - High
   * - **QPTIFF**
     - Built-in
     - Page-per-channel, multi-channel accumulation
     - No (sequential)
     - Low

**Aperio SVS:**
  - Queries TIFF structure once (tile dimensions, channels)
  - Enumerates tiles using simple grid arithmetic
  - Stores TIFF metadata in reader for executor
  - Separate plan builder class for modularity

**MRXS:**
  - Queries spatial R-tree index for intersecting tiles
  - Handles non-uniform tile layouts and overlaps
  - Calculates fractional offsets for subpixel positioning
  - Sets blend mode to ``kAverage`` for overlap regions

**QPTIFF:**
  - No separate plan builder (simpler structure)
  - Planning embedded directly in ``PrepareRequest``
  - Creates one operation per channel per tile
  - Sequential execution due to channel dependencies

Benefits of Two-Stage Architecture
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**1. Testability**
  - Plan generation can be unit tested without I/O
  - Mock file systems not required for planning tests
  - Execution can be tested with synthetic plans

**2. Optimization**
  - Plans can be analyzed before execution (cost estimation)
  - Tile operations can be reordered for better I/O patterns
  - Redundant tiles can be deduplicated across requests

**3. Caching**
  - Plans can be cached for repeated region reads
  - Cache keys: (filename, level, region) → TilePlan
  - Significant speedup for viewer panning/zooming

**4. Batching**
  - Multiple requests can be merged into single plan
  - Deduplicates overlapping tiles across requests
  - Enables efficient batch processing

**5. Profiling**
  - Clear separation between planning and execution time
  - Cost estimates can be validated against actual timing
  - Identifies bottlenecks (I/O vs decode vs planning)

Code Example: Using Two-Stage Pipeline
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include "fastslide/slide_reader.h"
   
   // Open slide
   auto reader = SlideReader::Open("slide.svs").value();
   
   // Create request
   core::TileRequest request;
   request.level = 0;
   request.region_bounds = {{1000, 1000}, {512, 512}};
   
   // Stage 1: Planning (no I/O)
   auto plan = reader->PrepareRequest(request).value();
   
   // Inspect plan before execution
   std::cout << "Tiles to read: " << plan.cost.total_tiles << std::endl;
   std::cout << "Estimated I/O: " << plan.cost.total_bytes_to_read / 1024 
             << " KB" << std::endl;
   
   // Stage 2: Execution (perform I/O)
   runtime::TileWriter writer(plan.output);
   reader->ExecutePlan(plan, writer);
   
   // Get final image
   auto image = writer.Finalize();

Format Reader Architecture
--------------------------

All format readers follow a common pattern:

.. doxygenclass:: fastslide::TiffBasedReader
   :no-link:

**Common Reader Pattern:**

.. mermaid::

   classDiagram
       class SlideReader {
           <<interface>>
           +PrepareRequest(request) TilePlan
           +ExecutePlan(plan, writer) Status
           +GetProperties() Properties
           +GetLevelCount() int
       }
       
       class TiffBasedReader {
           <<abstract>>
           +PrepareRequest(request) TilePlan
           +ExecutePlan(plan, writer) Status
           #ValidateFile() Status
           #LoadMetadata() Status
       }
       
       class MrxsReader {
           +Create(path) StatusOr~Reader~
           -ReadSlidedatIni() Status
           -BuildSpatialIndex() Status
       }
       
       class QpTiffReader {
           +Create(path) StatusOr~Reader~
           -LoadQpTiffMetadata() Status
       }
       
       class AperioReader {
           +Create(path) StatusOr~Reader~
           -ParseAperioMetadata() Status
           -BuildLevelInfo() Status
       }
       
       SlideReader <|-- TiffBasedReader
       TiffBasedReader <|-- MrxsReader
       TiffBasedReader <|-- QpTiffReader  
       TiffBasedReader <|-- AperioReader

Performance Architecture
------------------------

SIMD Optimization
~~~~~~~~~~~~~~~~~

FastSlide uses SIMD instructions for performance-critical operations:

.. doxygennamespace:: fastslide::resample
   :no-link:

**Resampling Pipeline:**

- **AVX2**: 256-bit vectorized operations for modern CPUs
- **Fallback**: Scalar implementation for compatibility
- **Runtime Detection**: Automatically selects best implementation

Spatial Indexing
~~~~~~~~~~~~~~~~

Efficient spatial queries using R-tree indexing.

**Benefits:**
- Fast tile lookup for large slides
- Optimized memory layout
- Cache-friendly access patterns

I/O Architecture
~~~~~~~~~~~~~~~~

**Async I/O Pattern:**
- Non-blocking file operations where supported
- Connection pooling for network resources (future)
- Memory-mapped files for large datasets

**TIFF Handling:**
- Multiple ``TIFF*`` handles per reader for thread safety
- Handle pooling to avoid open/close overhead
- Directory caching for metadata access

Extension Points
----------------

Adding New Formats
~~~~~~~~~~~~~~~~~~~

The architecture supports adding new formats in several ways:

1. **Built-in Readers**: Compile-time linked format support
2. **Plugin Readers**: Runtime-loaded shared libraries (future)
3. **External Readers**: Process-based readers via IPC (future)

Key interfaces to implement:

.. code-block:: cpp

   class MyFormatReader : public fastslide::TiffBasedReader {
   public:
       static absl::StatusOr<std::unique_ptr<MyFormatReader>> 
       Create(const std::string& path);
       
   protected:
       absl::Status ValidateFile() override;
       absl::Status LoadMetadata() override; 
       absl::StatusOr<fastslide::Image> ExecutePlan(
           const fastslide::BatchTilePlan& plan) override;
   };

Debugging & Observability
-------------------------

Built-in debugging support:

**Logging**
   - Structured logging with ``absl::log``
   - Configurable log levels per component
   - Performance metrics collection

**Cache Analytics**
   - Hit/miss ratios
   - Memory usage tracking
   - Access pattern analysis

**Error Reporting**
   - Detailed error messages with context
   - Stack traces in debug builds
   - File format validation errors

Security Architecture
---------------------

**Memory Safety**
   - No raw memory management
   - Buffer overflow protection via ``std::span``
   - Automatic bounds checking in debug builds

**Input Validation**
   - File format validation before processing
   - Metadata sanitization
   - Size limit enforcement

**Resource Limits**
   - Configurable memory limits
   - Timeout handling for I/O operations
   - Graceful degradation on resource exhaustion

Future Architecture Plans
-------------------------

**Planned Enhancements:**

1. **Network Support**: HTTP/S3 slide access
2. **Distributed Caching**: Redis/Memcached integration
3. **GPU Acceleration**: CUDA/OpenCL for resampling
4. **Cloud Integration**: Direct cloud storage access
5. **Streaming**: Progressive loading for large slides

**Plugin Ecosystem:**

- External format plugins via shared libraries  
- Custom processing pipelines
- Integration with existing tools (ImageJ, OMERO, etc.)
