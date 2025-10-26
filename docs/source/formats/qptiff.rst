QPTIFF Format
=============

QPTIFF (PerkinElmer Quantitative Pathology TIFF) is a multi-channel imaging format designed for 
spectral microscopy applications. Unlike standard RGB imaging, QPTIFF stores each spectral channel 
as a separate grayscale image, enabling quantitative analysis of immunofluorescence and hyperspectral data.

Format Specification
--------------------

**File Extensions:** ``.tif``, ``.tiff``, ``.qptiff``

**Detection:** FastSlide detects QPTIFF files based on the file extension matching ``.tif``, ``.tiff``, 
or ``.qptiff``.

File Structure
--------------

QPTIFF organizes channels as separate TIFF pages in a page-per-channel layout:

.. code-block:: text

   multispectral.tif
   ├── Page 0: Channel 0, Full resolution
   ├── Page 1: Channel 1, Full resolution
   ├── Page 2: Channel 2, Full resolution
   ├── ...
   ├── Page N: Channel 0, Downsampled 2×
   ├── Page N+1: Channel 1, Downsampled 2×
   └── ...

Each channel is stored separately as a grayscale (single-sample) image. For a multi-level pyramid, 
all channels at level 0 come first, followed by all channels at level 1, and so on.

**Key Characteristics:**

- One TIFF page per channel per pyramid level
- Grayscale images (1 sample per pixel)
- Supports 8-bit, 12-bit, 16-bit, and 32-bit float data
- Compressed with LZW, JPEG, or stored uncompressed
- 3-100+ spectral channels typical

Channel Types
-------------

**Multispectral Imaging** (3-10 channels):

Specific fluorescent markers like DAPI (nuclei), FITC, Cy3, Cy5. Each channel captures emission 
from a particular fluorophore at a defined wavelength.

**Hyperspectral Imaging** (50-100+ channels):

Dense wavelength sampling across the visible or near-infrared spectrum. Used for material identification 
through spectral signatures and advanced unmixing algorithms.

Metadata
--------

Channel information is embedded in XML within each page's ImageDescription tag:

.. code-block:: xml

   <?xml version="1.0"?>
   <PerkinElmer-QPI-ImageDescription>
     <Name>Channel_DAPI</Name>
     <ImageType>FullResolution</ImageType>
     <ChannelID>0</ChannelID>
     <Biomarker>
       <Name>DAPI</Name>
       <Type>Fluorophore</Type>
     </Biomarker>
     <Acquisition>
       <Wavelength>461</Wavelength>
       <ExposureTime>100</ExposureTime>
     </Acquisition>
     <Magnification>20</Magnification>
     <PhysicalSizeX>0.3250</PhysicalSizeX>
     <PhysicalSizeY>0.3250</PhysicalSizeY>
   </PerkinElmer-QPI-ImageDescription>

**Key Metadata:**

- **Name**: Channel identifier
- **ChannelID**: Numeric index starting from 0
- **Wavelength**: Emission wavelength in nanometers
- **ExposureTime**: Camera exposure in milliseconds
- **PhysicalSizeX/Y**: Microns per pixel (resolution)

Pyramid Structure
-----------------

The pyramid follows a by-channel grouping:

.. code-block:: text

   Level 0 (Full resolution, e.g., 40000×30000):
     Pages 0-2: Channels 0, 1, 2
   
   Level 1 (2× downsampled, 20000×15000):
     Pages 3-5: Channels 0, 1, 2
   
   Level 2 (4× downsampled, 10000×7500):
     Pages 6-8: Channels 0, 1, 2

All channels at a given level have identical dimensions. Downsampling is typically 2× per level, 
with 4-6 levels total.

Compatibility
-------------

FastSlide's QPTIFF reader works with files from PerkinElmer Vectra systems and is compatible with 
Bio-Formats, QuPath, and ImageJ/Fiji.

References
----------

- `TIFF Specification <https://www.awaresystems.be/imaging/tiff.html>`_
- `Bio-Formats QPTIFF Support <https://docs.openmicroscopy.org/bio-formats/>`_

.. note::
   The QPTIFF specification is not publicly documented. This documentation is based on 
   reverse engineering and practical implementation experience.
