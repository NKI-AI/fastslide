---
title: 'FastSlide: a multi-format whole slide image reader in C++ with Python bindings'
tags:
  - C++
  - Python
  - digital pathology
  - whole slide imaging
  - computational pathology
  - multiplex immunofluorescence
authors:
  - name: George Yiasemis
    orcid: 0000-0002-1348-8987
    affiliation: 1
  - name: Rolf Harkes
    orcid: 0000-0001-6592-654X
    affiliation: 1
  - name: Jonas Teuwen
    orcid: 0000-0002-1825-1428
    corresponding: true
    affiliation: 1
affiliations:
  - name: Netherlands Cancer Institute, Amsterdam, The Netherlands
    index: 1
date: 25 November 2026
bibliography: paper.bib
---

# Summary

A whole slide image (WSI) is a scan of a glass microscope slide, stored as a
multi-resolution pyramid of compressed tiles that at full resolution
can exceed 100,000 pixels per side. Software that works with these images reads
rectangular regions at a chosen resolution through a reader library.

`FastSlide` is such a reader: a C++20 library with Python bindings that
opens twelve file formats through one interface: Aperio SVS, 3DHISTECH MRXS,
Hamamatsu NDPI, Ventana BIF, Olympus VSI, Zeiss CZI, Philips iSyntax,
Akoya QPTIFF, OME-TIFF, OME-Zarr, DICOM, and generic tiled TIFF. A region is
returned in the pixel type of the file, for example 8-bit or 16-bit, and is
exposed to NumPy [@harris2020numpy] without copying. Besides brightfield RGB
slides, `FastSlide` reads multi-channel fluorescence images, focal planes (Z),
time points (T), and files that hold several images (\autoref{fig:regions}). A single
open slide can be read from many threads at once. `FastSlide` is released under
the Apache License 2.0, and wheels for CPython 3.11 and later are published on
PyPI for Linux (glibc), macOS, and Windows on x86-64 and ARM64.

![Regions read with `FastSlide` from two public slides. Left: a brightfield H&E slide in Aperio SVS format (CMU-1 from the OpenSlide test data). Right: a five-channel fluorescence slide in QPTIFF format (LuCa-7color_Scan1 from the OME sample images), showing the DAPI (blue), FITC (green), and CY5 (red) channels.\label{fig:regions}](figures/regions.png)

# Statement of need

Deep learning models in computational pathology are trained on large collections of
WSIs [@campanella2019clinical]. Training and inference read many small
regions, usually from several data-loading threads or processes. Each scanner vendor writes its own container, and a cohort
collected across hospitals or over several years mixes formats; a pipeline then
depends on several readers that differ in coordinate conventions and in the
array layout they return. In addition, multiplex immunofluorescence slides store more than three
channels, often at 16 bits, which readers designed for 8-bit RGB cannot return
unchanged.

`FastSlide` is written for researchers who build data pipelines and viewers
on top of WSIs and want one dependency for the formats in their cohorts. It exposes pyramid levels, pixel spacing, channel names, associated images,
and the embedded ICC profile through the same properties for every format.

# State of the field

`OpenSlide` [@goode2013openslide] is the established open-source WSI
reader. It is a C library under the LGPL 2.1; version 4.0.1 lists thirteen
formats and has run concurrent reads on one handle in parallel since version 3.3.1. Its read
function returns premultiplied 8-bit ARGB for a position, a level, and a
size, so fluorescence channels beyond three, 16-bit data, focal planes, and
time points cannot be returned, and its format list does not include
Philips iSyntax, Olympus VSI, QPTIFF, or OME-Zarr. `Bio-Formats`
[@linkert2010bioformats] reads more than 160 microscopy formats, but it is a Java library whose readers are documented as not
thread-safe, and its maintainers provide no Python implementation. `cuCIM`
[@lee2021cucim] targets GPU pipelines and documents support for Aperio SVS,
Philips TIFF, and generic tiled TIFF. `TiffSlide` [@poehlmann2022tiffslide] is
a pure-Python replacement for the OpenSlide Python API built on `tifffile`,
and is limited to TIFF-based formats. `SlideIO` [@slideio] is the closest in
scope: a BSD-licensed C++ library with Python bindings whose documented
drivers cover most of the formats above, though not iSyntax or OME-Zarr.
\textcolor{red}{[TO ADD by the authors: why FastSlide was built instead of
contributing to SlideIO.]}

We wrote a new library instead of extending `OpenSlide` because the features we
needed, pixel data in its stored type with any number of channels and selection
of a Z/T plane, do not fit an interface whose output is a `w × h × 4` byte ARGB
buffer.

# Software design

**Readers plan before they read.** Each format is a reader class registered
with a central registry, which selects the reader from the file extension or,
failing that, the file content. A reader
implements two steps. `PrepareRequest` turns a region request into a `TilePlan`,
a list of the tiles to fetch and where each lands in the output; it performs no
I/O. `ExecutePlan` reads, decodes, and paints those tiles. This costs more code
per format than a single read function, but the geometry of a format (level
selection, tile overlap, clipping at slide borders) can be unit-tested without
slide files, and the TIFF-based readers share their plan-building and decoding
code.

**Coordinates are level-native.** A region at pyramid level $k$ is addressed in
the pixels of level $k$, whereas `OpenSlide` addresses every level in level-0
pixels. This breaks drop-in compatibility with the OpenSlide API, but location and
size are then in the same unit; helper functions convert between the two.

**Images keep their stored layout.** `read_region` returns an `Image` that
records its data type, channel count, and whether channels are interleaved or
stored as separate planes, and can convert between the two layouts. The NumPy
array is a view of the image buffer.

**Concurrency and caching.** The Python binding releases the global interpreter
lock while a region is read, and one slide handle can be shared between
threads. In our tests, 200 random regions read by 32 threads from one handle
were bit-identical to the same regions read serially for files in all twelve
formats.
By default the tiles of one region are decoded in parallel on a process-wide
thread pool, so a single calling thread already uses several cores. An
application that already runs about as many reads at once as it has cores,
such as a tile server or a threaded data loader, can instead keep every read
on its calling thread. The pool size defaults to the number of hardware
threads of the host and can be changed at run time. Decoded tiles are not cached unless the
user asks: a least-recently-used cache can be attached to one slide or shared
by all slides in the process, and it reports hits and misses. The iSyntax
reader additionally keeps wavelet coefficients in a bounded cache.

**Distribution.** All codecs are compiled into the Python extension, which on
Linux links only against the C and C++ runtime libraries. The same core is exposed through a C API; the
repository also holds Java, Rust, Go, and WebAssembly bindings and a
command-line tool.

**Limitations.** Where tiles overlap, as in CZI mosaics, `FastSlide` paints
them in a fixed order, so a region does not depend on thread timing. The OME-Zarr reader takes version 0.5 images with one Z
and T plane, and not yet sharded or Blosc-compressed arrays. Signed 8-bit
samples are returned as unsigned, 16-bit JPEG-XR CZI images are not decoded,
and a region that crosses the slide border is cropped, not padded.

# Validation and performance

We compared level-0 pixels with independent readers on public files. Output
was bit-identical to libCZI (through pylibCZIrw 6.1.0) for 180 regions on
three slides of the SurGen dataset [@myles2025surgen], to tifffile for a QPTIFF
slide and for all 35 Z/T planes of an OME-TIFF test file, to zarr-python 3.1.5
for two OME-Zarr images (a five-channel 16-bit image from the Image Data
Resource and an H&E slide from the SpatialData samples), and to
`OpenSlide` for 100 regions on each of four JPEG-compressed slides (SVS, NDPI,
MRXS, and generic TIFF). Regions of a JPEG 2000 SVS slide differed from
`OpenSlide` by at most 1 gray level, as did JPEG 2000 DICOM tiles from an
OpenJPEG decode. VSI regions were within 2 of Bio-Formats 8.1.1, and 99.96% of
the values of a BIF slide within 5 of `OpenSlide`. iSyntax output was compared with the
Philips SDK during development; we could not repeat that with public tools.

\autoref{tab:bench} compares `FastSlide` 0.8.6 with `OpenSlide` 4.0.1 on five
public OpenSlide test slides. `FastSlide` was measured in its default mode, in
which the tiles of a read are decoded on a shared pool, and with every read on
its calling thread, which uses one CPU per Python thread as `OpenSlide` does.
On its calling thread, `FastSlide` read 1.4 to 4.2 times as many regions per
second as `OpenSlide` on the four JPEG slides and 0.8 times as many on the
JPEG 2000 slide, with one Python thread or eight. In the default mode, one
Python thread used 4.0 to 11.5 CPUs and read 2.0 to 11.6 times as many regions
as `OpenSlide`; with eight Python threads the ratio was 1.1 to 5.0.

: Regions read per second from one open slide by one or eight Python threads,
which together read 1,000 random 512 × 512 level-0 regions and convert each to
a NumPy array; median of five runs. Pool: `FastSlide` in its default mode, with
a shared pool of 16 threads. Calling: every read on its calling thread. Neither
library used a tile cache beyond its default, and the files were in the page
cache. Bold marks the fastest per row; the ranges of the five runs did not
overlap. The process had 16 logical CPUs of a server with two 64-core AMD EPYC
7763 processors. \label{tab:bench}

| Slide (compression)       | FastSlide, pool | FastSlide, calling | OpenSlide |
|:--------------------------|----------:|----------:|----------:|
| *One Python thread*       |           |           |           |
| Aperio SVS (JPEG)         | **1,996** |       478 |       172 |
| Aperio SVS (JPEG 2000)    |   **119** |        47 |        56 |
| Hamamatsu NDPI (JPEG)     | **1,576** |       246 |       179 |
| 3DHISTECH MRXS (JPEG)     | **3,405** |     2,637 |     1,680 |
| Generic tiled TIFF (JPEG) | **1,032** |       745 |       178 |
| *Eight Python threads*    |           |           |           |
| Aperio SVS (JPEG)         | **3,914** |     3,128 |     1,006 |
| Aperio SVS (JPEG 2000)    |   **400** |       293 |       349 |
| Hamamatsu NDPI (JPEG)     | **2,161** |     1,529 |     1,038 |
| 3DHISTECH MRXS (JPEG)     | **14,701** |   13,425 |     5,880 |
| Generic tiled TIFF (JPEG) | **5,145** |     3,934 |     1,036 |

# Example

Reading a grid of tiles from one slide with a thread pool:

```python
from concurrent.futures import ThreadPoolExecutor
from fastslide import FastSlide

with FastSlide.from_file_path("CMU-1.svs") as slide:
    level = slide.get_best_level_for_downsample(4.0)
    width, height = slide.level_dimensions[level]
    # Locations are in the pixel coordinates of the level being read.
    grid = [(x, y) for y in range(0, height - 512, 512)
            for x in range(0, width - 512, 512)]
    with ThreadPoolExecutor(max_workers=8) as pool:
        tiles = list(pool.map(
            lambda xy: slide.read_region(xy, level, (512, 512)).numpy(), grid))

print(len(tiles), tiles[0].shape, tiles[0].dtype)  # 352 (512, 512, 3) uint8
```

# Research impact statement

`FastSlide` has been available on PyPI since May 2026. It is a dependency and
slide backend of our `DLUP` library [@dlup] and the default slide reader of
our [`dlux`](https://github.com/NKI-AI/dlux) pipeline. Through our
[extension](https://github.com/NKI-AI/qupath-extension-fastslide) it is also
an image reader for QuPath [@bankhead2017qupath]. On 25 November 2026, pypistats.org
reported 3,284 downloads from PyPI over the preceding 30 days, and the issue
tracker held 45 issues opened by eleven GitHub accounts. The benchmark and
figure in this paper can be regenerated with the scripts in `paper/scripts`.
We are not aware of published studies that used `FastSlide` yet.

# AI usage disclosure

\textcolor{red}{Generative AI tools were used during development of the
software for code clean-up and, where relevant, to improve individual
functions. All such changes were reviewed by the authors. [TO CONFIRM WITH
J.T.: tool names and versions, and whether this statement is complete; JOSS
asks that the disclosure cover the software, its documentation, and the
paper.]}

# Acknowledgements

We thank the `OpenSlide` project for its format documentation and public test
slides. `FastSlide` builds on libjpeg-turbo,
OpenJPEG, libdicom, Little CMS, Highway, nanobind, pugixml, and `libisyntax`.
\textcolor{red}{[TO ADD: funding statement.]}

# References
