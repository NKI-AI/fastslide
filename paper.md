---
title: 'FastSlide: A High-Performance Whole Slide Image Reader for Digital Pathology'
tags:
  - C++
  - Python
  - digital pathology
  - whole slide imaging
  - computational pathology
  - medical imaging
  - histopathology
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
date: 1 December 2025
bibliography: paper.bib
---

# Summary

FastSlide is a modern, high-performance library for reading whole slide images (WSI) in digital pathology. Written in C++20 with native Python bindings, FastSlide provides efficient access to multiple slide formats including Aperio SVS, 3DHISTECH MRXS, and PerkinElmer QPTIFF. The library is specifically designed (but not limited to) for artificial intelligence (AI) and machine learning (ML) workflows, offering practical multi-threaded operation, intelligent caching, CLI tooling, and seamless integration with common frameworks like PyTorch. Beyond the Python module, FastSlide exposes a C API, a Meson-powered SDK, and a benchmarking harness to support integration into viewers, training pipelines, and automated slide QC systems. At its core, FastSlide implements a two-stage planning/execution pipeline that separates tile selection from decoding, enabling reproducible scheduling, inspectable cost models, and optimal performance for both interactive viewing and batch processing scenarios.

# Statement of Need

Digital pathology workflows increasingly rely on computational analysis of whole slide images (WSIs), which routinely exceed gigapixel resolutions and produce terabytes per cohort [@lecun2015deep; @litjens2016deep; @campanella2019clinical]. Clinics and research groups train deep neural networks on public benchmarks such as CAMELYON17 [@bandi2018detection] and deploy large-scale quality assurance pipelines inside viewers such as QuPath [@bankhead2017qupath], both of which require deterministic, multi-threaded slide reading. These workloads increasingly combine RGB brightfield and multiplex immunofluorescence data, expect fault-tolerant caching, and must operate reliably across environments ranging from interactive notebooks to distributed PyTorch [@paszke2019pytorch] training loops.

Several important community efforts already exist. OpenSlide [@openslide] has played a foundational role for over a decade and remains central to many academic and clinical projects. Likewise, Bio-Formats [@linkert2010metadata] offers extensive multi-format support, though it is primarily a Java-based ecosystem. However, there is currently no permissively licensed, high-performance C++ reader that offers native Python bindings, modern concurrency primitives, and an extensible architecture suitable for today’s AI/ML workloads. As datasets grow and pipelines become increasingly parallelized, this gap becomes more apparent—particularly for training loops that require thread-safe decoding, composable abstractions, and predictable performance.

FastSlide aims to fill this space by providing a modern C++20 implementation designed explicitly with the following design principles:


1. **Memory Safety**: Automatic resource management using RAII, smart pointers, and `absl::StatusOr` for error handling (no exceptions).
2. **Concurrency-focused design**: Lock-free `pread()` I/O, thread-local decode contexts, and concurrency-aware caching enable practical multi-threaded access without global locks.
3. **Performance**: SIMD-optimized image processing, intelligent LRU caching, and a two-stage planning/execution pipeline minimize unnecessary computation.
4. **Extensibility**: A clean plugin architecture with format descriptors allows easy addition of new formats without modifying core code.
5. **Interoperability**: Native Python bindings via pybind11 [@pybind11] provide zero-copy NumPy integration and PyTorch DataLoader compatibility.

The library is particularly valuable for researchers developing AI models for digital pathology, where efficient data loading from multi-gigapixel images is critical for training performance.

# Implementation Overview

FastSlide keeps format-specific code thin by building everything on a common two-stage pipeline. Each reader first plans a request (validating bounds, enumerating tiles, and describing the output layout in a `TilePlan`) and only then executes it (reading and decoding the listed tiles, applying any transforms, and writing pixels through a `TileWriter`). This separation makes scheduling deterministic, enables caching of plans for interactive viewers, and keeps execution tight for batch workloads.

Supported formats include Aperio SVS (through the in-tree SimpleTIFF engine, our native C++ implementation of a TIFF reader), 3DHISTECH MRXS (with index parsing and overlap-aware blending), and PerkinElmer QPTIFF (exposing per-channel metadata and selective loading). All formats plug into the same reader registry, cache manager, and Python bindings, so applications see a uniform API across both C++ and Python.

Key features:

- multi-thread-aware I/O via SimpleTIFF (`pread()` plus thread-local decode contexts);
- optional LRU caches exposed in both languages for sharing decoded tiles between workers;
- native Python bindings that return NumPy arrays, with helpers for associated images, coordinate conversion, channel visibility, and OpenSlide-compatible quick hashes on SVS/MRXS;
- a C API (`libfastslide_c`) for non-Python consumers that mirrors the same concepts (reader creation, metadata access, `read_region`);
- lightweight CLI commands (`fastslidetool info|region`) and benchmark targets for automated slide QA.

## Python Examples

`fastslide.FastSlide` integrates directly with NumPy/PyTorch data pipelines. The first example reads a region from the public CMU-1 MRXS slide and saves it as a PNG:

```python
import fastslide
from PIL import Image

slide_path = "path/to/CMU-1-Exported.mrxs"

with fastslide.FastSlide.from_file_path(slide_path) as slide:
    location = (40440, 154894)  # level-0 coordinates (with bounds applied)
    region = slide.read_region(location=location, level=0, size=(5000, 5000))
    image = Image.fromarray(region)
    image.show()
    image.save("example_cmu_slide.png")
```

![Example of a region of an H&E slide extracted from the CMU-1 MRXS slide using FastSlide.](example_cmu_slide.png){ width=90% }


For multiplex QPTIFF data, you can pick channels and build RGB composites (e.g., CD8/CD4/DAPI) in a few lines:

```python
import fastslide, numpy as np
from PIL import Image

slide_path = "qptiff_example.qptiff"

with fastslide.FastSlide.from_file_path(slide_path) as slide:
    channels = slide.channel_metadata # list of channel metadata with biomarker names
    region = slide.read_region(location=(15000, 15300), level=0, size=(2000, 2000))
```

![Example of a region extracted from a QPTIFF image containing the CD8/CD4/DAPI biomarkers.](example_codex_cd8_cd4_dapi.png){ width=90% }


Bindings also expose associated images, coordinate conversions, channel visibility controls, cache tuning hooks, and OpenSlide-compatible quick hashes where available, so notebook users rarely have to drop into C++.

# Use Cases

FastSlide is used to:
1.  Stream large training patches into PyTorch DataLoaders while sharing caches across workers
2.  Drive interactive viewers that prefetch tiles and blend MRXS overlaps without seams
3.  Run scheduled QC/thumbnail extraction through the CLI or C API
4.  Analyze multiplex QPTIFF channels by selecting only the biomarkers needed for a given spectral pipeline

# Future Work

Upcoming milestones focus on broadening format support (additional TIFF derivatives), adding URI-based loading so `FastSlide.from_uri` can stream slides from object storage, experimenting with GPU-accelerated JPEG decoding, and layering in background prefetchers so large batches can be prepared while earlier batches are still training. We also plan to upstream optional gRPC bindings based on the existing C interface so FastSlide can serve slides over the network without embedding the full library inside every client.

# Availability and Community

FastSlide is open-source software released under the Apache License 2.0. The source code is available at [https://github.com/NKI-AI/fastslide](https://github.com/NKI-AI/fastslide), with comprehensive documentation at [https://docs.aifo.dev/fastslide/](https://docs.aifo.dev/fastslide/).

The library targets Python 3.11+ on Linux, Windows, and macOS (x86_64 or ARM64) with modern C++20 compilers (GCC ≥ 11, Clang ≥ 14, or MSVC 19.3+). Wheels are built via Meson and published to PyPI (`pip install fastslide`), while developers who customise formats can build via Meson and install locally with `(uv) pip install .`.

We welcome contributions via GitHub Issues and Discussions; feature requests for new formats, bug reports with sample slides, and documentation improvements all feed directly into our roadmap. Comprehensive developer docs live alongside the Sphinx user guide, and every pull request runs the Meson and Python test suites against the bundled sample data to keep CI fast and deterministic.

# Acknowledgments

We acknowledge the OpenSlide project for their pioneering work in making whole slide images accessible to the research community. FastSlide builds upon the format documentation and reverse-engineering efforts of the OpenSlide team and the broader digital pathology community.

# References



