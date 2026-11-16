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
date: 15 November 2026
bibliography: paper.bib
---

# Summary

A whole slide image (WSI) is a scan of a glass microscope slide, stored as a
multi-resolution pyramid of compressed tiles that at full resolution
can exceed 100,000 pixels per side. Software that works with these images asks a
reader library for rectangular regions at a chosen resolution.

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
PyPI for Linux, macOS, and Windows on x86-64 and ARM64.

![Regions read with `FastSlide` from two public slides. Left: a brightfield H&E slide in Aperio SVS format (CMU-1 from the OpenSlide test data). Right: a five-channel fluorescence slide in QPTIFF format (LuCa-7color_Scan1 from the OME sample images), showing the DAPI (blue), FITC (green), and CY5 (red) channels.\label{fig:regions}](figures/regions.png)

# Statement of need

Deep learning models in computational pathology are trained on large collections of
WSIs [@campanella2019clinical]. Training and inference read many small
regions, usually from several data-loading threads or processes. Two practical
problems follow. First, each scanner vendor writes its own container, and a cohort
collected across hospitals or over several years mixes formats; a pipeline then
depends on several readers that differ in coordinate conventions and in the
array layout they return. Second, multiplex immunofluorescence slides store more than three
channels, often at 16 bits, which readers designed for 8-bit RGB cannot return
unchanged.

`FastSlide` is written for researchers who build data pipelines and viewers
on top of WSIs and want one dependency for the formats in their cohorts. It reports pyramid levels, pixel spacing, channel names,
associated images (label, macro, thumbnail), and the embedded ICC profile
through the same properties for every format, and it reads brightfield and fluorescence slides
through the same call.

# State of the field

`OpenSlide` [@goode2013openslide] is the established open-source WSI
reader. It is a C library under the LGPL 2.1; version 4.0.1 lists thirteen
formats and has been safe for concurrent reads on one handle since version 3.3.1. Its read
function copies premultiplied 8-bit ARGB into a caller-provided buffer and
takes a position, a level, and a size. There is no argument to select a
channel, a focal plane, or a time point, so fluorescence channels beyond three
and 16-bit data cannot be returned, and its format list does not include
Philips iSyntax, Olympus VSI, QPTIFF, or OME-Zarr. `Bio-Formats`
[@linkert2010bioformats] reads 165 microscopy formats with full five-dimensional
access, but it is a Java library whose readers are documented as not
thread-safe, and its maintainers provide no Python implementation. `cuCIM`
[@lee2021cucim] targets GPU pipelines and documents support for Aperio SVS,
Philips TIFF, and generic tiled TIFF. `TiffSlide` [@poehlmann2022tiffslide] is
a pure-Python replacement for the OpenSlide Python API built on `tifffile`,
and is limited to TIFF-based formats.

We wrote a new library instead of extending `OpenSlide` because the features we
needed, pixel data in its stored type with any number of channels and selection
of a Z/T plane, do not fit an interface whose output is a `w × h × 4` byte ARGB
buffer. To keep existing datasets usable, `FastSlide` computes the same
"quickhash" slide identifier as `OpenSlide`: the two agreed on seven of the eight public
OpenSlide test slides we compared, the exception being a Ventana BIF slide.

# Software design

**Readers plan before they read.** Each format is a reader class registered
with a central registry, which selects the reader from the file name. A reader
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
size are then in the same unit; helper functions convert in both directions.

**Images keep their stored layout.** `read_region` returns an `Image` that
records its data type, channel count, and whether channels are interleaved or
stored as separate planes, and can convert between the two layouts. The NumPy
array is a view of the image buffer.

**Concurrency and caching.** The Python binding releases the global interpreter
lock while a region is read, and one slide handle can be shared between
threads. In our tests, 200 random regions read by 32 threads from one handle
were bit-identical to the same regions read serially for files in all twelve
formats.
The tiles of one region are decoded
in parallel on a process-wide thread pool, so a single calling thread already
uses several cores. Decoded tiles are not cached unless the
user asks: a least-recently-used cache can be attached to one slide or shared
by all slides in the process, and it reports hits and misses. When several
threads miss on the same tile, it is decoded once.

**Distribution.** All codecs are compiled into the Python extension, so on
Linux the extension links only against the C and C++ runtime libraries. The same core is exposed through a C API; the
repository also holds Java, Rust, Go, and WebAssembly bindings and a
command-line tool.

**Limitations.** Where tiles overlap, as in CZI mosaics, `FastSlide` paints
them in a fixed order, so a region does not depend on thread timing; this
makes painting serial. The OME-Zarr reader takes version 0.5 images with one Z
and T plane, and not yet sharded or Blosc-compressed arrays.

# Validation and performance

We compared level-0 pixels with independent readers on public files. Output
was bit-identical to libCZI (through pylibCZIrw 6.1.0) for 180 regions on
three slides of the SurGen dataset [@myles2025surgen], to tifffile for a QPTIFF
slide and for all 35 Z/T planes of an OME-TIFF test file, and to zarr-python
3.1.5 for two OME-Zarr images: a five-channel 16-bit image from the Image Data
Resource (idr0033) and an H&E slide from the SpatialData sample data. For
lossy JPEG and JPEG 2000 data, where decoders round differently, SVS regions
differed from `OpenSlide` and DICOM tiles from an OpenJPEG decode by at most
one grey level. VSI regions differed from Bio-Formats 8.1.1 by at most two grey levels. A
BIF slide was aligned with `OpenSlide`, with 99.8% of pixels within five grey
levels. We had no
independent reader for iSyntax.

\autoref{tab:bench} compares `FastSlide` 0.8.4 with `OpenSlide`
4.0.1 (openslide-python 1.4.7) on five public OpenSlide test slides. With one
calling thread, `FastSlide` read 1.9 to 5.7 times as many regions per second.
This is not a single-core comparison: because of its thread pool, `FastSlide`
used 3.9 (JPEG) to 4.5 (JPEG 2000) CPU-seconds per second of wall-clock time on
the two SVS slides, where `OpenSlide` used one. With eight calling threads the
throughput of `FastSlide` changed little, and `OpenSlide` was faster on the
NDPI and JPEG 2000 slides and comparable on MRXS.

: Regions read per second when one
Python thread, or eight Python threads sharing one open slide, read 1,000
random 512 × 512 level-0 regions and convert each to a NumPy array. Values are
the median of five runs. Bold marks the faster library for that slide and
thread count; neither is bold where the ranges of the five runs overlap. Both
libraries ran with default settings and the files were in the page cache.
Measured on a shared server with two 64-core AMD EPYC 7763 processors. \label{tab:bench}

| Slide (compression)       | FastSlide, 1 thread | OpenSlide, 1 thread | FastSlide, 8 threads | OpenSlide, 8 threads |
|:--------------------------|----------:|----------:|----------:|----------:|
| Aperio SVS (JPEG)         |   **839** |       171 |   **924** |       496 |
| Aperio SVS (JPEG 2000)    |   **136** |        56 |       160 |   **193** |
| Hamamatsu NDPI (JPEG)     |   **342** |       182 |       307 |   **604** |
| 3DHISTECH MRXS (JPEG)     | **3,405** |     1,683 |     3,486 |     3,273 |
| Generic tiled TIFF (JPEG) | **1,021** |       178 | **1,163** |       524 |

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
slide backend of `DLUP` [@dlup], and the default slide reader of
[`dlux`](https://github.com/NKI-AI/dlux). Through a separate
[extension](https://github.com/NKI-AI/qupath-extension-fastslide) it is
an image reader for QuPath [@bankhead2017qupath]. On 15 November 2026, pypistats.org
reported 2,272 downloads from PyPI over the preceding 30 days, and the issue
tracker held 31 issues opened by seven GitHub accounts. The benchmark and
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
slides. `FastSlide` builds on open-source components including libjpeg-turbo,
OpenJPEG, libdicom, Little CMS, Highway, nanobind, pugixml, and `libisyntax`.
\textcolor{red}{[TO ADD: funding statement.]}

# References
