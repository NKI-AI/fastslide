API Reference
=============

This section provides comprehensive documentation for both the C++ and Python APIs.

.. toctree::
   :maxdepth: 3
   :caption: API Documentation:

   python_api
   cpp_api
   cross_reference

Overview
--------

FastSlide provides two complementary APIs:

- **Python API**: High-level interface with NumPy integration, perfect for research and prototyping
- **C++ API**: High-performance native interface for production applications

The APIs are designed to be consistent - most operations are available in both languages with similar signatures.

Key Concepts
~~~~~~~~~~~~

**Slide Reader**
   Central class for reading whole slide images. Supports multiple formats (MRXS, Aperio SVS, QPTIFF).

**Tile Cache**
   Memory-efficient caching system to speed up repeated tile access.

**Associated Data**
   Additional data embedded in slides (thumbnails, labels, metadata).

**Multi-level Pyramids**
   Slides contain multiple resolution levels for efficient zooming.

Performance Notes
~~~~~~~~~~~~~~~~~

- Use tile caching for applications that read overlapping regions
- The C++ API provides zero-copy operations where possible  
- Python API converts to NumPy arrays for easy integration with scientific libraries
- Both APIs are fully thread-safe
