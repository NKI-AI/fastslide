MIRAX (MRXS) Format
===================

MIRAX is a proprietary format developed by 3DHISTECH (now Sysmex) for their PANNORAMIC line of scanners. 
Unlike single-file formats, MRXS slides are stored across multiple files in a directory structure.

Format Specification
--------------------

**File Extension:** ``.mrxs``

**Detection:** FastSlide detects MRXS files based on the ``.mrxs`` file extension.

File Structure
--------------

An MRXS slide consists of a main index file and a companion directory:

.. code-block:: text

   Slide001.mrxs                    # Main index (INI format)
   Slide001/                        # Data directory
   ├── Slidedat.ini                 # Metadata and configuration
   └── Data/                        
       ├── Index.dat                # Tile location index
       └── Data0000.dat, ...        # Compressed tile data

The ``.mrxs`` file itself is a simple INI file that points to the data directory. The real content lives in 
the companion directory, which contains metadata, an index of tile locations, and the actual image data 
packed into data files.

**Storage Characteristics:**

- Tiles compressed as JPEG, PNG, or BMP
- Multiple data files (Data0000.dat, Data0001.dat, etc.)
- Binary index maps tile coordinates to file locations
- Hierarchical pyramid with 2× downsampling per level

Tile Organization
-----------------

MRXS uses a unique tile layout based on camera capture patterns. The scanner's camera captures overlapping 
photographs, which are then split into non-overlapping tiles. **Tiles from adjacent photographs overlap at their 
boundaries**, requiring special handling during reconstruction.

FastSlide handles these overlaps by averaging pixel values in overlapping regions, 
after subpixel alignment using the Magic Kernel 2021

Metadata
--------

The Slidedat.ini file uses INI format with multiple sections:

.. code-block:: ini

   [GENERAL]
   SLIDE_ID = 123456789
   IMAGENUMBER_X = 8
   IMAGENUMBER_Y = 6
   OBJECTIVE_MAGNIFICATION = 20
   MICROMETER_PER_PIXEL_X = 0.2432
   MICROMETER_PER_PIXEL_Y = 0.2432
   
   [HIERARCHICAL]
   HIER_COUNT = 9                  # Number of pyramid levels
   HIER_0_COUNT = 48               # Tiles in level 0

**Key Metadata Sections:**

- **[GENERAL]**: Basic slide properties (dimensions, magnification, resolution)
- **[HIERARCHICAL]**: Pyramid structure (levels, tile counts)
- **[DATAFILE]**: Maps file indices to data filenames
- **[NONHIER_*]**: Associated images (thumbnail, macro, label)

Data Files
----------

**Index.dat** contains a binary index that maps tile coordinates to byte offsets in the data files. 
Each entry specifies which data file contains a tile and where in that file to find it.

**Data files** (Data0000.dat, etc.) store the compressed tiles as contiguous blobs. The index provides 
the exact byte offset and length for each tile.

Associated Images
-----------------

MRXS supports optional associated images referenced in Slidedat.ini:

- **thumbnail**: Preview image (ScanDataLayer_SlidePreview)
- **macro**: Full slide photograph (ScanDataLayer_SlideThumbnail)  
- **label**: Barcode/label image (ScanDataLayer_SlideBarcode)

References
----------

- `OpenSlide MIRAX Documentation <https://openslide.org/formats/mirax/>`_

.. note::
   MIRAX is a proprietary format. This documentation is based on reverse engineering efforts 
   and implementation experience of many people and open source projects, especially the OpenSlide
   project.
