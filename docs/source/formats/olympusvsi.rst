Olympus VSI Format
==================

VSI (Olympus Virtual Slide Image) is the whole-slide format written by Olympus /
EVIDENT **VS** scanners through the CellSens / Olympus Imaging software. Unlike the
single-file TIFF derivatives, a VSI slide is **not** one file: it is split between a
small ``.vsi`` TIFF container and a sibling data directory that holds the actual
high-resolution pixel pyramids in one or more Olympus ``.ets`` files (the "SIS/ETS"
container).

A single VSI slide is a **multi-image** container. A typical brightfield scan holds a
low-resolution navigator preview and one high-resolution scan of the imaged region;
fluorescence and multi-position / time-lapse acquisitions add further scans, channel
planes and focal planes. Each scan is an independent image with its own resolution
pyramid, so a VSI slide should be treated as a collection of images rather than a
single picture.

.. note::

   The on-disk layout described here is reproducible from the raw bytes with the
   standalone ``inspect_vsi_format.py`` tool (published as a
   `GitHub gist <https://gist.github.com/jonasteuwen/f6fcc75a61cfece68a2015aad8f70bd6>`_)::

       python inspect_vsi_format.py vsi OS-1.vsi
       python inspect_vsi_format.py ets _OS-1_/stack10001/frame_t.ets
       python inspect_vsi_format.py tags OS-1.vsi

Overview
--------

**File extensions.** ``.vsi`` for the TIFF container and ``.ets`` for an individual
Olympus stack (e.g. ``frame_t.ets``).

A slide is recognised from:

- a ``.vsi`` file — its pixel data lives in the sibling ``_<stem>_`` folder
  (e.g. ``OS-1.vsi`` → ``_OS-1_/``),
- an individual ``.ets`` file (e.g. ``frame_t.ets``, ``frame_t_0.ets``), or
- a data directory containing ``stack*/*.ets``.

An ``.ets`` payload is identified by its ``SIS\0`` container magic at offset ``0``
and an embedded ``ETS\0`` sub-header magic at offset ``0x40``. The ``.vsi`` file is a
standard little-endian TIFF.

File Structure
--------------

A VSI slide is a directory tree:

.. code-block:: text

   OS-1.vsi                         # TIFF container: thumbnail, macro, label, resolution
   _OS-1_/                          # data directory ("_" + stem + "_")
   |-- stack1/frame_t.ets           # navigator / low-resolution preview pyramid
   `-- stack10001/frame_t.ets       # main high-resolution pyramid (one per region)

Multi-position / time-lapse layouts add ``frame_t_<index>.ets`` siblings, where
``<index>`` is a time-point / position index:

.. code-block:: text

   Slide_00.vsi
   _Slide_00_/
   |-- stack1/frame_t.ets               # navigator
   |-- stack10000/frame_t.ets           # primary region, t = 0
   |-- stack10002/frame_t_0.ets         # region 1, t = 0
   |-- stack10005/frame_t_0.ets         # region 2, t = 0
   `-- ...

Olympus does not commit to a single filename inside each stack: brightfield mosaics
typically write a single ``frame_t.ets``, while time-lapse or multi-position
acquisitions emit one or more ``frame_t_<index>.ets`` files.

**Stack numbering.** VS scanners number the ``stack*`` folders so that folders with a
numeric suffix ``>= 10000`` are the high-resolution scans of an imaged region, and
lower numbers are navigator / preview pyramids. There is usually exactly one navigator
and one or more region scans; the largest region scan is normally the slide's main
image.

The ``.vsi`` TIFF
-----------------

The ``.vsi`` file is a standard little-endian TIFF whose pages carry the overview /
thumbnail, macro and label images. Olympus inserts a proprietary block between the 
8-byte TIFF header and the first IFD (it begins ``18 00 'IS' ...``). 
**All pixel pyramid data lives in the ``.ets`` files, never in the``.vsi``** — 
the TIFF container only holds the small associated images, calibration
and the metadata tag tree (see *Channel Names and Colours*).

ETS Binary Layout
-----------------

A ``frame_t.ets`` file is a little-endian SIS container with three regions: a 64-byte
SIS header, an embedded ETS image header, and a tile table that locates every
compressed tile by absolute file offset.

.. code-block:: text

   frame_t.ets
   |-- 0x00  SIS\0  container header        (64 bytes)
   |-- 0x40  ETS\0  image sub-header        (4 + 228 bytes)
   |-- ...          compressed tile payloads (JPEG / JPEG2000)
   `-- tiles_offset tile table              (n_tiles records)

The tile table sits **after** the tile payloads near the end of the file; the SIS
header stores its absolute offset.

SIS container header (64 bytes)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 10 12 14 64

   * - Offset
     - Type
     - Field
     - Notes
   * - ``0x00``
     - ``char[4]``
     - magic
     - ``"SIS\0"``.
   * - ``0x04``
     - ``u32``
     - ``header_size``
     - Always ``64`` (the size of this header).
   * - ``0x08``
     - ``u32``
     - ``version``
     - ``2`` in the observed samples.
   * - ``0x0c``
     - ``u32``
     - ``ndim``
     - Tile-record dimensionality (``4`` for brightfield, ``5`` for
       fluorescence); also stored as a constant marker in each tile record.
   * - ``0x10``
     - ``u64``
     - ``ets_offset``
     - Byte offset of the ``ETS\0`` sub-header (``64``).
   * - ``0x18``
     - ``u32``
     - ``ets_nbytes``
     - Size of the ETS sub-header body (``228``).
   * - ``0x20``
     - ``u64``
     - ``tiles_offset``
     - Byte offset of the tile table.
   * - ``0x28``
     - ``u32``
     - ``n_tiles``
     - Tile-record count.

The bytes at ``0x1c``, ``0x24``, ``0x2c`` and ``0x30``–``0x3f`` are reserved/padding
(``0x30`` holds a second, smaller table that is not needed for pixel access).

ETS image sub-header
~~~~~~~~~~~~~~~~~~~~~

The sub-header is a ``4``-byte ``ETS\0`` magic followed by nine ``u32`` fields (a
40-byte prefix), a 17-word zero pad, then a per-component background block:

.. list-table::
   :header-rows: 1
   :widths: 10 12 18 60

   * - Offset
     - Type
     - Field
     - Notes
   * - ``+0x00``
     - ``char[4]``
     - magic
     - ``"ETS\0"`` at ``ets_offset`` (``0x40``).
   * - ``+0x04``
     - ``u32``
     - ``version``
     - ``0x30003`` (196611) in the observed samples.
   * - ``+0x08``
     - ``u32``
     - ``pixel_type``
     - Per-sample storage type (Olympus enumeration). ``2`` = ``UCHAR``
       (8-bit, brightfield) and ``4`` = ``USHORT`` (16-bit, fluorescence)
       are the values seen in practice.
   * - ``+0x0c``
     - ``u32``
     - ``n_channels``
     - Number of samples **within one tile**: ``1`` for grayscale tiles,
       ``3`` for RGB brightfield, ``4`` for RGBA-style data. Note this is
       not the number of channel planes — 16-bit fluorescence declares
       ``1`` here but stacks several grayscale planes via the tile-record
       intermediate dimension (see *Tile table*).
   * - ``+0x10``
     - ``u32``
     - ``color_space``
     - ``4`` in the observed samples.
   * - ``+0x14``
     - ``u32``
     - ``compression``
     - Tile codec (Olympus enumeration); ``2`` = baseline JPEG and ``3`` =
       JPEG2000 are the values seen in practice (see *Codecs*).
   * - ``+0x18``
     - ``u32``
     - ``quality``
     - Encoder quality; ``90`` in the samples.
   * - ``+0x1c``
     - ``u32``
     - ``tile_w``
     - Tile width in pixels; ``512``.
   * - ``+0x20``
     - ``u32``
     - ``tile_h``
     - Tile height in pixels; ``512``.
   * - ``+0x24``
     - ``u32``
     - ``tile_d``
     - Tile depth; ``1`` (a single Z plane).
   * - ``+0x28``
     - ``u32[17]``
     - reserved
     - 68 bytes of zero pad.
   * - ``+0x6c``
     - ``u8[n]`` / ``u16[n]``
     - ``background``
     - Per-component background fill, ``n = n_channels``. Component width
       follows ``pixel_type``: one byte per component for ``UCHAR`` and two
       little-endian bytes per component for ``USHORT``. Brightfield writes
       ``0xff 0xff 0xff`` (white); fluorescence writes a 16-bit zero
       background.

The background block lives directly after the prefix and pad, at
``ets_offset + 40 + 68``. It is the fill colour for tile-grid cells that have no
stored tile.

Tile table
~~~~~~~~~~

The tile-record layout is parametric in ``ndim`` (the SIS-declared dimensionality).
Brightfield stacks use ``ndim = 4`` (records pack ``x, y, channel, level``);
fluorescence sub-stacks use ``ndim = 5`` (records pack ``x, y, channel, Z, level``).
A record is laid out as:

* ``+0`` — ``const_marker`` (``u32``, equal to ``ndim`` in every record).
* ``+4`` … ``+4·ndim`` — one ``u32`` per dimension. Slot 0 is the tile-grid
  **column** ``x``, slot 1 is the tile-grid **row** ``y``, the last slot is the
  pyramid ``level``, and the intermediate slots (slot 2 onward) are the channel /
  focal-plane axis.
* ``+4·ndim`` … ``+4·ndim + 16`` — ``(u64 offset, u32 n_bytes, u32 pad)``: the
  absolute byte offset and compressed length of the tile payload.

So the per-record size is ``4·ndim + 20`` bytes (36 B for ``ndim = 4``, 40 B for
``ndim = 5``). The ``(offset, n_bytes)`` pair points at the JPEG / JPEG2000 payload
elsewhere in the file. All coordinates are **tile-grid indices**, not pixels.

The intermediate dimensions carry the channel / focal-plane axis:

* **8-bit brightfield** (``ndim = 4``): a single intermediate slot (``channel``),
  ``0`` in practice — one image plane.
* **16-bit fluorescence** (``ndim = 5``): several grayscale planes share one
  ``(x, y, level)`` grid, with the plane index in the intermediate dimension. A stack
  with three planes therefore represents a 3-channel image, each plane being one
  channel.

Coordinate System and Tile Grid
--------------------------------

Olympus tiling is simple: there is **no inter-tile overlap and no stitching**. Each
tile record gives a direct ``(level, x, y)`` tile-grid cell, origin at the top-left,
``x`` increasing right and ``y`` increasing down. A tile covers pixels
``[x·tile_w, (x+1)·tile_w) × [y·tile_h, (y+1)·tile_h)``, so a region of interest maps
to a contiguous tile range with a plain crop-and-paint.

The tile grid is **sparse**: border or untiled cells may be absent, in which case the
``background`` colour applies. A pyramid level's extent is ``grid_cols = max(x)+1`` by
``grid_rows = max(y)+1`` tiles, i.e. ``grid_cols·tile_w × grid_rows·tile_h`` pixels.

Pyramid / Levels
----------------

Within a single ``.ets`` stack the records are grouped by ``level`` to reconstruct a
resolution pyramid. The pyramid is **dyadic**: each successive level halves both grid
dimensions, level ``0`` being full resolution. Because per-level grids round to whole
tiles, the true on-disk extent of a level can differ slightly from an exact halving;
consumers typically report clean power-of-two level dimensions (``ceil(width/2)`` per
step, with ``2**level`` downsamples).

Codecs
------

Tiles are individually compressed. Two ``compression`` values appear in practice,
following the Olympus codec enumeration:

.. list-table::
   :header-rows: 1
   :widths: 12 22 66

   * - Value
     - Codec
     - Notes
   * - ``2``
     - JPEG
     - Baseline JPEG (SOI marker ``FF D8 FF``).
   * - ``3``
     - JPEG2000
     - JPEG2000 codestream (``FF 4F FF 51``) or JP2 box; the codec used by
       the bundled samples.

Each tile payload also self-identifies by its leading magic bytes, so the codec can be
confirmed per tile and cross-checked against the declared ``compression``. 16-bit
(``USHORT``) fluorescence tiles are JPEG2000 and may be single-component (grayscale) or
three-component.

Metadata and Associated Images
------------------------------

When the ``.vsi`` TIFF container is present it supplies:

- **Associated images** from the extra TIFF pages — typically a macro image and a
  label image alongside the overview thumbnail.

An individual ``.ets`` file carries no container, so resolution and associated images
are absent when one is read on its own.

Channel Names and Colours
-------------------------

The ``.ets`` pixel files carry **no** channel naming — only raw planes. Human-readable
channel names (e.g. ``"FL DAPI"``, ``"FL FITC"``, ``"FL CY3"``) and their display
colours live in the ``.vsi`` container's Olympus **tag tree**: a nested
``SIS volume`` structure that begins at byte offset 8 (right after the TIFF header).

Relevant facts of the on-disk layout:

- The tree is a recursive list of *data fields*. Each field is a 16-byte header — a
  type word, a tag id, a next-sibling offset, and a payload size — optionally followed
  by inline value bytes; the next-sibling offset is measured in bytes relative to the
  enclosing volume. Bit flags in the type word mark extended/volume fields (which
  recurse), inline data and extra tags.
- The flat field stream is segmented into per-image groups by structural markers: a
  frame-group marker (tag ``2002``) immediately followed by an external-pixels marker
  (tag ``2018``) begins a new image group, and the document- / slide-scope closer
  blocks (tags ``2109`` / ``2062``) close the current group. This is how per-channel and
  per-image fields are attributed to the right image.
- The channel-label field (tag ``2419``) is a UTF-16LE string. A 256-entry display LUT
  (an ``RGB``/``BGR`` array, real type ``269``/``270``) precedes each channel name; its
  brightest entry is the channel's display colour — ``DAPI → blue``, ``FITC → green``,
  ``Cy3 → orange``. Note the ``BGR`` byte order: the on-disk endpoint ``(B, G, R)`` is
  the reverse of conventional RGB.
- The layer-label field (tag ``2030``) is the series / layer name (``"Label"``,
  ``"Overview"``, ``"10x_01"`` …).
- The boundary-rect field (tag ``2053``) is the image's true ``(x, y, width, height)``
  rectangle. Rounded up to whole tiles it equals the matching ``.ets`` stack's level-0
  grid, which is how a container image is paired with its pixel stack.
- The micron-scale field (tag ``2019``) is a pair of doubles giving the per-image
  microns-per-pixel; each image frame (navigator, overview, region) carries its own.

These tag identifiers and the segmentation markers are facts of the format that can be
recovered directly from the bytes (the ``tags`` subcommand of ``inspect_vsi_format.py``
derives them by correlating field payloads against the measured ``.ets`` pyramid sizes
and channel-name content).

Sample-file measurements
------------------------

The numbers below are the contents of the OpenSlide provided samples as reported by
``inspect_vsi_format.py``.

``_OS-1_/stack10001/frame_t.ets`` (≈ 512 MB): SIS ``version=2``, ``ndim=4``,
``ets_nbytes=228``, ``tiles_offset=511012660``, ``n_tiles=26503``. ETS
``version=0x30003``, ``pixel_type=2``, ``n_channels=3``, ``color_space=4``,
``compression=3`` (JPEG2000), ``quality=90``, ``tile=512×512``, ``tile_d=1``,
``background=white``. Nine dyadic levels:

.. code-block:: text

   level 0: 131 x 151 tiles  ->  67072 x 77312 px
   level 1:  66 x  76        ->  33792 x 38912
   level 2:  33 x  38        ->  16896 x 19456
   level 3:  17 x  19        ->   8704 x  9728
   level 4:   9 x  10        ->   4608 x  5120
   level 5:   5 x   5        ->   2560 x  2560
   level 6:   3 x   3        ->   1536 x  1536
   level 7:   2 x   2        ->   1024 x  1024
   level 8:   1 x   1        ->    512 x   512

``_OS-2_/stack10001/frame_t.ets`` (≈ 304 MB): identical header semantics, with
``n_tiles=19312`` and a ``95 × 152`` level-0 grid (``48640 × 77824`` px) halving down
to a single ``512 × 512`` tile at level 8. Both files show the layout is stable across
slides: the same 64-byte SIS header, the same 228-byte ETS body, the same 36-byte
records with the ``(u64 offset @+20, u32 length @+28)`` pair, and the same constant
``const_marker == ndim == 4``.

The companion ``stack1`` previews are smaller dyadic pyramids of the same shape
(``OS-1`` ``stack1`` is ``14 × 26`` tiles at level 0, six levels), confirming the
navigator-vs-main-scan distinction implied by the stack numbering.

References
----------

- `inspect_vsi_format.py (gist) <https://gist.github.com/jonasteuwen/28c0271b23108d7771e446cb114680d8>`_ — a from-raw-bytes layout derivation tool that reproduces every measurement above.
- `JPEG 2000 (ISO/IEC 15444-1) codestream <https://www.iso.org/standard/78321.html>`_
- `TIFF 6.0 specification <https://www.awaresystems.be/imaging/tiff/specification/TIFF6.pdf>`_
