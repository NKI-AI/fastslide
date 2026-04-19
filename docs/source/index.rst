FastSlide Documentation
=======================

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   formats/index
   api/index

About FastSlide
---------------

FastSlide is a high-performance C++ library for reading whole slide images (WSI) in digital pathology, 
with native Python bindings for integration into machine learning and scientific computing workflows.

**Design Goals:**

- **Fast C++ Core**: Built with modern C++20 for maximum performance
- **Native Python Bindings**: Zero-overhead integration using pybind11
- **Thread-Safe**: Safe for multi-threaded applications and PyTorch DataLoaders
- **Format Support**: MRXS (3DHistech), Aperio SVS, and QPTIFF formats

**Documentation:**

- **Python API**: :doc:`api/python_api` - Complete Python interface documentation
- **C++ API**: :doc:`api/cpp_api` - C++ interface overview, or see `Doxygen Documentation <../doxygen/index.html>`_ for full details
- **Format Support**: :doc:`formats/index` - Supported file formats

Quick Start
-----------

Python API
~~~~~~~~~~

.. code-block:: python

   import fastslide
   
   # Open a slide
   slide = fastslide.SlideReader("slide.mrxs")
   
   # Read a region
   region = slide.read_region((0, 0), 0, (1024, 1024))
   
   # Get slide properties
   print(f"Slide dimensions: {slide.dimensions}")
   print(f"Available levels: {len(slide.level_dimensions)}")

C++ API
~~~~~~~

.. code-block:: cpp

   #include <fastslide/slide_reader.h>
   
   auto reader = fastslide::SlideReader::Open("slide.mrxs");
   if (!reader) {
       // Handle error
       return;
   }
   
   // Read a region at level 0
   auto image = reader->ReadRegion(0, {0, 0}, {1024, 1024});
   
   // Get slide properties
   auto properties = reader->GetProperties();
   std::cout << "Slide dimensions: " 
             << properties.base_dimensions.width << "x" 
             << properties.base_dimensions.height << std::endl;