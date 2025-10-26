Python API Reference
====================

The FastSlide Python API provides a high-level, NumPy-integrated interface to the FastSlide C++ library.

.. currentmodule:: fastslide

Installation & Import
---------------------

.. code-block:: python

   import fastslide
   import numpy as np
   from pathlib import Path

Core Classes
------------

FastSlide (Main Reader)
~~~~~~~~~~~~~~~~~~~~~~~

.. autoclass:: fastslide.FastSlide
   :members:
   :inherited-members:
   :special-members: __enter__, __exit__
   
   The main slide reader class. This is the primary entry point for reading whole slide images.
   
   **Example Usage:**
   
   .. code-block:: python
   
      # Basic usage
      slide = fastslide.FastSlide.from_file_path("slide.mrxs")
      
      # With context manager (auto-cleanup)
      with fastslide.FastSlide.from_file_path("slide.svs") as slide:
          region = slide.read_region((0, 0), 0, (1024, 1024))
          print(f"Slide format: {slide.format}")
          print(f"Dimensions: {slide.dimensions}")

Cache Management
~~~~~~~~~~~~~~~~

.. autoclass:: fastslide.CacheManager
   :members:
   :inherited-members:
   
   Advanced cache management with detailed statistics and controls.

.. autoclass:: fastslide.TileCache  
   :members:
   :inherited-members:
   
   Basic tile cache interface.

.. autoclass:: fastslide.RuntimeGlobalCacheManager
   :members:
   :inherited-members:
   
   Global cache manager singleton using the modern cache injection system.

Cache Statistics
~~~~~~~~~~~~~~~~

.. autoclass:: fastslide.CacheInspectionStats
   :members:
   
   Detailed cache performance statistics.

.. autoclass:: fastslide.CacheStats
   :members:
   
   Basic cache statistics (for backward compatibility).

Associated Data Access
~~~~~~~~~~~~~~~~~~~~~~

.. autoclass:: fastslide.AssociatedImages
   :members:
   
   Dictionary-like access to associated images (thumbnails, labels, overview images).
   
   **Example:**
   
   .. code-block:: python
   
      slide = fastslide.FastSlide.from_file_path("slide.mrxs")
      
      # Check available associated images
      print("Available images:", slide.associated_images.keys())
      
      # Get thumbnail (lazy loaded)
      if "thumbnail" in slide.associated_images:
          thumbnail = slide.associated_images["thumbnail"]
          print(f"Thumbnail shape: {thumbnail.shape}")

.. autoclass:: fastslide.AssociatedData
   :members:
   
   Dictionary-like access to associated data (XML files, binary data).
   
   **Example:**
   
   .. code-block:: python
   
      # MRXS files often have embedded XML metadata
      slide = fastslide.FastSlide.from_file_path("slide.mrxs")
      
      if "SlideData.xml" in slide.associated_data:
          xml_content = slide.associated_data["SlideData.xml"]
          print("Slide metadata XML:", xml_content[:200])

Utility Functions
-----------------

.. autofunction:: fastslide.get_supported_extensions

.. autofunction:: fastslide.is_supported

Constants
---------

.. autodata:: fastslide.__version__
   
   Library version string.

.. autodata:: fastslide.RGB_CHANNELS
   
   Number of RGB channels (always 3).

Python API Examples
-------------------

Basic Slide Reading
~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   import fastslide
   import matplotlib.pyplot as plt
   
   # Open slide
   slide = fastslide.FastSlide.from_file_path("example.mrxs")
   
   # Read full resolution region
   region = slide.read_region((0, 0), 0, (2048, 2048))
   
   # Display with matplotlib
   plt.figure(figsize=(10, 10))
   plt.imshow(region)
   plt.axis('off')
   plt.title(f"Slide: {slide.format}")
   plt.show()

Multi-level Reading
~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   slide = fastslide.FastSlide.from_file_path("slide.svs")
   
   print(f"Available levels: {len(slide.level_dimensions)}")
   print("Level dimensions:")
   for i, dims in enumerate(slide.level_dimensions):
       downsample = slide.level_downsamples[i]
       print(f"  Level {i}: {dims} (downsample: {downsample:.2f}x)")
   
   # Read the same region at different levels
   location = (10000, 10000)
   size = (1024, 1024)
   
   for level in range(min(3, slide.level_count)):
       # Convert coordinates to level-native space
       native_loc = slide.convert_level0_to_level_native(*location, level)
       
       # Read region
       region = slide.read_region(native_loc, level, size)
       print(f"Level {level}: {region.shape}")

Cache Performance Optimization
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   slide = fastslide.FastSlide.from_file_path("large_slide.mrxs")
   
   # Create cache for better performance
   cache = fastslide.CacheManager.create(capacity=500)  # 500 tiles
   slide.set_cache_manager(cache)
   
   # Read overlapping regions (will benefit from caching)
   regions = []
   for i in range(5):
       x_offset = i * 256  # 50% overlap
       region = slide.read_region((x_offset, 0), 0, (512, 512))
       regions.append(region)
   
   # Check cache performance
   stats = cache.get_detailed_stats()
   print(f"Cache hit ratio: {stats.hit_ratio:.3f}")
   print(f"Memory usage: {stats.memory_usage_mb:.1f} MB")

Channel Selection
~~~~~~~~~~~~~~~~~

.. code-block:: python

   # Multi-channel slides (e.g., fluorescence)
   slide = fastslide.FastSlide.from_file_path("fluorescence.qptiff")
   
   # Show only specific channels
   slide.set_visible_channels([0, 2])  # Red and Blue channels
   region = slide.read_region((0, 0), 0, (1024, 1024))
   
   # Reset to show all channels
   slide.show_all_channels()
   full_region = slide.read_region((0, 0), 0, (1024, 1024))

Error Handling
~~~~~~~~~~~~~~

.. code-block:: python

   import fastslide
   
   try:
       slide = fastslide.FastSlide.from_file_path("nonexistent.svs")
   except Exception as e:
       print(f"Failed to open slide: {e}")
   
   # Check if format is supported before opening
   if fastslide.is_supported("unknown_format.xyz"):
       slide = fastslide.FastSlide.from_file_path("unknown_format.xyz")
   else:
       print("Format not supported")
       print("Supported extensions:", fastslide.get_supported_extensions())

Performance Best Practices
---------------------------

1. **Use Context Managers**: Always use ``with`` statements or manually call ``close()``
2. **Cache Management**: Use ``CacheManager`` for applications reading overlapping regions
3. **Level Selection**: Choose appropriate pyramid levels for your zoom requirements
4. **Batch Operations**: Read multiple regions in sequence to benefit from cache locality
5. **Memory Management**: Monitor cache usage with ``get_detailed_stats()``
