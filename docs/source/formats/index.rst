Supported File Formats
======================

FastSlide supports multiple whole slide imaging formats commonly used in digital pathology. 
Each format has unique characteristics in terms of compression, tile organization, and metadata structure.

.. toctree::
   :maxdepth: 2
   :caption: Format Documentation:

   svs
   mrxs
   qptiff

Format Overview
---------------

.. list-table:: Supported Formats
   :header-rows: 1
   :widths: 15 20 20 25 20

   * - Format
     - File Extension
     - Compression
     - Tile Organization
     - Special Features
   * - Aperio SVS
     - .svs
     - JPEG (in TIFF)
     - Tiled TIFF / Strips
     - BigTIFF, associated images
   * - MIRAX (MRXS)
     - .mrxs
     - JPEG / PNG / BMP
     - Hierarchical DAT files
     - Overlapping tiles, spatial index
   * - QPTIFF
     - .tif/.tiff
     - LZW / JPEG / Uncompressed
     - Multi-page TIFF
     - Multi-channel spectral imaging

Format Detection
----------------

FastSlide automatically detects the file format based on the file extension:

- ``.svs`` → Aperio SVS reader
- ``.mrxs`` → MIRAX (MRXS) reader  
- ``.tif``, ``.tiff``, ``.qptiff`` → QPTIFF reader

The appropriate reader is selected based solely on the filename extension.
