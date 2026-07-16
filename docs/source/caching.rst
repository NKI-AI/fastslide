.. _caching:

=============
Caching Guide
=============

FastSlide provides a sophisticated caching system that dramatically improves performance for repeated tile access through dependency injection and intelligent cache management.

.. contents:: Table of Contents
   :local:
   :depth: 2

Overview
========

The caching system stores **decoded** tile data in memory, avoiding expensive disk I/O and decompression operations on subsequent reads. Key features include:

- **Global caching**: Share a single cache across all readers
- **Per-reader caching**: Isolate caches for specific workflows
- **Zero overhead**: Optional—adds no overhead when disabled
- **Thread-safe**: All operations protected by mutexes
- **LRU eviction**: Automatic memory management
- **Format-specific**: Caches decoded camera images (MRXS) or tiles (TIFF)

Performance Impact
------------------

.. list-table:: Expected Performance Gains
   :header-rows: 1
   :widths: 30 35 35

   * - Format
     - First Read
     - Cached Read (Speedup)
   * - MRXS (JPEG)
     - 10-50ms (I/O + decode)
     - 0.1-1ms (**10-50×** faster)
   * - Aperio/QPTIFF
     - 5-20ms (I/O + decode)
     - 0.1-1ms (**5-20×** faster)

Architecture
============

System Components
-----------------

.. code-block:: none

   ┌─────────────────────────────────────┐
   │   GlobalCacheManager (Singleton)    │
   │   - Application-wide caching        │
   │   - Thread-safe singleton           │
   └──────────────┬──────────────────────┘
                  │ provides
                  ▼
   ┌─────────────────────────────────────┐
   │   ITileCache (Interface)            │
   │   - Get(TileKey) → CachedTileData   │
   │   - Put(TileKey, CachedTileData)    │
   │   - Clear(), GetStats()             │
   └──────────────┬──────────────────────┘
                  │ implemented by
                  ▼
   ┌─────────────────────────────────────┐
   │   LRUTileCache (Default)            │
   │   - O(1) LRU operations             │
   │   - Configurable capacity           │
   └─────────────────────────────────────┘
                  │
                  │ attached via
                  ▼
   ┌─────────────────────────────────────┐
   │   SlideReader::SetCache(cache)      │
   │   - Opt-in per reader               │
   │   - nullptr disables caching        │
   └──────────────┬──────────────────────┘
                  │ used by
                  ▼
   ┌─────────────────────────────────────┐
   │   Slide Readers                     │
   │   - Use cache during tile reads     │
   └─────────────────────────────────────┘

Cache Keys
----------

Cache keys uniquely identify tiles:

- **MRXS**: ``(dirname, level, fileno, byte_offset)``
- **TIFF**: ``(filename, level, tile_x, tile_y)``

What Gets Cached
----------------

.. important::

   FastSlide caches **decoded** image data, not raw compressed bytes.

**MRXS Format**
   Decoded camera images after JPEG/PNG/BMP decompression.
   
   *Rationale*: Decompression is the primary bottleneck.

**TIFF Formats** (Aperio, QPTIFF)
   Decoded tiles after decompression and planar interleaving.
   
   *Rationale*: TIFF tiles require decompression and possible format conversion.

C++ API
=======

Global Cache (Recommended)
---------------------------

The simplest and most efficient approach:

.. code-block:: cpp

   #include "fastslide/runtime/global_cache_manager.h"
   #include "fastslide/runtime/reader_registry.h"

   // Configure global cache at application startup (2 GiB)
   auto& cache_manager = fastslide::GlobalCacheManager::Instance();
   cache_manager.SetCapacityBytes(static_cast<size_t>(2) << 30);

   // Create a reader and attach the shared global cache.
   auto reader = fastslide::runtime::GetGlobalRegistry()
                     .CreateReader("slide.mrxs").value();
   reader->SetCache(cache_manager.GetCache());

   // First read - cache miss, decodes from disk
   auto region1 = reader->ReadRegion({
       .top_left = {1000, 2000}, .size = {512, 512}, .level = 0});

   // Second read - cache hit, no disk I/O!
   auto region2 = reader->ReadRegion({
       .top_left = {1000, 2000}, .size = {512, 512}, .level = 0});

   // Check cache statistics
   auto stats = cache_manager.GetStats();
   std::cout << "Cache hits: " << stats.hits << "\n";
   std::cout << "Cache misses: " << stats.misses << "\n";
   std::cout << "Hit ratio: " << (stats.hit_ratio * 100.0) << "%\n";

Per-Reader Cache
----------------

For isolated caching between readers:

.. code-block:: cpp

   #include "fastslide/runtime/lru_tile_cache.h"

   // Create a custom cache for this reader (512 MiB)
   auto cache = fastslide::LRUTileCache::Create(
       static_cast<size_t>(512) << 20).value();

   // Attach it to the reader.
   auto reader = registry.CreateReader("slide.mrxs").value();
   reader->SetCache(cache);

C API
=====

The C API exposes the same cache, so C and Rust consumers get decode reuse
without reimplementing tile-grid-aware caching:

.. code-block:: c

   #include "fastslide/c/fastslide.h"

   fastslide_registry_initialize();

   // Per-reader cache (256 MiB); 0 opens without a cache.
   FastSlideSlideReader* reader =
       fastslide_create_reader_with_cache("slide.svs", (size_t)256 << 20);

   // ... or attach later / switch to the shared global cache:
   fastslide_slide_reader_set_cache(reader, (size_t)512 << 20);
   fastslide_global_cache_set_capacity_bytes((size_t)2 << 30);
   fastslide_slide_reader_use_global_cache(reader);

   FastSlideCacheStats stats;
   if (fastslide_slide_reader_get_cache_stats(reader, &stats)) {
     printf("hit ratio: %.1f%%\n", stats.hit_ratio * 100.0);
   }
   fastslide_slide_reader_free(reader);

Rust API
========

The ``fastslide`` crate surfaces the cache on ``SlideReader``:

.. code-block:: rust

   use fastslide::{SlideReader, set_global_cache_capacity};

   // Per-reader cache (256 MiB).
   let reader = SlideReader::open_with_cache("slide.svs", 256 << 20)?;
   assert!(reader.is_cache_enabled());

   // Or attach after opening / use the shared global cache.
   reader.set_cache(512 << 20)?;
   set_global_cache_capacity(2 << 30)?;
   reader.use_global_cache()?;

   if let Some(stats) = reader.cache_stats() {
       println!("hit ratio: {:.1}%", stats.hit_ratio * 100.0);
   }

Python API
==========

``FastSlide.from_file_path`` accepts a ``cache`` argument (an int byte
capacity, a ``CacheManager``/``TileCache``, or ``None``):

.. code-block:: python

   import fastslide

   # Per-slide LRU cache (256 MiB).
   with fastslide.FastSlide.from_file_path("slide.svs", cache=256 << 20) as slide:
       slide.read_region((0, 0), 0, (256, 256))
       slide.read_region((0, 0), 0, (256, 256))  # served from cache
       print(slide.cache_stats.hit_ratio)

   # Share the process-wide global cache across slides.
   fastslide.GlobalCacheManager.instance().set_capacity_bytes(2 << 30)
   with fastslide.FastSlide.from_file_path("slide.svs") as slide:
       slide.use_global_cache()

Use Cases for Per-Reader Cache
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **Isolate training/validation**: Separate caches for different datasets
- **Multi-tenant**: Different caches per user/session
- **Testing**: Controlled cache environments
- **Memory budgets**: Different capacity per reader

Disabling Cache
---------------

To disable caching entirely:

.. code-block:: cpp

   // Option 1: Never attach a cache (default) - readers decode every tile.
   auto reader = registry.CreateReader("slide.mrxs").value();

   // Option 2: Detach an existing cache.
   reader->SetCache(nullptr);

Cache Statistics
----------------

Monitor cache effectiveness:

.. code-block:: cpp

   auto& cache_mgr = fastslide::GlobalCacheManager::Instance();
   auto stats = cache_mgr.GetStats();

   std::cout << "Capacity: "
             << (stats.capacity_bytes / (1024.0 * 1024.0)) << " MB\n";
   std::cout << "Current size: " << stats.size << " tiles\n";
   std::cout << "Hits: " << stats.hits << "\n";
   std::cout << "Misses: " << stats.misses << "\n";
   std::cout << "Hit ratio: " << (stats.hit_ratio * 100.0) << "%\n";
   std::cout << "Memory usage: " 
             << (stats.memory_usage_bytes / 1e6) << " MB\n";

Cache Management
----------------

.. code-block:: cpp

   auto& cache_mgr = fastslide::GlobalCacheManager::Instance();

   // Clear cache (free memory)
   cache_mgr.Clear();

   // Change capacity (clears existing cache); here 4 GiB
   cache_mgr.SetCapacityBytes(static_cast<size_t>(4) << 30);

   // Get current capacity (bytes)
   size_t capacity_bytes = cache_mgr.GetCapacityBytes();

   // Get current size
   size_t size = cache_mgr.GetSize();

Python API
==========

Global Cache
------------

.. code-block:: python

   import fastslide

   # Configure global cache at startup (2 GiB)
   cache_mgr = fastslide.GlobalCacheManager.instance()
   cache_mgr.set_capacity_bytes(2 * 1024**3)

   # Create reader - global cache automatically used
   slide = fastslide.FastSlide.from_file_path("slide.mrxs")

   # Read regions - caching happens transparently
   region = slide.read_region(
       location=(1000, 2000),
       level=0,
       size=(512, 512)
   )

   # Check cache statistics
   stats = cache_mgr.get_stats()
   print(f"Cache hits: {stats.hits}")
   print(f"Cache misses: {stats.misses}")
   print(f"Hit ratio: {stats.hit_ratio * 100:.2f}%")
   print(f"Memory: {stats.memory_usage_bytes / 1024 / 1024:.2f} MB")

Cache Management
----------------

.. code-block:: python

   cache_mgr = fastslide.GlobalCacheManager.instance()

   # Clear cache
   cache_mgr.clear()

   # Change capacity (4 GiB)
   cache_mgr.set_capacity_bytes(4 * 1024**3)

   # Query current state
   capacity_bytes = cache_mgr.get_capacity_bytes()
   size = cache_mgr.get_size()
   stats = cache_mgr.get_stats()

Examples
========

Example 1: Interactive Viewer
------------------------------

Configure a large cache for interactive panning and zooming:

.. code-block:: cpp

   #include "fastslide/runtime/global_cache_manager.h"
   #include "fastslide/runtime/reader_registry.h"

   // Large cache for interactive viewing (~8 GiB)
   auto& cache = fastslide::GlobalCacheManager::Instance();
   cache.SetCapacityBytes(static_cast<size_t>(8) << 30);

   // Setup registry
   fastslide::ReaderRegistry registry;
   registry.RegisterFormat(/* ... */);

   // Create reader and attach the global cache
   auto reader = registry.CreateReader("slide.svs").value();
   reader->SetCache(cache.GetCache());

   // User interaction loop
   for (const auto& pan_event : user_interactions) {
     auto region = reader->ReadRegion(pan_event.region_spec);
     display_image(region);
   }

   // Log effectiveness
   auto stats = cache.GetStats();
   std::cout << "Hit ratio: " << (stats.hit_ratio * 100.0) << "%\n";

Example 2: ML Training Pipeline
--------------------------------

Separate caches for training and validation:

.. code-block:: cpp

   #include "fastslide/runtime/lru_tile_cache.h"

   // Training cache (~4 GiB)
   auto train_cache =
       fastslide::LRUTileCache::Create(static_cast<size_t>(4) << 30).value();

   // Validation cache (~1 GiB)
   auto val_cache =
       fastslide::LRUTileCache::Create(static_cast<size_t>(1) << 30).value();

   // Create readers with isolated caches (attach the shared cache per reader)
   std::vector<std::unique_ptr<fastslide::SlideReader>> train_readers;
   for (const auto& path : training_slides) {
     auto reader = registry.CreateReader(path).value();
     reader->SetCache(train_cache);
     train_readers.push_back(std::move(reader));
   }

   std::vector<std::unique_ptr<fastslide::SlideReader>> val_readers;
   for (const auto& path : validation_slides) {
     auto reader = registry.CreateReader(path).value();
     reader->SetCache(val_cache);
     val_readers.push_back(std::move(reader));
   }

   // Training loop with separate cache statistics
   for (int epoch = 0; epoch < num_epochs; ++epoch) {
     // Train with training cache
     train_epoch(train_readers);
     
     // Validate with validation cache
     validate_epoch(val_readers);
     
     // Log cache stats
     std::cout << "Epoch " << epoch << ":\n";
     std::cout << "  Train cache hit ratio: " 
               << (train_cache->GetStats().hit_ratio * 100.0) << "%\n";
     std::cout << "  Val cache hit ratio: " 
               << (val_cache->GetStats().hit_ratio * 100.0) << "%\n";
   }

Example 3: Batch Processing
----------------------------

Python batch processing with monitoring:

.. code-block:: python

   import fastslide
   from pathlib import Path

   # Configure reasonable cache (~1.5 GiB)
   cache_mgr = fastslide.GlobalCacheManager.instance()
   cache_mgr.set_capacity_bytes(int(1.5 * 1024**3))

   # Process multiple slides
   slide_paths = list(Path("slides/").glob("*.mrxs"))
   
   for i, slide_path in enumerate(slide_paths):
       slide = fastslide.FastSlide.from_file_path(str(slide_path))
       
       # Extract multi-scale patches
       for level in range(slide.level_count):
           region = slide.read_region(
               location=(0, 0),
               level=level,
               size=(1024, 1024)
           )
           process_region(region)
       
       # Monitor cache performance
       stats = cache_mgr.get_stats()
       print(f"Slide {i+1}/{len(slide_paths)}: {slide_path.name}")
       print(f"  Hit ratio: {stats.hit_ratio * 100:.1f}%")
       print(f"  Memory: {stats.memory_usage_bytes / 1e6:.1f} MB")
       
       # Clear cache between slides if memory constrained
       if i % 10 == 9:
           cache_mgr.clear()
           print("  Cache cleared")

Configuration
=============

Choosing Cache Size
-------------------

The optimal cache size depends on your workload:

.. list-table:: Cache Size Guidelines
   :header-rows: 1
   :widths: 30 35 35

   * - Access Pattern
     - Recommended Size
     - Rationale
   * - Random access
     - 4-8 GiB
     - Large working set
   * - Sequential scan
     - 512 MiB - 2 GiB
     - Small working set
   * - Memory constrained
     - 256-512 MiB
     - Limited resources
   * - Interactive viewer
     - 4-12 GiB
     - User panning/zooming

Memory Footprint
----------------

Cache capacity is a byte budget. The cache stops admitting new tiles
when the total stored tile bytes would exceed the configured capacity:

.. code-block:: none

   Per tile (512x512 RGB) ~ 512 * 512 * 3 = 786,432 bytes (~768 KiB)

   Capacity   Approx. tiles cached
   --------   --------------------
   512 MiB    ~ 680
   1 GiB      ~ 1,360
   4 GiB      ~ 5,460
   8 GiB      ~ 10,920

.. warning::

   Ensure sufficient RAM is available. The cache uses physical memory
   and is **not** swapped to disk.

When to Enable Caching
----------------------

✅ **Enable caching when:**

- Reading overlapping regions repeatedly
- Interactive viewers with panning/zooming
- Training ML models with multiple epochs
- Processing with sliding windows
- Multi-scale analysis of same regions

❌ **Disable caching when:**

- Single-pass sequential reads
- Each region read only once (random access)
- Memory is severely constrained
- Tiles are very large (>2 MB each)

Performance Tuning
==================

Monitoring Hit Ratio
--------------------

Monitor cache effectiveness:

.. code-block:: cpp

   auto stats = cache_manager.GetStats();
   double hit_ratio = stats.hit_ratio;

   if (hit_ratio < 0.3) {
     std::cout << "WARNING: Low hit ratio (<30%)\n";
     std::cout << "Consider:\n";
     std::cout << "  - Increasing cache capacity\n";
     std::cout << "  - Verifying access patterns\n";
     std::cout << "  - Checking if caching is beneficial\n";
   } else if (hit_ratio > 0.8) {
     std::cout << "GOOD: High hit ratio (>80%)\n";
     std::cout << "Cache is effective for this workload\n";
   }

Adaptive Sizing
---------------

Dynamically adjust cache size based on hit ratio:

.. code-block:: cpp

   void AdaptCacheSize(fastslide::GlobalCacheManager& cache_mgr) {
     auto stats = cache_mgr.GetStats();
     constexpr size_t kMaxBytes = static_cast<size_t>(8) << 30;  // 8 GiB
     constexpr size_t kMinBytes = static_cast<size_t>(256) << 20;  // 256 MiB

     if (stats.hit_ratio < 0.5 && stats.capacity_bytes < kMaxBytes) {
       size_t new_capacity_bytes =
           std::min(stats.capacity_bytes * 2, kMaxBytes);
       cache_mgr.SetCapacityBytes(new_capacity_bytes);
     } else if (stats.hit_ratio > 0.9 && stats.capacity_bytes > kMinBytes) {
       size_t new_capacity_bytes =
           std::max(stats.capacity_bytes / 2, kMinBytes);
       cache_mgr.SetCapacityBytes(new_capacity_bytes);
     }
   }

Multi-threaded Access
---------------------

The cache is thread-safe and can be accessed from multiple threads:

.. code-block:: cpp

   auto& cache_mgr = fastslide::GlobalCacheManager::Instance();
   cache_mgr.SetCapacityBytes(static_cast<size_t>(8) << 30);  // 8 GiB

   // Create multiple readers sharing the same cache
   std::vector<std::unique_ptr<fastslide::SlideReader>> readers;
   for (const auto& path : slide_paths) {
     auto reader = registry.CreateReader(path).value();
     reader->SetCache(cache_mgr.GetCache());
     readers.push_back(std::move(reader));
   }

   // Process in parallel - cache is thread-safe
   #pragma omp parallel for
   for (size_t i = 0; i < readers.size(); ++i) {
     auto region = readers[i]->ReadRegion(/* ... */);
     process(region);
   }

Troubleshooting
===============

Cache Not Working
-----------------

If caching isn't providing benefits:

.. code-block:: cpp

   // Check that a cache is attached to the reader
   if (!reader->IsCacheEnabled()) {
     std::cerr << "ERROR: No cache attached to reader!\n";
   }

   // Verify cache is being used
   auto stats = cache_mgr.GetStats();
   if (stats.hits + stats.misses == 0) {
     std::cerr << "ERROR: Cache not being accessed!\n";
   }

Memory Issues
-------------

If hitting memory limits:

.. code-block:: cpp

   // Monitor memory usage
   auto stats = cache_mgr.GetStats();
   double memory_mb = stats.memory_usage_bytes / 1024.0 / 1024.0;
   
   if (memory_mb > 5000.0) {  // More than 5 GB
     std::cout << "WARNING: High memory usage: " 
               << memory_mb << " MB\n";
     
     // Reduce capacity
     cache_mgr.SetCapacityBytes(cache_mgr.GetCapacityBytes() / 2);
     
     // Or disable caching
     // cache_mgr.Clear();
   }

Poor Hit Ratio
--------------

If hit ratio is low (<30%):

1. **Increase cache capacity**

   .. code-block:: cpp

      cache_mgr.SetCapacityBytes(cache_mgr.GetCapacityBytes() * 2);

2. **Check access patterns**

   Sequential access patterns may not benefit from caching.

3. **Verify workload**

   If each tile is read only once, caching provides no benefit.

4. **Monitor statistics over time**

   Hit ratio improves as cache warms up.

Best Practices
==============

1. **Configure Once at Startup**

   Set global cache capacity before creating readers:

   .. code-block:: cpp

      // At application initialization (2 GiB)
      auto& cache = fastslide::GlobalCacheManager::Instance();
      cache.SetCapacityBytes(static_cast<size_t>(2) << 30);

2. **Use Global Cache by Default**

   Unless you have specific isolation requirements:

   .. code-block:: cpp

      auto& cache = fastslide::GlobalCacheManager::Instance();
      reader->SetCache(cache.GetCache());

3. **Monitor Statistics Periodically**

   Log cache performance to validate benefits:

   .. code-block:: cpp

      auto stats = cache_mgr.GetStats();
      std::cerr << "Cache: " << stats.hits << " hits, " 
                << stats.misses << " misses, "
                << (stats.hit_ratio * 100.0) << "% hit ratio\n";

4. **Clear Between Datasets**

   When switching to different slides:

   .. code-block:: cpp

      cache_mgr.Clear();  // Free memory for new working set

5. **Profile Memory Usage**

   Ensure cache fits in available RAM:

   .. code-block:: cpp

      auto stats = cache_mgr.GetStats();
      double memory_gb = stats.memory_usage_bytes / 1e9;
      if (memory_gb > available_ram_gb * 0.8) {
        cache_mgr.SetCapacityBytes(cache_mgr.GetCapacityBytes() / 2);
      }

Advanced Topics
===============

Custom Cache Implementation
---------------------------

Implement custom caching strategies by inheriting from ``ITileCache``:

.. code-block:: cpp

   #include "fastslide/runtime/cache_interface.h"

   class GPUCache : public fastslide::ITileCache {
    public:
     std::shared_ptr<fastslide::CachedTileData> Get(
         const fastslide::TileKey& key) override {
       // Implement GPU-based caching
     }
     
     void Put(const fastslide::TileKey& key,
              std::shared_ptr<fastslide::CachedTileData> tile) override {
       // Store on GPU
     }
     
     // Implement other ITileCache methods...
   };

   // Use custom cache
   auto custom_cache = std::make_shared<GPUCache>();
   reader->SetCache(custom_cache);

Distributed Caching
-------------------

For cluster environments, implement a distributed cache:

.. code-block:: cpp

   class DistributedCache : public fastslide::ITileCache {
    public:
     DistributedCache(const std::string& redis_host) 
         : redis_client_(redis_host) {}
     
     std::shared_ptr<fastslide::CachedTileData> Get(
         const fastslide::TileKey& key) override {
       // Fetch from Redis/memcached
       return redis_client_.Get(KeyToString(key));
     }
     
     void Put(const fastslide::TileKey& key,
              std::shared_ptr<fastslide::CachedTileData> tile) override {
       // Store in distributed cache
       redis_client_.Set(KeyToString(key), tile);
     }
     
    private:
     RedisClient redis_client_;
   };

Cache Warming
-------------

Pre-populate the cache for known access patterns:

.. code-block:: cpp

   void WarmCache(fastslide::SlideReader* reader,
                  const std::vector<RegionSpec>& regions) {
     for (const auto& region : regions) {
       // Read to populate cache
       auto result = reader->ReadRegion(region);
       // Discard result, cache is now populated
     }
     
     auto stats = cache_mgr.GetStats();
     std::cout << "Cache warmed: " << stats.size << " tiles\n";
   }

API Reference
=============

C++ Classes
-----------

``GlobalCacheManager``
   Singleton manager for application-wide caching. All capacities are in
   bytes.

   .. code-block:: cpp

      static GlobalCacheManager& Instance();
      std::shared_ptr<ITileCache> GetCache();
      void SetCache(std::shared_ptr<ITileCache> cache);
      aifocore::Status SetCapacityBytes(size_t capacity_bytes);
      size_t GetCapacityBytes() const;
      size_t GetSize() const;
      ITileCache::Stats GetStats() const;
      void Clear();

``LRUTileCache``
   Default LRU cache implementation. Capacity is expressed in bytes.

   .. code-block:: cpp

      static aifocore::Result<std::shared_ptr<LRUTileCache>>
          Create(size_t capacity_bytes = 1024 * 1024 * 1024);  // 1 GiB

      std::shared_ptr<CachedTileData> Get(const TileKey& key) override;
      void Put(const TileKey& key,
               std::shared_ptr<CachedTileData> tile) override;
      void Clear() override;
      Stats GetStats() const override;
      aifocore::Status SetCapacityBytes(size_t capacity_bytes);

``SlideReader`` (cache methods)
   Caching is attached per reader; there is no dependency-injection struct.

   .. code-block:: cpp

      void SetCache(std::shared_ptr<ITileCache> cache);  // nullptr disables
      std::shared_ptr<ITileCache> GetCache() const;
      bool IsCacheEnabled() const;

C API
-----

Declared in ``fastslide/c/slide_reader.h`` and ``fastslide/c/registry.h``:

.. code-block:: c

   FastSlideSlideReader* fastslide_create_reader_with_cache(
       const char* file_path, size_t cache_capacity_bytes);
   int  fastslide_slide_reader_set_cache(
       FastSlideSlideReader* reader, size_t capacity_bytes);  // 0 disables
   int  fastslide_slide_reader_use_global_cache(FastSlideSlideReader* reader);
   int  fastslide_slide_reader_is_cache_enabled(
       const FastSlideSlideReader* reader);
   void fastslide_slide_reader_clear_cache(FastSlideSlideReader* reader);
   int  fastslide_slide_reader_get_cache_stats(
       const FastSlideSlideReader* reader, FastSlideCacheStats* out_stats);

   int  fastslide_global_cache_set_capacity_bytes(size_t capacity_bytes);
   int  fastslide_global_cache_get_stats(FastSlideCacheStats* out_stats);
   void fastslide_global_cache_clear(void);

Python Classes
--------------

``GlobalCacheManager``
   Global cache manager for Python. Capacities are in bytes.

   .. code-block:: python

      @staticmethod
      def instance() -> GlobalCacheManager

      def set_capacity_bytes(capacity_bytes: int) -> None
      def get_capacity_bytes() -> int
      def get_size() -> int
      def get_stats() -> RuntimeCacheStats
      def clear() -> None

``RuntimeCacheStats``
   Cache statistics structure.

   .. code-block:: python

      capacity_bytes: int
      size: int                # number of cached tiles
      hits: int
      misses: int
      hit_ratio: float
      memory_usage_bytes: int

See Also
========

- :doc:`architecture` - Overall FastSlide architecture
- :doc:`api/index` - Complete API documentation
- :doc:`guides/index` - Guides and tutorials

.. note::

   For questions or issues with the caching system, please consult
   the project's GitHub repository or documentation website.
