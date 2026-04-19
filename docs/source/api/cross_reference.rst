C++ ↔ Python API Cross-Reference
================================

This section shows how the C++ and Python APIs correspond to each other, making it easy to translate between the two languages.

Core Classes Mapping
---------------------

.. list-table:: Main Classes
   :widths: 40 40 20
   :header-rows: 1

   * - C++
     - Python
     - Notes
   * - ``fastslide::SlideReader``
     - ``fastslide.FastSlide``
     - Main reader class
   * - ``fastslide::Image``
     - ``numpy.ndarray``
     - Images converted to NumPy arrays
   * - ``fastslide::Metadata``
     - ``dict``
     - Metadata as Python dictionary
   * - ``fastslide::LevelInfo``
     - ``tuple`` of ``(width, height)``
     - Level dimensions as tuples
   * - ``absl::Status``
     - Python exceptions
     - Errors converted to exceptions

Method Mapping
--------------

Reading Operations
~~~~~~~~~~~~~~~~~~

.. list-table::
   :widths: 45 45 10
   :header-rows: 1

   * - C++ Method
     - Python Method
     - Return Type
   * - ``SlideReader::Open(path)``
     - ``FastSlide.from_file_path(path)``
     - Reader/FastSlide
   * - ``reader->ReadRegion(spec)``
     - ``slide.read_region(location, level, size)``
     - Image/ndarray
   * - ``reader->GetProperties()``
     - ``slide.properties``
     - Properties/dict
   * - ``reader->GetLevelCount()``
     - ``slide.level_count``
     - int
   * - ``reader->GetDimensions()``
     - ``slide.dimensions``
     - tuple/tuple

Cache Operations
~~~~~~~~~~~~~~~~

.. list-table::
   :widths: 45 45 10
   :header-rows: 1

   * - C++ Method
     - Python Method  
     - Return Type
   * - ``GlobalTileCache::Create(capacity)``
     - ``CacheManager.create(capacity)``
     - Cache
   * - ``cache->GetStats()``
     - ``cache.get_detailed_stats()``
     - Stats
   * - ``cache->Clear()``
     - ``cache.clear()``
     - void
   * - ``reader->SetTileCache(cache)``
     - ``slide.set_cache_manager(cache)``
     - void

Data Type Conversion
--------------------

Image Data
~~~~~~~~~~

C++ ``fastslide::Image`` objects are automatically converted to NumPy arrays in Python:

.. code-block:: cpp

   // C++ - Native image object
   auto image_or = reader->ReadRegion(spec);
   const fastslide::Image& image = image_or.value();
   
   // Access raw data
   const auto* data = image.GetData<uint8_t>();
   auto [width, height] = image.GetDimensions();
   int channels = image.GetChannels();

.. code-block:: python

   # Python - NumPy array
   region = slide.read_region((0, 0), 0, (1024, 1024))
   
   # NumPy array properties
   height, width, channels = region.shape
   print(f"Data type: {region.dtype}")  # Usually uint8
   print(f"Memory layout: {region.flags}")

Coordinate Systems
~~~~~~~~~~~~~~~~~~

The coordinate system mapping:

.. list-table::
   :widths: 50 50
   :header-rows: 1

   * - C++
     - Python
   * - .. code-block:: cpp
       
          fastslide::RegionSpec spec{
              .bounds = {x, y, width, height},
              .level = level
          };
          
     - .. code-block:: python
       
          location = (x, y)
          size = (width, height)
          slide.read_region(location, level, size)

Error Handling Translation
--------------------------

C++ uses ``absl::StatusOr`` while Python uses exceptions:

.. list-table::
   :widths: 50 50
   :header-rows: 1

   * - C++
     - Python
   * - .. code-block:: cpp
       
          auto result = operation();
          if (!result.ok()) {
              // Handle error
              std::cerr << result.status();
              return;
          }
          auto value = std::move(result).value();
          
     - .. code-block:: python
       
          try:
              value = operation()
          except Exception as e:
              print(f"Error: {e}")
              return

Performance Considerations
--------------------------

Memory Efficiency
~~~~~~~~~~~~~~~~~

.. list-table::
   :widths: 25 35 40
   :header-rows: 1

   * - Operation
     - C++
     - Python
   * - **Copy Overhead**
     - Zero-copy when possible
     - Data copied to NumPy arrays
   * - **Memory Layout**
     - Configurable (planar/interleaved)
     - Always interleaved (HWC format)
   * - **Data Types**
     - Native C++ types
     - NumPy dtypes (usually uint8)

Thread Safety
~~~~~~~~~~~~~

Both APIs are fully thread-safe:

.. code-block:: cpp

   // C++ - Multiple threads can read simultaneously
   std::vector<std::thread> workers;
   for (int i = 0; i < num_threads; ++i) {
       workers.emplace_back([&reader, i]() {
           auto image = reader->ReadRegion(create_spec(i));
       });
   }

.. code-block:: python

   # Python - GIL released during C++ operations
   from concurrent.futures import ThreadPoolExecutor
   
   def read_tile(args):
       location, level, size = args
       return slide.read_region(location, level, size)
   
   with ThreadPoolExecutor() as executor:
       futures = [executor.submit(read_tile, spec) for spec in tile_specs]
       results = [f.result() for f in futures]

Migration Guide
---------------

From OpenSlide
~~~~~~~~~~~~~~

If you're migrating from OpenSlide, here are the key differences:

.. list-table::
   :widths: 40 40 20
   :header-rows: 1

   * - OpenSlide
     - FastSlide Python
     - Notes
   * - ``OpenSlide(path)``
     - ``FastSlide.from_file_path(path)``
     - Constructor
   * - ``slide.read_region(location, level, size)``
     - ``slide.read_region(location, level, size)``
     - Same signature!
   * - ``slide.properties``
     - ``slide.properties``
     - Same access pattern
   * - ``slide.associated_images``
     - ``slide.associated_images``
     - Same lazy loading

The FastSlide Python API is designed to be largely compatible with OpenSlide for easy migration.

Extending the APIs
------------------

Adding New Reader Types
~~~~~~~~~~~~~~~~~~~~~~~

To add support for a new slide format:

1. **C++**: Inherit from ``TiffBasedReader`` or implement ``SlideReader`` interface
2. **Python**: Bindings are automatically generated via the registry system
3. **Registration**: Use ``ReaderRegistry`` to register your new format

See :doc:`../guides/new_reader` for detailed instructions.

Custom Cache Implementation
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both APIs support custom cache implementations:

.. code-block:: cpp

   // C++ - Implement ITileCache interface
   class MyCustomCache : public fastslide::ITileCache {
       // Implement virtual methods
   };

.. code-block:: python

   # Python - Custom cache automatically available
   # if registered in C++ ReaderRegistry
