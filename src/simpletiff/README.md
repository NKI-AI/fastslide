# SimpleTIFF

A fast, lightweight C++20 TIFF reader library optimized for whole-slide imaging (WSI) and large tiled TIFF files.

## Features

- **Zero-copy design**: Structure of Arrays (SoA) with arena allocators for minimal heap overhead
- **High performance**: O(1) page/tile lookup, cache-friendly data layout
- **BigTIFF support**: Handles files larger than 4GB
- **Flexible storage**: Supports tiled, stripped, and single-JPEG encodings
- **JPEG decompression**: Configurable backend (libjpeg-turbo or jpeg-compressor with Highway SIMD)
- **Modern C++20**: Memory-safe RAII patterns, no raw pointers
- **No libtiff dependency**: Pure C++ implementation
- **WebAssembly support**: Compiles to WASM with Emscripten

## Quick Start

### C++

```cpp
#include <simpletiff/index.h>
#include <simpletiff/tiff_parser.h>
#include <simpletiff/reader.h>
#include <unistd.h>

// Open and parse a TIFF file
simpletiff::TiffIndex index;
int fd;
if (!simpletiff::OpenTiff("slide.tiff", index, fd)) {
  // Handle error
}

// Print information
std::cout << "Pages: " << index.NumPages() << "\n";
std::cout << "First page: " << index.Page(0).width
          << " x " << index.Page(0).height << "\n";

// Read a region from the first page
simpletiff::Roi roi{0, 0, 512, 512};  // x, y, width, height
std::vector<uint8_t> buffer(512 * 512 * 3);  // RGB888
int stride = 512 * 3;

// Create decode context (reusable for multiple reads)
simpletiff::DecodeContext ctx;
auto result = simpletiff::ReadPage(index, 0, roi, ctx, buffer.data(), stride);
if (!result) {
  std::cerr << "Error: " << result.error().message << "\n";
}

close(fd);
```

See [examples/example_read.cpp](examples/example_read.cpp) for a complete working example with command-line parsing and output to PNG/PPM.

### Python

SimpleTIFF provides Python bindings for easy integration:

```python
import simpletiff
import numpy as np

# Open and parse a TIFF file
with simpletiff.TiffFile.open("slide.tiff") as tiff:
    # Get file information
    print(f"Pages: {tiff.num_pages}")
    print(f"BigTIFF: {tiff.is_big_tiff}")

    # Access page metadata
    page = tiff.pages[0]
    print(f"Page 0: {page.width}x{page.height}")
    print(f"Compression: {page.compression}")
    print(f"Storage: {page.storage}")

    # Read a region (returns NumPy array)
    roi = simpletiff.Roi(x=0, y=0, width=512, height=512)
    region = page.read_region(roi)  # Returns (H, W, C) uint8 array
    print(f"Region shape: {region.shape}")
```

## Architecture

### JPEG Decoder Backends

SimpleTIFF supports two JPEG decoder backends:

1. **libjpeg-turbo** (default): Industry-standard JPEG decoder with SIMD optimizations
2. **jpeg-compressor** (jpgd): Lightweight decoder with Highway SIMD for IDCT and color conversion.

The decoder backend is selected at build time:

```bash
# Use jpeg-compressor (jpgd) backend
bazelisk build //aifo/simpletiff:simpletiff --define=jpeg_decoder=jpgd

# Use libjpeg-turbo backend (default)
bazelisk build //aifo/simpletiff:simpletiff
```

The jpgd backend is particularly useful for WebAssembly builds where libjpeg-turbo gives broken outputs.

## Building

### With Bazel

```bash
# Build library
bazelisk build //aifo/simpletiff:simpletiff

# Build and run tests
bazelisk test //aifo/simpletiff:all

# Build example
bazelisk build //aifo/simpletiff/examples:example_read

# Build Python bindings
bazelisk build //aifo/simpletiff/python:simpletiff

# Build WebAssembly version
bazelisk build //aifo/simpletiff/wasm:wasm --platforms=//platforms:wasm32 --define=jpeg_decoder=jpgd
```

## Core Types

- `TiffIndex`: Main index structure containing all pages and metadata
- `PageHeader`: Metadata for a single IFD/page (width, height, storage type, etc.)
- `DecodeContext`: Reusable decompression context (contains JPEG state, buffers)
- `Roi`: Region of interest (x, y, width, height)
- `Result<T>`: Error handling wrapper (contains value or error)

## Storage Types

SimpleTIFF automatically handles different storage formats:

```cpp
const auto& page = index.Page(page_idx);

switch (page.storage) {
  case simpletiff::Storage::kTiles: {
    // Tiled storage - supports efficient ROI reads
    const auto& tiles = index.Tiles(page.payload_id);
    auto offsets = index.Offsets(tiles.offsets);
    auto bytecounts = index.Bytecounts(tiles.bytecounts);
    break;
  }
  case simpletiff::Storage::kStrips: {
    // Strip-based storage
    const auto& strips = index.Strips(page.payload_id);
    auto offsets = index.Offsets(strips.offsets);
    auto bytecounts = index.Bytecounts(strips.bytecounts);
    break;
  }
  case simpletiff::Storage::kSingleJpeg: {
    // Single embedded JPEG
    const auto& single = index.SingleJpeg(page.payload_id);
    break;
  }
}
```

## Performance Notes

- **Tiled files**: Use `ReadPage` with small ROIs for best performance
- **Decode context**: Reuse `DecodeContext` across multiple reads to avoid reallocating working buffers
- **JPEG tables**: Cached once per page in the index (shared across threads), then reused for all tiles/strips
- **Threading**: Each thread needs its own `DecodeContext` for working buffers (thread-local storage recommended)
- **I/O**: Uses `pread()` for thread-safe, lock-free random access

## Limitations

- **Read-only**: No TIFF writing support
- **Compression**: JPEG, LZW, ZSTD, and uncompressed only
- **Sample formats**: Only byte-aligned formats (8, 16, 32 bits/sample)
  - Packed formats (1-bit, 4-bit, 12-bit) are not supported
  - This is an explicit design decision to keep SimpleTIFF simple and maintainable
- **Photometric**: RGB/YCbCr primary focus (grayscale supported)
- **POSIX only**: Uses `pread`; Windows support requires porting

## Dependencies

### Core Dependencies

- **C++20 compiler** (GCC 10+, Clang 11+, MSVC 2019+)
- **zstd** - ZSTD decompression
- **libpng** - PNG image writing (examples only, not used for TIFF decompression)

### JPEG Decoder (one of):

- **libjpeg-turbo** - Default JPEG decoder
- **jpeg-compressor** - Alternative lightweight JPEG decoder

### Third-Party Components

SimpleTIFF incorporates the following third-party software:

- **jpeg-compressor (jpgd)** - Public domain JPEG decoder by Rich Geldreich

  - Licensed under: Public Domain
  - Used for: JPEG decompression with Highway SIMD optimizations for IDCT and color conversion
  - Repository: Integrated from various sources with Highway SIMD enhancements

- **Google Highway** - Portable SIMD library
  - Licensed under: Apache License 2.0
  - Used for: SIMD-accelerated JPEG IDCT and color conversion in jpgd backend
  - Repository: https://github.com/google/highway

## License

Copyright 2025 Jonas Teuwen

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.
