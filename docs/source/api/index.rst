API Reference
=============

This section provides comprehensive documentation for both the C++ and Python APIs.

.. toctree::
   :maxdepth: 3
   :caption: API Documentation:

   python_api
   cpp_api
   multiple_images
   cross_reference

Overview
--------

FastSlide provides two complementary APIs:

- **Python API**: High-level interface with NumPy integration, perfect for research and prototyping
- **C++ API**: High-performance native interface for production applications

The APIs are designed to be consistent - most operations are available in both languages with similar signatures.

Key Concepts
~~~~~~~~~~~~

**Slide Reader (the container)**
   Central class for opening a slide file. Owns the file handles, the
   tile cache, and any associated images. Supports MRXS, Aperio SVS,
   QPTIFF, Olympus VSI, NDPI, BIF, DICOM, OME-TIFF / OME-Zarr, and
   generic TIFF.

**Slide Image (one navigable pyramid)**
   A single navigable pyramid inside a slide file. Most formats expose
   exactly one. Formats like Olympus VSI expose several (e.g. a
   ``navigator`` plus one ``region`` per imaged area). See
   :doc:`multiple_images`.

**Tile Cache**
   Memory-efficient caching system to speed up repeated tile access.

**Associated Data**
   Additional data embedded in slides (thumbnails, labels, metadata).
   Distinct from ``slide.images``: associated images are static
   thumbnails, not navigable pyramids.

**Multi-level Pyramids**
   Each Slide Image contains multiple resolution levels for efficient
   zooming.

Performance Notes
~~~~~~~~~~~~~~~~~

- Use tile caching for applications that read overlapping regions
- The C++ API provides zero-copy operations where possible  
- Python API converts to NumPy arrays for easy integration with scientific libraries
- Both APIs are fully thread-safe
