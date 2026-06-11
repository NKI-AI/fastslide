---
title: 'FastSlide: A high-performance, multi-format whole slide image reader for digital pathology'
tags:
  - C++
  - Python
  - digital pathology
  - whole slide imaging
  - computational pathology
  - medical imaging
  - histopathology
  - deep learning
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
date: 11 June 2026
bibliography: paper.bib
---

# Summary

`FastSlide` is a high-performance, open-source library for reading whole slide
images (WSIs) in digital and computational pathology. It is written in modern
C++20 with first-class Python bindings, so the same engine powers interactive
viewers and large-scale machine-learning pipelines alike. A single uniform API
reads twelve brightfield and fluorescence formats produced by the major scanner
vendors and by open imaging standards (Aperio SVS, 3DHISTECH MRXS,
Akoya/PerkinElmer QPTIFF, Hamamatsu NDPI, Philips iSyntax, Ventana BIF, Zeiss
CZI, Olympus VSI, OME-TIFF, OME-Zarr, DICOM, and generic TIFF), and returns
pixels through one `Image` type regardless of source. Beyond conventional
two-dimensional RGB slides, `FastSlide` treats each acquisition as a
`C·X·Y·Z·T` hyper-volume: it exposes fluorescence channels (C), focal planes
(Z), and time points (T), and files that contain several images or scenes, such
as Zeiss CZI scenes and Olympus VSI navigator/region images, are surfaced as an
indexable sequence of independently navigable pyramids. This spans routine
brightfield histology (\autoref{fig:he}) through high-plex fluorescence
(\autoref{fig:multiplex}).

![A region of a brightfield hematoxylin-and-eosin (H\&E) slide read with `FastSlide` and returned as a zero-copy RGB array.\label{fig:he}](figures/he_region.png){ width=62% }

`FastSlide` is engineered for the throughput and concurrency demanded by AI/ML
workflows. Lock-free positional I/O, thread-local decode contexts, and a shared,
instrumented least-recently-used (LRU) tile cache allow many threads or PyTorch
[@paszkePyTorchImperativeStyle2019] data-loader workers to stream tiles from a
single slide without contention, returning zero-copy NumPy arrays
[@harrisArrayProgrammingNumPy2020]. Every
reader is built on a two-stage planning/execution pipeline that separates tile
selection from decoding, yielding deterministic, inspectable, and unit-testable
scheduling. The same C++ core is reached from Python, a C API, and Rust, Go,
Java, and WebAssembly bindings, ships a command-line tool, and powers a QuPath
[@bankheadQuPathOpenSource2017] extension that opens every supported format
without code.
`FastSlide` is released under the Apache License 2.0, distributed as prebuilt
wheels (CPython 3.10–3.14) on PyPI for Linux, macOS, and Windows on both x86\_64
and ARM64, and builds from source with either Meson or Bazel.

![A region of a multiplex immunofluorescence slide read with `FastSlide` and composited from three of its channels, selected directly through the reader's channel API: cell nuclei (DAPI, blue), B cells (CD20, green), and cytotoxic T cells (CD8, red).\label{fig:multiplex}](figures/multiplex_composite.png){ width=70% }

# Statement of need

Computational pathology increasingly depends on analysing WSIs that routinely
exceed gigapixel resolution and produce terabytes of data per study
[@litjensSurveyDeepLearning2017; @campanellaClinicalgradeComputationalPathology2019].
Deep-learning models are trained on public benchmarks such as CAMELYON
[@bandiDetectionIndividualMetastases2019] and deployed inside
viewers and quality-control pipelines, all of which require fast, deterministic,
and thread-safe access to slide pixels. These workloads now span RGB brightfield
and high-plex immunofluorescence imaging and run across heterogeneous
environments, from interactive notebooks to distributed multi-GPU training loops.
A practical obstacle is *format fragmentation*: each scanner vendor writes its
own proprietary container, and groups commonly stitch together several readers
with inconsistent coordinate conventions and pixel layouts, an arrangement that
is error-prone, hard to maintain, and detrimental to reproducibility.

`OpenSlide` [@goodeOpenSlideVendorneutralSoftware2013] is the de facto open-source foundation for WSI reading
and has served the community well, but its design predates these requirements:
it is written in C, expresses all coordinates relative to the full-resolution
level, has limited support for multi-channel and multi-dimensional (C/Z/T) data,
and offers no built-in tile caching or instrumentation. Complementary tools
cover parts of the space: `Bio-Formats` reads many microscopy formats on the
JVM, `cuCIM` [@leeCuCIMGPUImage2021] targets GPU-accelerated loading,
and several Python-only libraries wrap existing C backends. Yet, to our
knowledge, none combines broad coverage of both vendor and standards-based
pathology formats, native multi-dimensional access across brightfield and
multiplex imaging, a concurrency model purpose-built for high-throughput patch
sampling, and bindings spanning the languages and ecosystems used in pathology
(Python, the JVM via QuPath, Rust, Go, and the browser via WebAssembly).
`FastSlide` consolidates these capabilities behind one tested, memory-safe
interface, reducing the number of moving parts a study must trust and maintain
(Table 1).

: Capability comparison with widely used open-source WSI readers.

| Capability | FastSlide | OpenSlide | cuCIM | Bio-Formats |
|---|---|---|---|---|
| Core language | C++20 | C | C++/CUDA | Java |
| Vendor pathology formats | 12 | ~10 | SVS/TIFF | many (microscopy) |
| DICOM / OME standards | yes | partial | partial | yes |
| Multi-channel (C) | yes | no | partial | yes |
| Focal planes / time (Z/T) | yes | no | no | yes |
| Multiple scenes per file | yes | no | no | yes |
| Thread-safe concurrent reads | lock-free `pread()` | limited | yes | limited |
| Built-in shared tile cache | yes (LRU, telemetry) | no | no | no |
| Zero-copy NumPy / PyTorch | yes | via ctypes | yes | no |
| Language bindings | Py, C, Rust, Go, Java, WASM | C/Py | C++/Py | Java/Py |

# Key capabilities

- **Unified multi-format access.** Twelve registered readers behind one API and
  `Image` type, with automatic format detection through a pluggable registry, so
  applications are written once and run across vendors and standards.
- **Multi-dimensional and multiplex imaging.** Per-`(z, t)` plane reads, full
  channel selection with biomarker metadata for multiplex stacks, multiple
  images/scenes per file, and `uint8`, `uint16`, `uint32`, and `float32` pixel
  types in both interleaved and band-separate layouts.
- **Concurrency and caching.** Slides are opened once and read through
  lock-free positional `pread()` with thread-local decode buffers; an optional
  LRU cache with hit/miss statistics can be shared across workers.
- **Two-stage planning/execution pipeline.** A pure planning stage enumerates
  tiles, computes coordinate transforms, and reports explicit cost estimates
  (bytes, tile counts, timing) before any I/O, enabling deterministic
  scheduling, plan caching for viewers, and testing without disk access.
- **Faithful, high-quality reconstruction.** An in-tree TIFF engine indexes
  directories in memory for fast random access; overlapping MRXS camera fields
  are stitched with Magic Kernel resampling and per-tile gain correction for
  subpixel-accurate, seamless blending; and resampling and pixel-processing
  routines are vectorised with the portable Highway SIMD library [@highway],
  which dispatches to the best available SIMD instruction set at runtime.
- **Ergonomics and interoperability.** Level-native coordinates with
  level-0 conversion helpers, associated images (label, macro, thumbnail),
  OpenSlide-compatible quick hashes, a `fastslidetool` CLI, multi-language
  bindings, and a QuPath extension.

Together these features make `FastSlide` a practical, sustainable building block
for both research data pipelines and pathology applications; it is used in the
authors' own deep-learning workflows for digital pathology.

# Example usage

The Python API mirrors familiar WSI readers while returning arrays that drop
straight into a PyTorch `DataLoader`. Because reads are thread-safe, the dataset
below works unchanged with multiple worker processes:

```python
import fastslide
import torch
from torch.utils.data import Dataset, DataLoader

class TileDataset(Dataset):
    def __init__(self, path, coords, level=0, size=256):
        self.slide = fastslide.FastSlide.from_file_path(path)
        self.coords, self.level, self.size = coords, level, size

    def __len__(self):
        return len(self.coords)

    def __getitem__(self, i):
        x, y = self.coords[i]
        tile = self.slide.read_region((x, y), self.level,
                                      (self.size, self.size)).numpy()
        return torch.from_numpy(tile)  # zero-copy HWC uint8 tensor

loader = DataLoader(TileDataset("slide.svs", coords),
                    batch_size=32, num_workers=8)
```

# Acknowledgements

We thank the `OpenSlide` project and the broader digital pathology community for
their pioneering work and format documentation, on which `FastSlide` builds.
`FastSlide` also relies on a number of open-source components, including Abseil,
Highway, libjpeg-turbo, nanobind, pugixml, and `libisyntax`, whose authors we
gratefully acknowledge.

# References
