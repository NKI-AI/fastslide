# FastSlide

> High-performance whole slide image reader for digital pathology

FastSlide is a modern C++20 library for reading whole slide images (WSI) with first-class Python support. Designed for AI/ML workflows, it provides thread-safe, efficient access to multiple slide formats.

**📖 Documentation:** https://docs.aifo.dev/fastslide/

**📜 License:** Apache 2.0

## Features

- 🚀 **High Performance** - Thread-safe design with LRU tile caching
- 📁 **Multiple Formats** - SVS (Aperio), QPTIFF, MRXS (3DHISTECH), including multiplex (non-RGB) formats. Others coming soon
- 🐍 **Python & C++** - Complete APIs for both languages
- 🔧 **PyTorch Ready** - Works seamlessly with DataLoader multi-worker loading
- 📊 **Production Ready** - Robust error handling and comprehensive testing

## Quick Start

### Installation

#### Option 1: Using uv

```bash
# Install in development mode
uv pip install -e .

# Or install from source
uv pip install .
```

#### Option 2: Using Meson directly

```bash
# Configure for your Python environment
export MESON_PREFIX=$(python -c "import sys; print(sys.prefix)")
meson setup builddir --prefix="$MESON_PREFIX" --wrap-mode=forcefallback --prefer-static
meson compile -C builddir
meson install -C builddir
```

### Python Usage

#### Basic Example: Opening and Reading a Slide

```python
import fastslide

# Open a slide using context manager (automatically closes when done)
with fastslide.FastSlide.from_file_path('slide.svs') as slide:
    # Get slide information
    print(f"Dimensions: {slide.dimensions}")  # (width, height) at level 0
    print(f"Levels: {slide.level_count}")     # Number of pyramid levels
    print(f"Resolution: {slide.mpp} µm/pixel")
    print(f"Format: {slide.format}")           # e.g., "SVS", "MRXS", "QPTIFF"

    # Read a region at full resolution (level 0)
    region = slide.read_region(
        location=(1000, 2000),  # (x, y) in level-native coordinates
        level=0,                 # pyramid level
        size=(512, 512)          # (width, height)
    )
    # region is a numpy array: shape (512, 512, 3), dtype uint8
```

#### Example: Manual Resource Management

```python
import fastslide

# Open a slide without context manager
slide = fastslide.FastSlide.from_file_path('slide.mrxs')

try:
    # Work with the slide
    region = slide.read_region(location=(0, 0), level=0, size=(1024, 1024))

    # Get slide properties
    props = slide.properties
    print(f"Scanner: {props.get('scanner_model', 'Unknown')}")
    print(f"Magnification: {props.get('objective_magnification', 'N/A')}")

finally:
    # Always close the slide to release resources
    slide.close()
```

#### Example: Working with Multiple Pyramid Levels

```python
import fastslide

with fastslide.FastSlide.from_file_path('slide.tiff') as slide:
    # Get information about all pyramid levels
    print(f"Level count: {slide.level_count}")
    print(f"Level dimensions: {slide.level_dimensions}")
    print(f"Level downsamples: {slide.level_downsamples}")

    # Read the same region at different resolutions
    location = (10000, 15000)
    size = (256, 256)

    # Full resolution (level 0)
    region_l0 = slide.read_region(location=location, level=0, size=size)

    # 4× downsampled (level 2)
    # Convert coordinates to level 2 space
    x_l2, y_l2 = slide.convert_level0_to_level_native(
        location[0], location[1], level=2
    )
    region_l2 = slide.read_region(location=(x_l2, y_l2), level=2, size=size)

    # Find best level for a specific downsample factor
    best_level = slide.get_best_level_for_downsample(8.0)
    print(f"Best level for 8× downsample: {best_level}")
```

#### Example: Accessing Associated Images

```python
import fastslide
from PIL import Image

with fastslide.FastSlide.from_file_path('slide.svs') as slide:
    # Check what associated images are available
    associated = slide.associated_images
    print(f"Available images: {associated.keys()}")  # e.g., ['thumbnail', 'macro', 'label']

    # Read thumbnail (lazy loaded)
    if 'thumbnail' in associated:
        thumbnail = associated['thumbnail']  # numpy array

        # Convert to PIL Image and save
        img = Image.fromarray(thumbnail)
        img.save('thumbnail.png')

        # Get dimensions without loading
        dims = associated.get_dimensions('thumbnail')
        print(f"Thumbnail size: {dims}")
```

### C++ Usage

```cpp
#include "fastslide/slide_reader.h"
#include "fastslide/runtime/reader_registry.h"

// Create reader
auto reader = fastslide::runtime::GetGlobalRegistry()
    .CreateReader("slide.svs");

// Read region
fastslide::RegionSpec spec{
    .top_left = {1000, 2000},
    .size = {512, 512},
    .level = 0
};
auto image = reader->ReadRegion(spec);
```

## Supported Formats

| Format         | Extension       | Features                                     |
| -------------- | --------------- | -------------------------------------------- |
| Aperio SVS     | `.svs`          | Multi-resolution pyramids, associated images |
| QPTIFF         | `.tif`, `.tiff` | Multi-channel fluorescence, metadata         |
| 3DHISTECH MRXS | `.mrxs`         | Overlapping tiles, spatial indexing          |

## Key Features

### Thread-Safe Multi-Processing

```python
from torch.utils.data import DataLoader

# Each worker gets its own slide reader
dataloader = DataLoader(
    dataset,
    batch_size=32,
    num_workers=8,  # Safe for multi-worker loading
    shuffle=True
)
```

### Level-Native Coordinates

FastSlide uses level-native coordinates for region reading. This is where
FastSlide clearly deviates from OpenSlide, which always represents the coordinates in level 0.

```python
# Level 0: 10000 × 8000 px (full resolution)
# Level 1: 5000 × 4000 px (2× downsample)
# Level 2: 2500 × 2000 px (4× downsample)

# Read 512×512 region from level 2 at position (100, 200)
region = slide.read_region((100, 200), level=2, size=(512, 512))

# Convert coordinates between levels if needed
x0, y0 = slide.convert_level_native_to_level0(100, 200, level=2)
# Returns: (400, 800) - the level-0 equivalent
```

## Documentation

📖 **Complete documentation:** https://docs.aifo.dev/fastslide/

## Contributing

We welcome contributions! See [CONTRIBUTING.md](../../CONTRIBUTING.md) for guidelines.

## Third-Party Components

FastSlide incorporates the following third-party software:

- **SHA-256 implementation** from [sha-2](https://github.com/amosnier/sha-2) by Alain Mosnier
  - Licensed under: The Unlicense or Zero Clause BSD license
  - Used for: Quick hash computation compatible with OpenSlide

- **unordered_dense** from [martinus/unordered_dense](https://github.com/martinus/unordered_dense) by Martin Leitner-Ankerl
  - Licensed under: MIT License
  - Used for: Fast hashmap/hashset for spatial lookup in the Mirax format

- **lodepng** from [vandeve/lodepng](https://github.com/lvandeve/lodepng) by Lode Vandevenne
  - Licensed under: Zlib License
  - Used for: Decoding PNG in file formats and to write png in examples.

- **pugixml**: from [pugixml.org](https://pugixml.org/)
  - Licensed under: MIT License
  - Used for: Parsing of XML headers

## Citation

```bibtex
@software{fastslide,
  title = {FastSlide: High-performance whole slide image reader},
  author = {George Yiasemis, Rolf Harkes and Jonas Teuwen},
  year = {2025},
  url = {https://github.com/NKI-AI/fastslide}
}
```

## Support

- **Documentation**: https://docs.aifo.dev/fastslide/
- **Issues**: [GitHub Issues](https://github.com/NKI-AI/fastslide/issues)
- **Discussions**: [GitHub Discussions](https://github.com/NKI-AI/fastslide/discussions)

## License

FastSlide is licensed under the **Apache License, Version 2.0**.

See [LICENSE](../../LICENSE) for full details.