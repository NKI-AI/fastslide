Aperio SVS Format
=================

Aperio SVS is a widely-adopted format in digital pathology, originally developed by Aperio Technologies 
(acquired by Leica Biosystems). SVS files are essentially BigTIFF files containing pyramidal whole slide 
images compressed with JPEG.

Format Specification
--------------------

**File Extension:** ``.svs``

**Detection:** FastSlide detects SVS files based solely on the ``.svs`` file extension.

File Structure
--------------

An SVS file is a multi-page TIFF containing pyramid levels and associated images:

.. code-block:: text

   slide.svs (BigTIFF)
   ├── Page 0: Base level (full resolution, tiled, JPEG)
   ├── Page 1: Thumbnail preview
   ├── Page 2: Pyramid level 1 (downsampled)
   ├── Page 3: Pyramid level 2 (downsampled further)
   ├── Page 4: Macro photograph of the slide
   └── Page 5: Label image with barcode

The first page contains the full-resolution scan, typically at 20× or 40× magnification. Subsequent pages 
contain progressively downsampled versions for efficient viewing at lower resolutions. Each level is typically 
downsampled by a factor of 2-4 from the previous level.

Associated images (thumbnail, macro, label) provide additional context about the physical slide.

Technical Details
-----------------

**Compression and Storage:**

- Tiles are typically 240×240 pixels
- JPEG compression using the OLD_JPEG TIFF tag
- YCbCr color space (automatically converted to RGB when reading)
- 8 bits per channel, 3 channels (RGB)

**Data Organization:**

Main pyramid levels use a tiled format for random access, while associated images typically use a 
stripped format for sequential reading.

Metadata
--------

Metadata is stored in the ImageDescription TIFF tag as a pipe-delimited text format:

.. code-block:: text

   Aperio Image Library v11.2.1
   46000x32914 [0,100 46000x32914] (256x256) JPEG/RGB Q=30
   |AppMag = 20
   |MPP = 0.4990
   |Date = 12/29/09
   |Time = 09:59:15
   |ScanScope ID = CPAPERIOCS

References
----------

- `OpenSlide Aperio Documentation <https://openslide.org/formats/aperio/>`_
- `BigTIFF Specification <https://www.awaresystems.be/imaging/tiff/bigtiff.html>`_
