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
                  │ injected via
                  ▼
   ┌─────────────────────────────────────┐
   │   ReaderDependencies                │
   │   - tile_cache                      │
   │   - enable_caching                  │
   └──────────────┬──────────────────────┘
                  │ passed to
                  ▼
   ┌─────────────────────────────────────┐
   │   Format Plugins                    │
   │   - CreateMrxsReader()              │
   │   - CreateAperioReader()            │
   └──────────────┬──────────────────────┘
                  │ creates
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
   #include "fastslide/runtime/reader_dependencies.h"

   // Configure global cache at application startup
   auto& cache_manager = fastslide::GlobalCacheManager::Instance();
   cache_manager.SetCapacity(5000);  // 5000 tiles

   // Register formats
   fastslide::ReaderRegistry registry;
   registry.RegisterFormat(
       fastslide::formats::mrxs::CreateMrxsFormatDescriptor());
   registry.RegisterFormat(
       fastslide::formats::aperio::CreateAperioFormatDescriptor());

   // Create reader with global cache (automatic injection)
   auto deps = fastslide::ReaderDependencies::WithGlobalCache();
   auto reader_or = registry.CreateReader("slide.mrxs", deps);

   if (reader_or.ok()) {
     auto reader = std::move(*reader_or);
     
     // First read - cache miss, decodes from disk
     auto region1 = reader->ReadRegion({
         .top_left = {1000, 2000},
         .size = {512, 512},
         .level = 0
     });
     
     // Second read - cache hit, no disk I/O!
     auto region2 = reader->ReadRegion({
         .top_left = {1000, 2000},
         .size = {512, 512},
         .level = 0
     });
     
     // Check cache statistics
     auto stats = cache_manager.GetStats();
     std::cout << "Cache hits: " << stats.hits << "\n";
     std::cout << "Cache misses: " << stats.misses << "\n";
     std::cout << "Hit ratio: " << (stats.hit_ratio * 100.0) << "%\n";
     std::cout << "Memory: " 
               << (stats.memory_usage_bytes / 1024.0 / 1024.0) 
               << " MB\n";
   }

Per-Reader Cache
----------------

For isolated caching between readers:

.. code-block:: cpp

   #include "fastslide/runtime/lru_tile_cache.h"

   // Create custom cache for this reader
   auto cache_or = fastslide::LRUTileCache::Create(1000);  // 1000 tiles
   if (!cache_or.ok()) {
     // Handle error
     return cache_or.status();
   }

   // Inject via dependencies
   auto deps = fastslide::ReaderDependencies::WithCache(*cache_or);
   auto reader_or = registry.CreateReader("slide.mrxs", deps);

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

   // Option 1: No cache in dependencies (default)
   auto reader_or = registry.CreateReader("slide.mrxs");

   // Option 2: Explicitly disable
   fastslide::ReaderDependencies deps;
   deps.enable_caching = false;
   auto reader_or = registry.CreateReader("slide.mrxs", deps);

Cache Statistics
----------------

Monitor cache effectiveness:

.. code-block:: cpp

   auto& cache_mgr = fastslide::GlobalCacheManager::Instance();
   auto stats = cache_mgr.GetStats();

   std::cout << "Capacity: " << stats.capacity << " tiles\n";
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

   // Change capacity (clears existing cache)
   cache_mgr.SetCapacity(10000);

   // Get current capacity
   size_t capacity = cache_mgr.GetCapacity();

   // Get current size
   size_t size = cache_mgr.GetSize();

Python API
==========

Global Cache
------------

.. code-block:: python

   import fastslide

   # Configure global cache at startup
   cache_mgr = fastslide.RuntimeGlobalCacheManager.instance()
   cache_mgr.set_capacity(5000)  # 5000 tiles

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

   cache_mgr = fastslide.RuntimeGlobalCacheManager.instance()

   # Clear cache
   cache_mgr.clear()

   # Change capacity
   cache_mgr.set_capacity(10000)

   # Query current state
   capacity = cache_mgr.get_capacity()
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

   // Large cache for interactive viewing
   auto& cache = fastslide::GlobalCacheManager::Instance();
   cache.SetCapacity(10000);  // 10K tiles (~7.5 GB for 512×512 RGB)

   // Setup registry
   fastslide::ReaderRegistry registry;
   registry.RegisterFormat(/* ... */);

   // Create reader with global cache
   auto deps = fastslide::ReaderDependencies::WithGlobalCache();
   auto reader = registry.CreateReader("slide.svs", deps).value();

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

   // Training cache (larger)
   auto train_cache = fastslide::LRUTileCache::Create(5000).value();
   fastslide::ReaderDependencies train_deps;
   train_deps.tile_cache = train_cache;

   // Validation cache (smaller)
   auto val_cache = fastslide::LRUTileCache::Create(2000).value();
   fastslide::ReaderDependencies val_deps;
   val_deps.tile_cache = val_cache;

   // Create readers with isolated caches
   std::vector<std::unique_ptr<fastslide::SlideReader>> train_readers;
   for (const auto& path : training_slides) {
     train_readers.push_back(
         registry.CreateReader(path, train_deps).value());
   }

   std::vector<std::unique_ptr<fastslide::SlideReader>> val_readers;
   for (const auto& path : validation_slides) {
     val_readers.push_back(
         registry.CreateReader(path, val_deps).value());
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

   # Configure reasonable cache
   cache_mgr = fastslide.RuntimeGlobalCacheManager.instance()
   cache_mgr.set_capacity(2000)  # ~1.5 GB

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
     - 5000-10000 tiles
     - Large working set
   * - Sequential scan
     - 1000-2000 tiles
     - Small working set
   * - Memory constrained
     - 500-1000 tiles
     - Limited resources
   * - Interactive viewer
     - 5000-15000 tiles
     - User panning/zooming

Memory Footprint
----------------

Calculate memory usage for a given cache size:

.. code-block:: none

   Memory (bytes) = tiles × width × height × channels
   
   Example: 512×512 RGB tiles
   - Per tile: 512 × 512 × 3 = 786,432 bytes (~768 KB)
   - 1000 tiles: ~768 MB
   - 5000 tiles: ~3.75 GB
   - 10000 tiles: ~7.5 GB

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
     
     if (stats.hit_ratio < 0.5 && stats.capacity < 10000) {
       // Increase capacity for low hit ratio
       size_t new_capacity = std::min(stats.capacity * 2, 10000ul);
       cache_mgr.SetCapacity(new_capacity);
       std::cout << "Increased cache to " << new_capacity << " tiles\n";
     } else if (stats.hit_ratio > 0.9 && stats.capacity > 1000) {
       // Decrease capacity if very high hit ratio
       size_t new_capacity = std::max(stats.capacity / 2, 1000ul);
       cache_mgr.SetCapacity(new_capacity);
       std::cout << "Decreased cache to " << new_capacity << " tiles\n";
     }
   }

Multi-threaded Access
---------------------

The cache is thread-safe and can be accessed from multiple threads:

.. code-block:: cpp

   auto& cache_mgr = fastslide::GlobalCacheManager::Instance();
   cache_mgr.SetCapacity(10000);

   auto deps = fastslide::ReaderDependencies::WithGlobalCache();

   // Create multiple readers sharing the same cache
   std::vector<std::unique_ptr<fastslide::SlideReader>> readers;
   for (const auto& path : slide_paths) {
     readers.push_back(registry.CreateReader(path, deps).value());
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

   // Check that cache is enabled
   auto deps = fastslide::ReaderDependencies::WithGlobalCache();
   if (!deps.HasCache()) {
     std::cerr << "ERROR: Cache not available!\n";
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
     cache_mgr.SetCapacity(cache_mgr.GetCapacity() / 2);
     
     // Or disable caching
     // cache_mgr.Clear();
   }

Poor Hit Ratio
--------------

If hit ratio is low (<30%):

1. **Increase cache capacity**

   .. code-block:: cpp

      cache_mgr.SetCapacity(cache_mgr.GetCapacity() * 2);

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

      // At application initialization
      auto& cache = fastslide::GlobalCacheManager::Instance();
      cache.SetCapacity(5000);

2. **Use Global Cache by Default**

   Unless you have specific isolation requirements:

   .. code-block:: cpp

      auto deps = fastslide::ReaderDependencies::WithGlobalCache();

3. **Monitor Statistics Periodically**

   Log cache performance to validate benefits:

   .. code-block:: cpp

      auto stats = cache_mgr.GetStats();
      LOG(INFO) << "Cache: " << stats.hits << " hits, " 
                << stats.misses << " misses, "
                << (stats.hit_ratio * 100.0) << "% hit ratio";

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
        cache_mgr.SetCapacity(cache_mgr.GetCapacity() / 2);
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
   auto deps = fastslide::ReaderDependencies::WithCache(custom_cache);

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
   Singleton manager for application-wide caching.

   .. code-block:: cpp

      static GlobalCacheManager& Instance();
      std::shared_ptr<ITileCache> GetCache();
      void SetCache(std::shared_ptr<ITileCache> cache);
      absl::Status SetCapacity(size_t capacity);
      size_t GetCapacity() const;
      size_t GetSize() const;
      ITileCache::Stats GetStats() const;
      void Clear();

``LRUTileCache``
   Default LRU cache implementation.

   .. code-block:: cpp

      static absl::StatusOr<std::shared_ptr<LRUTileCache>> 
          Create(size_t capacity = 1000);
      
      std::shared_ptr<CachedTileData> Get(const TileKey& key) override;
      void Put(const TileKey& key, 
               std::shared_ptr<CachedTileData> tile) override;
      void Clear() override;
      Stats GetStats() const override;

``ReaderDependencies``
   Dependency injection container.

   .. code-block:: cpp

      static ReaderDependencies WithGlobalCache();
      static ReaderDependencies WithCache(
          std::shared_ptr<ITileCache> cache);
      
      std::shared_ptr<ITileCache> tile_cache;
      bool enable_caching = true;
      bool HasCache() const;

Python Classes
--------------

``RuntimeGlobalCacheManager``
   Global cache manager for Python.

   .. code-block:: python

      @staticmethod
      def instance() -> RuntimeGlobalCacheManager
      
      def set_capacity(capacity: int) -> None
      def get_capacity() -> int
      def get_size() -> int
      def get_stats() -> RuntimeCacheStats
      def clear() -> None

``RuntimeCacheStats``
   Cache statistics structure.

   .. code-block:: python

      capacity: int
      size: int
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
