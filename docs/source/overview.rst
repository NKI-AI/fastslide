Overview
========

FastSlide is a high-performance, cross-platform library for reading digital pathology whole slide images. It provides both C++ and Python APIs with a focus on performance, memory safety, and ease of use.

.. raw:: html

   <div style="text-align: center; margin: 20px 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); border-radius: 10px; color: white;">
       <h2 style="margin: 0; color: white;">🔬 Digital Pathology Slide Reader</h2>
       <p style="margin: 10px 0 0 0; color: white;">High-performance • Thread-safe • Multi-format</p>
   </div>

Key Features
------------

🚀 **Performance**
   - Multi-threaded tile reading
   - SIMD-optimized image processing
   - Memory-efficient caching system
   - Zero-copy operations where possible

🔒 **Memory Safety**
   - Modern C++20 with RAII patterns
   - No raw pointers or manual memory management
   - ``absl::StatusOr`` for error handling (no exceptions)
   - Bounds-checked array access

🌐 **Multi-Platform**
   - macOS and Linux support
   - x86_64 and ARM64 architectures  
   - Bazel build system
   - CI/CD tested on all platforms (currently internally in our monorepo)

📁 **Format Support**
   - **MRXS** (3DHistech Pannoramic): Full support including associated data
   - **Aperio SVS**: Complete SVS format support
   - **QPTIFF** (Quantitative Pathology TIFF): High-performance reading
   - **Generic TIFF**: Fallback for standard TIFF files

🐍 **Dual APIs**
   - **C++ API**: Native high-performance interface
   - **Python API**: NumPy-integrated bindings via pybind11
   - OpenSlide-compatible Python interface for easy migration

Supported File Formats
-----------------------

.. list-table:: Format Compatibility Matrix
   :widths: 20 15 15 15 15 20
   :header-rows: 1

   * - Format
     - Extension
     - Pyramids
     - Associated Images  
     - Metadata
     - Special Features
   * - **MRXS**
     - ``.mrxs``
     - ✅ Multi-level
     - ✅ Thumbnails, Labels
     - ✅ Rich XML metadata
     - Non-hierarchical layers
   * - **Aperio SVS**
     - ``.svs``
     - ✅ Multi-level
     - ✅ Macro, Label, Thumbnail
     - ✅ Standard properties
     - TIFF-based pyramid
   * - **QPTIFF**
     - ``.qptiff``
     - ✅ Multi-level  
     - ✅ Overview images
     - ✅ Channel metadata
     - Multi-channel support
   * - **Generic TIFF**
     - ``.tiff``, ``.tif``
     - ⚠️ Single level
     - ❌ None
     - ⚠️ Basic TIFF tags
     - Fallback support

Performance Benchmarks
-----------------------

.. raw:: html

   <div class="performance-showcase">
       <h3>📊 Performance Highlights</h3>
   </div>

Typical performance characteristics:

**Memory Usage**
   - TBD/Optimized

**Read Performance** 
   - TBD/Optimized



Use Cases
---------

With Deep Learning Frameworks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   import fastslide
   import torch
   from torch.utils.data import Dataset, DataLoader
   
   class SlideDataset(Dataset):
       def __init__(self, slide_paths, tile_size=224, level=1):
           self.slides = []
           self.tile_positions = []
           
           for path in slide_paths:
               slide = fastslide.FastSlide.from_file_path(path)
               self.slides.append(slide)
               
               # Generate tile positions
               dims = slide.level_dimensions[level]
               positions = []
               for y in range(0, dims[1], tile_size):
                   for x in range(0, dims[0], tile_size):
                       positions.append((slide, x, y, level, tile_size))
               self.tile_positions.extend(positions)
       
       def __len__(self):
           return len(self.tile_positions)
       
       def __getitem__(self, idx):
           slide, x, y, level, size = self.tile_positions[idx]
           
           # Read tile
           tile = slide.read_region((x, y), level, (size, size))
           
           # Convert to PyTorch tensor
           tensor = torch.from_numpy(tile).permute(2, 0, 1).float() / 255.0
           
           return tensor
   
   # Use with PyTorch DataLoader
   dataset = SlideDataset(["slide1.mrxs", "slide2.svs"])
   loader = DataLoader(dataset, batch_size=32, num_workers=4)

Design Principles
-----------------

1. **Performance First**
   - Minimize memory allocations
   - Cache-friendly data structures
   - SIMD vectorization where applicable

2. **Memory Safety**
   - Modern C++ memory management
   - No undefined behavior
   - Comprehensive bounds checking

3. **Thread Safety**
   - Lock-free read operations
   - Thread-safe caching
   - Concurrent access support

4. **Extensibility**
   - Plugin architecture for new formats
   - Configurable processing pipelines
   - Clean interfaces for customization

5. **Cross-Platform**
   - Consistent behavior across platforms
   - Native performance on each platform
   - Comprehensive testing matrix (TODO)

Next Steps
----------

- :doc:`api/index` - Detailed API documentation
- :doc:`architecture` - Deep dive into system architecture  
- :doc:`guides/index` - Step-by-step implementation guides
- :doc:`examples/index` - Complete working examples
