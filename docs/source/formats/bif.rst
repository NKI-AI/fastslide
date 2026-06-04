Ventana BIF Format
==================

BIF (BioImage File) is the proprietary whole slide image format used by Roche Tissue
Diagnostics scanners in the VENTANA family. The Roche Digital Pathology BIF whitepaper
publicly documents the variant produced by the **VENTANA DP 200** brightfield scanner;
files produced by older scanners (VENTANA iScan Coreo, VENTANA iScan HT) share the
``.bif`` extension but are explicitly out of scope for the public specification and
should not be assumed to decode the same way.

BIF files are fully compliant BigTIFF files: every payload (overview, tissue
probability mask, high-resolution pyramid) lives in a standard TIFF Image File
Directory (IFD). The Ventana-specific behaviour is concentrated in two places:

- the XMP metadata blob (TIFF tag 700) attached to IFD 0 and the high-resolution
  (level-0) IFD, and
- the way tiles in the high-resolution IFD (and, in practice, the rest of the
  pyramid) overlap and must be stitched.

This document is the **normative specification for the fastslide BIF decoder**. It is
derived from the Roche whitepaper's documented field semantics and validated against
the OpenSlide sample files (see *Sample-file measurements* below). Where the on-disk
data contradicts the whitepaper, a dedicated *Divergences from the whitepaper* section
calls it out.

Format Specification
--------------------

**File Extension:** ``.bif`` (occasionally ``.tif`` for the no-overlap variant
emitted by the same scanner)

**Detection:** The decoder identifies a BIF file by reading the XMP / ``XMLPacket``
blob (TIFF tag 700) of the **first IFD** and checking that an ``iScan`` element is
present. A fast substring test for ``"iScan"`` is performed before XML parsing so
non-Ventana TIFFs fail immediately; the subsequent parse accepts the ``iScan``
element either as the document root or nested anywhere beneath a ``Metadata``
wrapper (the parser searches for the element by name, depth-first).

**Pyramid pages** are recognised by the presence of a ``level=`` token in the IFD's
``ImageDescription`` tag (e.g. ``level=0 mag=40 quality=95``) **and** the page being
tiled. IFDs without ``level=`` are associated images (overview/label and tissue
probability mask).

File Structure
--------------

A DP 200 BIF file always contains at least three image IFDs followed by a dyadic
resolution pyramid:

.. code-block:: text

   slide.bif (BigTIFF)
   |-- IFD 0: Overview / "Label_Image"   (JPEG, striped, sRGB)
   |-- IFD 1: Tissue probability mask    (8-bit gray, LZW, striped)
   |-- IFD 2: High-resolution scan       (tiled JPEG, YCbCr, ICC profile)
   |           level=0  mag=40  quality=95
   |           XMP tag carries EncodeInfo/SlideStitchInfo/AoiOrigin
   |-- IFD 3: Pyramid level 1            (tiled JPEG, no XMP, 2x downsampled)
   |-- IFD 4: Pyramid level 2            (tiled JPEG, no XMP, 4x downsampled)
   |-- ...
   `-- IFD N: Pyramid level N            (downsampling stops at a single tile)

The pyramid keeps halving until the entire image fits in a single tile.

The decoder does **not** rely on the IFD order above. It enumerates every tiled page
carrying a ``level=`` token, sorts them by descending ``ImageWidth``, and treats the
widest as level 0; the rest follow in decreasing resolution. The ``EncodeInfo``
stitch metadata is read from the XMP packet of the level-0 page (in practice IFD 2).

**Compression and pixel layout for the level-0 IFD and above:**

- Tiled storage (TIFF tags ``TileWidth``/``TileLength``/``TileOffsets``/
  ``TileByteCounts``). Tile size is typically ``1024x1024`` or ``1280x1024``.
- JPEG compression in YCbCr colour space, optionally with a shared ``JPEGTables``
  table (tag 347, TIFF TechNote 2) to deduplicate Huffman/quantization tables.
- Three 8-bit samples per pixel (RGB after YCbCr conversion).
- An ICC v4 profile (tag 0x8773) is stored on the level-0 IFD only but is meant to
  apply to every pyramid level.
- Optional volumetric scans use the private ``IMAGE_DEPTH`` tag (32997 = 0x80E5).
  Multiple Z planes are interleaved into a single IFD; the in-focus plane is stored
  first so plain TIFF readers see a single 2D image.

**Unscanned tiles inside an AOI** have ``TileOffsets`` and ``TileByteCounts`` set to
zero. The decoder substitutes the scanner's ``ScanWhitePoint`` value (from the IFD 0
``iScan`` element) for these tiles rather than reading from disk.

Coordinate Systems
------------------

Two coordinate systems are used in BIF and are *not* interchangeable:

- **Physical / Stage coordinate system.** Origin at the **lower-left** corner of
  the slide (label up, coverslip facing the viewer). X increases right, Y increases
  upward, units are pixels (at the IFD's native resolution). Tile numbering in
  ``TileJointInfo`` is in this system: tile 1 is the lower-left tile of the AOI,
  rows go up, and the numbering snakes — left-to-right on the bottom row,
  right-to-left on the next row up, and so on.

- **Image coordinate system.** Origin at the **top-left** corner, X increases
  right, Y increases down (the standard TIFF / raster convention). ``AoiOrigin``
  values and the in-file ``TILE_OFFSETS`` order are in this system: row-major from
  the top-left of the AOI.

Mapping a 1-based snake index ``n`` to an image-system ``(col, row)`` inside an AOI
of size ``cols`` x ``rows`` is::

   n0   = n - 1                        # 0-based
   srow = n0 // cols                   # snake row, 0 = bottom
   scol = n0 %  cols                   # snake col within that row
   col  = (cols - 1 - scol) if (srow % 2 == 1) else scol
   row  = rows - 1 - srow

This is the standard serpentine (boustrophedon) de-indexing fixed by the physical
tile-numbering rule above. The decoder implements it in ``SerpentineColumn`` and
``SerpentineCell`` (``bif_stitcher.cpp``).

Metadata
--------

The IFD 0 XMP packet contains an ``iScan`` element whose attributes are the
primary scanner-level metadata:

.. code-block:: xml

   <iScan Mode="brightfield"
          Magnification="40"
          ScanRes="0.25"
          UnitNumber="2000123"
          ScannerModel="VENTANA DP 200"
          Z-layers="1"
          Z-spacing="0"
          ScanWhitePoint="255"
          Barcode1D="..."
          Barcode2D="..."
          .../>

The decoder reads (``ParseScannerInfo`` in ``bif_xml.cpp``):

- ``ScannerModel`` — must equal ``"VENTANA DP 200"`` for this specification to
  apply; other models are out of scope.
- ``Magnification`` — objective power (typically 20 or 40).
- ``ScanRes`` — microns per pixel; populates the ``mpp-x`` / ``mpp-y`` properties
  (X and Y are equal by design).
- ``ScanWhitePoint`` — 8-bit per-channel value painted into unscanned tiles
  (defaults to ``255`` when absent).
- ``Z-layers`` (odd integer) and ``Z-spacing`` (microns) — present only for
  volumetric scans; combined with the ``IMAGE_DEPTH`` tag on the level-0 IFD.
- ``Barcode1D`` / ``Barcode2D`` — slide barcodes detected during scanning.

The IFD 0 XMP also lists every user-defined AOI as ``AOI0`` ... ``AOI<N-1>`` in
**physical** coordinates (``Left``, ``Top``, ``Right``, ``Bottom`` with ``Top``
larger than ``Bottom``). To draw these on top of the overview image, subtract each
value from the overview image height.

Level-0 Stitching
-----------------

The level-0 (high-resolution) image is **not** a regular abutting tile grid. Each
AOI was captured as a sequence of slightly overlapping snapshots, and the BIF file
stores those raw snapshots plus enough metadata to reconstruct an integrated image.

The relevant XML lives inside the XMP packet of the level-0 IFD:

.. code-block:: xml

   <EncodeInfo Ver="2">
     <SlideInfo Rack="..." Slot="..." BaseName="...">
       <SlideStitchInfo Left="..." Top="..." Right="..." Bottom="...">
         <ImageInfo AOIScanned="1" AOIIndex="0"
                    NumRows="13" NumCols="4"
                    Width="1280" Height="1024"
                    Pos-X="27102" Pos-Y="52691">
           <TileJointInfo FlagJoined="1" Confidence="100"
                          Direction="RIGHT"
                          Tile1="3" Tile2="4"
                          OverlapX="24" OverlapY="0"/>
           <TileJointInfo FlagJoined="1" Confidence="100"
                          Direction="UP"
                          Tile1="4" Tile2="9"
                          OverlapX="0"  OverlapY="32"/>
           ...
           <Frame XY="0,0" Z="0" Focus="..."/>
           <Frame XY="1,0" Z="0" Focus="..."/>
           ...
         </ImageInfo>
         <ImageInfo AOIScanned="1" AOIIndex="1" .../>
       </SlideStitchInfo>
     </SlideInfo>
     <AoiOrigin>
       <AOI0 OriginX="15360" OriginY="0"/>
       <AOI1 OriginX="0"     OriginY="20480"/>
     </AoiOrigin>
   </EncodeInfo>

**Anatomy of the stitching metadata:**

- ``ImageInfo`` describes one AOI: its tile grid (``NumRows`` x ``NumCols``), the
  per-tile pixel size (``Width`` x ``Height``, must match the TIFF tile size), and
  its absolute stage position (``Pos-X``, ``Pos-Y``). AOIs flagged
  ``AOIScanned="0"`` are skipped — they failed to scan and carry no on-disk tiles.
- ``TileJointInfo`` records one neighbour-pair join. ``Direction`` is one of
  ``RIGHT``, ``LEFT``, ``UP``, ``DOWN`` (in practice DP 200 emits ``LEFT``/``RIGHT``
  and ``UP``). ``Tile1`` / ``Tile2`` are 1-based serpentine indices in the
  *physical* coordinate system; ``OverlapX`` / ``OverlapY`` are the overlap in
  pixels (a positive number meaning the two tiles share that many pixels along the
  joined edge). ``Confidence`` is a quality score in ``[0, 100]`` and ``FlagJoined``
  marks whether the join was actually measured.
- ``AoiOrigin/AOI<n>`` gives the top-left corner of each AOI in image coordinates,
  **rounded to a multiple of the tile size**.
- The whitepaper's note that "Tile2 replaces Tile1 in the overlap" applies: when two
  tiles overlap, the later tile's pixels win — no blending.

**Reconstruction algorithm** The placement below is
defined directly from the ``EncodeInfo`` field semantics and was tuned by measuring
the joints actually present in the bundled sample files (see *Sample-file
measurements* below). Each joint is classified purely by axis: ``LEFT`` and
``RIGHT`` are **horizontal** overlaps (the shared ``OverlapX`` across a column
boundary) and ``UP`` and ``DOWN`` are **vertical** overlaps (the shared ``OverlapY``
across a row boundary). Across the measured corpus only ``RIGHT``, ``LEFT`` and
``UP`` actually occur; ``DOWN`` is never emitted. ``DOWN`` is the mirror of ``UP``
(``Tile2`` one row below ``Tile1``) and the whitepaper defines ``OverlapY``
identically for both vertical directions, so its whitepaper-correct geometry is the
same shared ``OverlapY`` across the row boundary. Because no sample file exercises it,
the decoder treats ``DOWN`` as **unverified** and fails loudly (returns
``kUnimplemented``) the moment it encounters such a joint, rather than silently
emitting an unchecked layout (see *Divergences from the Whitepaper*, item 2).

Notation, per AOI: tile size ``tw`` x ``th``; grid of ``C`` columns x ``R`` rows
(``NumCols`` x ``NumRows``). A joint has a ``Direction``, 1-based serpentine indices
``Tile1``/``Tile2``, overlaps ``OverlapX``/``OverlapY`` (pixels of shared edge), a
``Confidence`` in ``[0, 100]`` and a ``FlagJoined`` flag. The overlap list is *not
authoritative*: in the measured corpus a large fraction of joints are placeholders
(``FlagJoined == 0``, ``Confidence == 0``, all-zero overlaps) and the genuinely
measured joints carry a *broad* confidence spread, not a constant 100 (see
*Sample-file measurements*). Two filters fall directly out of those measurements: a
joint is used to estimate geometry only when the scanner actually joined it
(``FlagJoined == 1``) and reported it at or above a high-confidence floor
(``Confidence >= 95``). The floor is the empirical knee of the observed distribution
— high enough to drop the long noisy tail (down to 84 in the samples) while retaining
the dense, reliable cluster — and is deliberately not a hard equality, because
requiring ``Confidence == 100`` (as the whitepaper does) would discard almost every
measured joint in real files.

The serpentine index of a tile maps to its image-space **column** ``c in [0, C)``
and **row** ``r in [0, R)`` via the snake rule (see *Coordinate Systems*; the snake
starts at the lower-left, so the image row is the mirror of the snake row)::

   k    = Tile - 1                        # 0-based serpentine index
   srow = k // C
   scol = k %  C
   col  = (C - 1 - scol) if (srow odd) else scol
   row  = (R - 1) - srow

*Step 1 - bin overlaps onto grid boundaries.* Each trusted joint constrains the
spacing across exactly one grid boundary, and that boundary - not the joint's
``Direction`` label - is what places the tiles. A **horizontal** joint
(``LEFT`` or ``RIGHT``) couples two tiles in adjacent columns and so measures the
shared ``OverlapX`` across the *column boundary* between them; a **vertical**
joint (``UP`` or ``DOWN``) measures the shared ``OverlapY`` across the *row
boundary*. The boundary index is the lower of the two tiles' grid indices::

   horizontal joint:  col_boundary = min(col(Tile1), col(Tile2))   # in [0, C-1)
                      record OverlapX at that column boundary
   vertical   joint:  row_boundary = min(row(Tile1), row(Tile2))   # in [0, R-1)
                      record OverlapY at that row boundary

``LEFT`` and ``RIGHT`` are treated identically here (both are horizontal overlaps
binned by column boundary); the direction label only governs which tile overwrites
the other in the shared band, not the geometry. Only the on-axis overlap feeds the
pitch: a small cross-axis component (e.g. a non-zero ``OverlapY`` on a ``RIGHT``
joint) is a minor stage slip this decoder does not model.

*Step 2 - per-boundary pitch.* Every column boundary gets its own horizontal pitch
and every row boundary its own vertical pitch - the tile size minus the overlap
*measured at that boundary*. A boundary that no trusted joint crossed falls back to
the AOI-wide mean overlap, which keeps all tiles on one consistent grid (no gaps)
without smearing a sharp, localised overlap across boundaries that do not have it::

   pitch_x[b] = tw - (mean OverlapX at column boundary b   if measured
                      else mean OverlapX over all horizontal joints)   # tw if none
   pitch_y[b] = th - (mean OverlapY at row boundary b      if measured
                      else mean OverlapY over all vertical   joints)   # th if none

This is the crucial point for ``LEFT``-only files such as ``Ventana-1.bif``: most
column boundaries abut (``OverlapX = 0``) while a few overlap by ``24`` px. Binning
per boundary applies the ``24`` px only where it was measured; a single averaged
advance would instead spread its mean across every column and misplace the abutting
ones.

*Step 3 - accumulate boundaries into positions* (image coordinates; ``col``/``row``
are 0-based in row-major ``TILE_OFFSETS`` order, ``row = 0`` at the top). A tile's
position is the running sum of the pitches of all boundaries before it::

   x(col) = anchor_x + sum(pitch_x[b] for b < col)
   y(row) = anchor_y + sum(pitch_y[b] for b < row)

and every tile ``(col, row)`` in the AOI is placed at ``(x(col), y(row))``.

The AOI is anchored at its image-space tile-grid origin
``anchor = (round(OriginX/tw) * tw, round(OriginY/th) * th)`` (from
``AoiOrigin/AOI<n>``). ``Pos-X``/``Pos-Y`` are parsed but not used for placement.
After all AOIs are placed the bounding box is normalised to ``(0, 0)`` (Step 4).

*Step 4 - level-0 size.* The level-0 dimensions are the bounding box of all placed
tiles (``ceil(max(x + tw) - min(x))`` by ``ceil(max(y + th) - min(y))``); every
position is then translated so the box starts at ``(0, 0)``. The level-0 IFD
``ImageWidth`` / ``ImageLength`` describe the *raw concatenated-snapshot* grid and
are larger than (or equal to) the stitched size; they are not used for sizing.

*Step 5 - mapping ``(col, row)`` to a TIFF tile.* The level-0 IFD is a standard
tiled image with ``grid_cols = ImageWidth / tw`` tiles per row, stored row-major.
The AOI occupies a ``C`` x ``R`` block whose top-left tile is
``(OriginX / tw, OriginY / th)``, so::

   tiff_tile_index = (OriginY/th + row) * grid_cols + (OriginX/tw + col)

``grid_cols`` may exceed ``C`` (the TIFF grid can carry extra trailing columns that
belong to no AOI); those padding tiles are never referenced.

Tiles whose ``TileOffsets``/``TileByteCounts`` are zero are *unscanned*: paint them
with ``ScanWhitePoint`` (from the IFD 0 ``iScan`` element) instead of reading from
disk.

**Sample-file measurements (empirical basis).** Every rule above was fixed by
reading the ``TileJointInfo`` records out of the bundled samples and tabulating
them. The numbers below are the actual contents of those files and are what the
algorithm was tuned against; they are reproducible by dumping tag 700 of the level-0
IFD and counting attributes.

``Ventana-1.bif`` (VENTANA DP 200): the level-0 IFD is ``24576 x 21504`` on a
``1024 x 1024`` tile, a ``24 x 21`` TIFF tile grid (504 tiles). One AOI, ``23`` cols
x ``21`` rows, ``OriginX = OriginY = 0``. It carries ``922`` joints, **all**
``FlagJoined = 1`` and **all** ``Confidence = 100``. Directions are ``462`` ``LEFT``
+ ``460`` ``UP`` and **zero** ``RIGHT``/``DOWN``. ``LEFT`` joints carry
``OverlapX in {0, 24}`` (105 of them are ``24``) with ``OverlapY = 0``; ``UP``
joints are all-zero. So horizontal overlap is sparse and column-local while vertical
spacing is exactly one tile — which is why per-boundary binning (Step 1) is
mandatory: averaging the ``LEFT`` overlaps into one advance would misplace the
abutting columns, and a ``RIGHT``-only decoder cannot place this file at all.

``OS-1.bif``: the level-0 IFD is ``118784 x 102000`` on a ``1024 x 1360`` tile, a
``116 x 75`` grid (8700 tiles). One AOI, ``116`` cols x ``75`` rows. It carries
``17209`` joints, of which ``10174`` are placeholders (``FlagJoined = 0`` with
``Confidence = 0`` and all-zero overlaps) and ``7035`` are real joins. Among the
real joins ``Confidence`` is **not** constant: it spans ``84..100`` (only ``124``
are exactly ``100``; the mode is ``99``/``98`` with ``1768``/``1715`` hits and a
dense cluster through ``96``). Directions are ``2870`` ``RIGHT`` + ``4165`` ``UP``
and **zero** ``LEFT``/``DOWN``. ``RIGHT`` joints carry ``OverlapX ~ 95..131`` with a
small ``OverlapY ~ 1..3`` (a minor cross-axis slip this decoder does not model —
only the on-axis ``OverlapX`` feeds the column pitch); ``UP`` joints carry
``OverlapY ~ 66..155`` with a small ``OverlapX ~ -3..0`` (likewise ignored — only the
on-axis ``OverlapY`` feeds the row pitch).

Two consequences drive the design directly: (1) the union of directions over the
corpus is ``{RIGHT, LEFT, UP}`` with ``DOWN`` unseen, and all four are classified by
axis (``LEFT``/``RIGHT`` horizontal, ``UP``/``DOWN`` vertical); (2) ``FlagJoined``
and the ``>= 95`` confidence floor are both required — without the ``FlagJoined``
filter OS-1's 10174 zero-overlap placeholders would collapse the per-boundary means,
and without a sub-100 floor the ``Confidence == 100`` rule would keep only ``124`` of
OS-1's ``7035`` joins.

Multiple AOIs are merged into one rectangular image. Holes between AOIs are
represented as zero-length tiles; they are painted with ``ScanWhitePoint``.

Pyramid / Higher Levels
-----------------------

**The mental model.** Two things are stitched in this format, and only one of them
is shift-corrected:

- *Level 0 is off the nominal lattice.* Each level-0 tile is its own physical TIFF
  tile. They are **not** on a clean ``tw`` x ``th`` lattice: the placement
  accumulates the per-boundary pitches (Steps 1-3), each shorter than a full tile by
  its measured overlap, so neighbouring tiles are pulled together by exactly the
  amount their content overlaps. This overlap-compacted mosaic is what level 0
  reconstructs.

- *Higher levels are raw, uncorrected packings.* The level-``L`` IFD is a plain
  dyadic downsample of the level-0 **raw, concatenated** tile grid - not of the
  stitched level-0 image. Concretely, one physical level-``L`` tile contains a
  ``d x d`` block of level-0 tiles, each independently downsampled by ``d`` and
  packed edge-to-edge on a regular ``tw/d x th/d`` lattice. At level 1 that is
  exactly **four** (``2 x 2``) downsampled level-0 tiles packed into a single tile;
  level 2 packs ``4 x 4``; and so on. The scanner applies **no** stitch correction
  when packing: the per-boundary overlap compaction is absent, and the per-tile
  overlap is still present inside the pixel data (each sub-cell is a full
  downsampled tile, overlap region included). So adjacent sub-cells in a physical
  tile image scene positions that *overlap* by roughly one tile-overlap.

Because the on-disk higher levels are uncorrected, this decoder does **not** trust
their internal layout. It reconstructs every level from the *level-0* compacted
geometry, scaled by ``1/d`` (next section): the per-boundary pitches are re-applied
(scaled) at placement time, while the pixels come from the regular-grid sub-cells of
the packed physical tile. The whitepaper's claim that higher-resolution levels abut
(*"there is no overlap between lower-resolution image tiles"*) does not match the
on-disk layout; see divergence 6.

Each pyramid level is its own IFD holding a complete, standard dyadic downsample of
the level-0 *raw* tile grid: every level halves both dimensions and keeps the same
tile size. In the bundled ``Ventana-1.bif`` the level-0 IFD is ``24576 x 21504``
(``1024``-px tiles) and the levels are ``12288 x 10752``, ``6144 x 5376``, ... down
to ``192 x 168``; the magnification in each level's ``ImageDescription``
(``level=N mag=M quality=Q``) halves accordingly (``40, 20, 10, 5, ...``).

The downsample factor is ``d = round(level0_width / level_width)`` (``1, 2, 4, 8,
...``), derived from the raw IFD widths. Because each level downsamples the *raw
concatenated* grid - not the stitched image - the overlap is preserved in the pixel
data and must be re-applied at every level.

**Re-stitching a higher level (normative for this decoder).** Level ``L`` reuses the
level-0 geometry scaled by ``1/d`` (``BifSpatialIndex::Build`` in
``bif_spatial_index.cpp``):

- *Geometry scales.* Every per-boundary pitch becomes ``pitch/d`` and every placed
  level-0 tile origin ``P0`` maps to ``P0 / d``; the drawn size of one tile becomes
  ``tw/d`` x ``th/d``.
- *Pixel source.* A level-0 tile at TIFF grid position ``(gx, gy)`` (Step 5 above)
  was, at level ``L``, packed ``d`` tiles per axis into a single physical
  level-``L`` tile. Its pixels are therefore the sub-rectangle::

     phys_tile = (gx // d, gy // d)           # tile index inside the level-L IFD
     i, j      = gx % d, gy % d               # sub-cell within the physical tile
     sub_x, sub_w = i*tw/d, ceil(tw/d)        # fractional origin, ceil size
     sub_y, sub_h = j*th/d, ceil(th/d)

  of physical tile ``phys_tile`` in the level-``L`` IFD, where ``phys_tile`` is
  linearised against that level's own tile-grid stride. This mapping is forced by
  the dyadic geometry alone (level-``L`` pixel ``(X, Y)`` is the downsample of
  level-0 pixel ``(dX, dY)``), so it needs no extra metadata. The sub-cell *offset*
  is the **fractional** running fraction ``k*t/d`` and is deliberately *not*
  rounded: rounding it would shift the painted content by up to half a pixel;
  because ``th/d`` is non-integral for ``d >= 32`` (``1360/32 = 42.5``) the rounding
  error differs per level, which makes tile seams *drift* between levels. The
  fractional origin keeps a given source feature at the same scene position across
  all levels. The sub-cell *size* is the uniform ``ceil(t/d)`` rather than the exact
  partition width: sub-cells are read at the source step ``t/d`` but reconstructed
  at the destination step ``adv/d`` (smaller, because tiles overlap). At fine levels
  the overlap is several pixels; at the coarsest levels it falls below one pixel, so
  a tight size would no longer reach the next cell, opening 1px gaps that show up as
  white seam rows in the thumbnail. ``ceil(t/d)`` is at least the largest integer
  destination step, so it always covers the gap; the extra sub-pixel overlap is
  duplicated source content and resolves under last-writer-wins.

Equivalently, in the spatial-index implementation: build the index once at level 0
(placed bounding box plus each tile's ``(gx, gy)`` and TIFF tile index); to serve a
region at level ``L``, scale the requested region into level-0 coordinates (``* d``),
query the index, and for each hit read the ``(sub_x, sub_y, sub_w, sub_h)`` crop of
``phys_tile`` from the level-``L`` IFD and paint it at the **sub-pixel** destination
``(P0 - region_origin) / d``.

Placement is sub-pixel: each tile is bilinearly resampled onto the integer output
grid at its true fractional offset (no rounding of the destination). Because every
tile is resampled onto the *same* output grid, overlapping tiles agree in the
overlap region and meet seamlessly. Rounding destinations to integers instead (as a
direct copy) injects up to half a pixel of per-tile misregistration, which reappears
as a faint ~1px seam exactly at tile boundaries; sub-pixel placement removes it.

The one requirement is edge handling: a level-``L`` physical tile packs ``d*d``
independently-downsampled level-0 tiles side by side, imaging scene positions
roughly one overlap apart. The resampler must therefore **clamp sampling to each
tile's own sub-rectangle** (EXTEND_PAD - "take the border pixel itself", never
``border + 1``). Sampling past a sub-cell edge would read the neighbouring packed
tile and paint a bright/coloured line - the classic artefact of naive bilinear on
this layout. With the clamp in place (``Canvas::BilinearRgbBlit``) the result is
seamless.

The level-``L`` size is the level-0 bounding box divided by ``d`` and rounded
(``round(level0_width / d)`` x ``round(level0_height / d)``), not the raw IFD
``ImageWidth``/``ImageLength``. Deriving every level size from the single level-0
bounding box keeps a region's pixel content identical between levels up to
resampling, so downstream code never has to reconcile per-level rounding drift in
the reported dimensions.

**Overlap resolution** is *last-writer-wins in raster order* at every level (no
blending): where tiles overlap, the right/bottom tile is visible. Crucially this
ordering is **global across all sub-tiles**, not just whole physical tiles: at level
``L`` a single physical tile hosts ``d*d`` overlapping level-0 sub-tiles, and their
mutual overlap must also resolve bottom-right-wins, or per-sub-tile seams appear. The
implementation (``BifTileExecutor::ExecutePlan``) decodes each unique physical tile
once (in parallel), then paints every sub-tile op in reverse raster order
(bottom-right first) against a first-writer-wins coverage map, which is equivalent.

Decoder consequences:

- Sizing any level from ``ImageWidth``/``ImageLength`` alone is unreliable; the size
  is derived from the stitched bounding box (level 0) divided by ``d``.
- Tile-aligned reads at level > 0 do **not** correspond to whole TIFF tiles - the
  stitched grid is derived from level 0 (scaled by ``d``, then rounded).

Divergences from the Whitepaper
-------------------------------

The Roche whitepaper is not fully consistent with how real DP 200 files actually
decode. The discrepancies below were established from the bundled sample files (see
*Sample-file measurements*) and matter for anyone implementing their own decoder:

1. **``<Frame>`` ordering is not used.** The whitepaper presents the per-tile
   ``<Frame XY="C,R" Z="..."/>`` list as the authoritative mapping from
   ``TILE_OFFSETS`` slots to ``(col, row)`` positions. This decoder instead assumes
   ``TILE_OFFSETS`` is row-major within each AOI and rebuilds ``(col, row)`` from
   ``AoiOrigin`` + ``NumRows``/``NumCols`` and the serpentine ``TileJointInfo``
   indices. In the measured samples DP 200 stores frames in exactly that row-major
   order, so the two agree; a strict whitepaper-compliant decoder would honour
   ``<Frame>``.

2. **``Direction`` is classified by axis, and ``DOWN`` is gated as unverified.**
   The whitepaper enumerates ``LEFT``, ``RIGHT``, ``UP``, ``DOWN`` as four distinct
   cases. This decoder classifies them by axis: ``LEFT``/``RIGHT`` are horizontal
   overlaps (shared ``OverlapX`` binned onto a column boundary) and ``UP``/``DOWN``
   are vertical (shared ``OverlapY`` binned onto a row boundary). Real DP 200 output
   uses ``LEFT`` + ``UP`` (``Ventana-1.bif``) or ``RIGHT`` + ``UP`` (``OS-1.bif``),
   all of which are exercised and verified. ``DOWN`` is **never seen** in the
   measured corpus. Its whitepaper-correct geometry is identical to ``UP`` (a vertical
   ``OverlapY`` across the shared row boundary, since the whitepaper defines
   ``OverlapY`` the same way for both vertical directions), and the decoder documents
   that placement in ``StitchLevel0`` — but because no sample file lets us check it,
   the ``DOWN`` branch deliberately returns an error
   (``kUnimplemented``: *"the DOWN tile-joint direction is implemented according to
   the whitepaper but has not been verified against a real VENTANA DP 200 file"*)
   rather than emitting an unchecked layout. A future file that genuinely carries
   ``DOWN`` can be validated and the guard lifted.

3. **AOI anchoring uses ``AoiOrigin``.** Both ``Pos-X``/``Pos-Y`` (stage
   coordinates, in ``ImageInfo``) and ``OriginX``/``OriginY`` (image coordinates, in
   ``AoiOrigin``) appear to anchor each AOI, and the whitepaper does not say which is
   canonical. ``AoiOrigin`` is already in the image coordinate system and is rounded
   to a tile multiple, so this decoder anchors on it directly
   (``round(OriginX/tw)*tw``) and ignores ``Pos-X``/``Pos-Y`` for placement. On
   well-formed files the two agree to within a tile size.

4. **``Confidence`` is a floor, not an equality.** The whitepaper says ``Confidence``
   must equal 100 and that decoders should stop otherwise. The data contradicts this:
   in ``OS-1.bif`` only ``124`` of ``7035`` joined joints are exactly ``100`` and the
   rest spread down to ``84``, so an equality test would discard ~98% of the usable
   geometry and the file could not be stitched. This decoder keeps joints at or above
   an empirical high-confidence floor (``>= 95``).

5. **Vertical overlap is real and must be applied.** The whitepaper says DP 200 files
   never have vertical overlap (``OverlapY`` must be 0 and decoders should stop
   otherwise). The data disagrees: ``OS-1.bif`` carries ``4165`` ``UP`` joints with
   ``OverlapY`` ranging roughly ``66..155``, so a non-zero Y advance is required to
   place that file. The overlap list is also not authoritative in another sense - a
   large share of the joints are zero-overlap placeholders (``FlagJoined = 0``) that
   must be filtered out.

6. **Higher pyramid levels are still stitched** (the most important divergence). The
   whitepaper says IFD 3 and up are pre-stitched abutting-tile pyramids. In reality
   the higher IFDs are dyadic downsamples of the *raw concatenated* grid, so the
   level-0 overlap survives and must be re-applied per level. Treating pyramid levels
   as plain row-major TIFFs produces subtly misaligned tiles at the AOI edges and
   incorrect overall level sizes on files with non-zero overlap. This decoder
   re-stitches every level at scaled overlap (see *Pyramid / Higher Levels*,
   *Re-stitching a higher level*).

References
----------

- `Roche Digital Pathology BIF whitepaper (PDF) <https://diagnostics.roche.com/content/dam/diagnostics/Blueprint/en/pdf/rmd/Roche-Digital-Pathology-BIF-Whitepaper.pdf>`_
- `BigTIFF specification <https://www.awaresystems.be/imaging/tiff/bigtiff.html>`_
- `TIFF TechNote 2 (JPEG-in-TIFF) <https://www.awaresystems.be/imaging/tiff/specification/TIFFTechNote2.txt>`_

.. note::
   The Roche whitepaper covers only the VENTANA DP 200 scanner output. Older Ventana
   scanners (iScan Coreo, iScan HT) emit ``.bif`` files with different internal
   layout — feeding their pixel data into the algorithm above is explicitly called
   out as unsupported and "will most likely result in incorrect object counts." The
   *Reconstruction algorithm* and *Re-stitching a higher level* sections above are
   the normative specification for this decoder: they were derived from the
   documented field semantics and validated against the bundled ``Ventana-1.bif`` /
   ``OS-1.bif`` samples.
