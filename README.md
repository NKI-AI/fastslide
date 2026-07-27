# FastSlide

> High-performance whole slide image reader for digital pathology

FastSlide is a modern C++20 library for reading whole slide images (WSI) with first-class Python support. Designed for AI/ML workflows, it provides thread-safe, efficient access to multiple slide formats.

**📖 Documentation:** https://docs.aifo.dev/fastslide/

**📜 License:** Apache 2.0

## Features

- 🚀 **High Performance** - Thread-safe design
- 🐍 **Python & C++** - Complete APIs for both languages
- 🔧 **PyTorch Ready** - Works seamlessly with DataLoader multi-worker loading
- 🔬 **Multi-dimensional** - Channels, focal planes (Z) and time points (T): full `C·X·Y·Z·T` selection for OME-TIFF and CZI
- 🖼️ **Multiple images / scenes** - Zeiss CZI scenes and Olympus VSI navigator/region images exposed via `slide.images`
- 📁 **Multiple Formats**:
  - SVS (Aperio)
  - QPTIFF (including mIF)
  - OME-TIFF (including Z/T stacks)
  - OME.ZARR (CXY, XYC only)
  - MRXS (3DHISTECH, including mIF)
  - iSyntax (Philips)
  - Philips TIFF
  - Hamamatsu NDPI (including Z stacks)
  - Generic TIFF
  - CZI (Zeiss, including Z/T stacks)
  - Ventana (BIF)
  - Olympus (VSI, including Z stacks)

## QuPath Extension

FastSlide is also available as a [QuPath](https://qupath.github.io) extension, so
you can open every FastSlide-supported format directly in QuPath — no coding
required. The easiest way to install it is through QuPath's extension catalog
(QuPath 0.6 or newer):

1. Open QuPath and go to `Extensions` → `Manage extensions`.
2. Click `Manage extension catalogs` → `Add`.
3. Enter the catalog URL and confirm:

   ```
   https://github.com/NKI-AI/qupath-extension-catalog
   ```

4. Back in the extension manager, click the `+` next to **QuPath FastSlide
   extension** to install it. Restart QuPath if prompted.

QuPath will then read whole-slide images via FastSlide and notify you when a new
version of the extension is published. Sources and manual install instructions
live at
[NKI-AI/qupath-extension-fastslide](https://github.com/NKI-AI/qupath-extension-fastslide).

## Quick Start

### Installation

FastSlide can be installed from a prebuilt wheel, or built from source with
either **Meson** (simplest, integrates with `pip`/`uv`) or **Bazel** (hermetic,
used for the official release wheels).

#### Option 1: Prebuilt wheel (recommended)

```bash
uv pip install fastslide
```

#### Option 2: Build from source with Meson

FastSlide is a regular [Meson](https://mesonbuild.com) project. All native
dependencies have [wrap fallbacks](https://mesonbuild.com/Wrap-dependency-system-manual.html),
so a checkout builds standalone with nothing but a C++20 compiler, Meson
(>= 1.3) and Ninja:

```bash
git clone https://github.com/NKI-AI/fastslide
cd fastslide

meson setup builddir
meson compile -C builddir

# Run the C++ test suite.
meson test -C builddir
```

This builds the C++ library, the `fastslidetool` CLI and the C API. See
`meson.options` for the available options (e.g. `-Dbuild_tool=false`,
`-Dbuild_c_api=false`, `-Djpeg_decoder=jpgd`).

**Python package via meson-python** — the Python bindings are wired up through
[meson-python](https://mesonpy.readthedocs.io), so the wheel builds straight
from the source tree with standard Python tooling:

```bash
# Editable (development) install: compiles the native extension with Meson.
uv pip install -e .

# Or build a wheel.
uv build
```

`pip install -e .` / `python -m build` work the same way. The build is
self-contained: all codecs are statically linked into the `_fastslide`
extension, so the resulting wheel has no native runtime dependencies.

#### Option 3: Build from source with Bazel

FastSlide is a [Bazel module](https://bazel.build/external/module). Builds are
driven by [bzlmod](https://bazel.build/external/overview#bzlmod) and we pin a
specific Bazel version through `.bazelversion`; the recommended launcher is
[bazelisk](https://github.com/bazelbuild/bazelisk), which picks up that file
automatically.

```bash
git clone https://github.com/NKI-AI/fastslide
cd fastslide

# Build everything (C++ library, Python bindings, Go bindings, WASM target).
bazelisk build //...

# Run the C++ test suite.
bazelisk test //...
```

##### Building Python wheels with Bazel

Wheels are platform-specific because they bundle the native C++ extension. A
single Bazel target, `//python:fastslide_wheel`, builds one stable-ABI wheel
tagged `cp312-abi3`; because it targets CPython's stable ABI, that wheel runs
on every CPython >= 3.12. Unlike the Meson path, Bazel can also cross-compile
wheels for other platforms.

**Current platform** — build on the host OS/arch without cross-compilation:

```bash
# The cp312-abi3 wheel for the machine you are on.
bazelisk build //python:fastslide_wheel
```

The `.whl` file appears under `bazel-bin/python/`.

**Cross-compilation** — build wheels for other platforms using the Zig-backed
hermetic toolchains (`--config=hermetic` in `.bazelrc`):

```bash
# Example: Linux x86_64 wheel, e.g. from macOS.
bazelisk build --config=hermetic --platforms=//platforms:linux_x86_64 \
  //python:fastslide_wheel
```

Supported platform keys: `linux_x86_64`, `linux_arm64`, `darwin_x86_64`,
`darwin_aarch64`, `windows_x86_64`. When building for the host macOS
architecture from macOS, the native toolchain is used instead of hermetic Zig.

**Batch builds** — `tools/build_wheels.py` drives Bazel for multiple platforms
and copies wheels into `artifacts/wheels/`:

```bash
# All supported platforms.
python tools/build_wheels.py

# Subset, e.g. one platform.
python tools/build_wheels.py --platform linux_x86_64

# Continue after individual failures.
python tools/build_wheels.py --keep-going
```

Run `python tools/build_wheels.py --help` for the full option list.

##### Using FastSlide from another Bazel module

To consume FastSlide from another Bazel module, add it to your `MODULE.bazel`:

```python
bazel_dep(name = "fastslide", version = "0.7.0")
git_override(
    module_name = "fastslide",
    remote = "https://github.com/NKI-AI/fastslide.git",
    commit = "<pin a recent commit SHA>",
)
```

and depend on `@fastslide//:fastslide_lib` (C++) or `@fastslide//python:fastslide` (Python).

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
    ).numpy()
    # region is a numpy array: shape (512, 512, 3), dtype uint8
```

#### Example: Manual Resource Management

```python
import fastslide

# Open a slide without context manager
slide = fastslide.FastSlide.from_file_path('slide.mrxs')

try:
    # Work with the slide
    region = slide.read_region(location=(0, 0), level=0, size=(1024, 1024)).numpy()

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
    region_l0 = slide.read_region(location=location, level=0, size=size).numpy()

    # 4× downsampled (level 2)
    # Convert coordinates to level 2 space
    x_l2, y_l2 = slide.convert_level0_to_level_native(
        location[0], location[1], level=2
    )
    region_l2 = slide.read_region(location=(x_l2, y_l2), level=2, size=size).numpy()

    # Find best level for a specific downsample factor
    best_level = slide.get_best_level_for_downsample(8.0)
    print(f"Best level for 8× downsample: {best_level}")
```

#### Example: Multi-dimensional Reading (Channels, Z focal planes, T time points)

OME-TIFF and CZI files can store more than a single 2D plane: multiple
fluorescence **channels** (C), a **Z** focal stack, and a **T** time series.
FastSlide exposes these as a `C·X·Y·Z·T` hyper-volume. Each `read_region`
selects one `(z, t)` plane and returns all channels of that plane; `z` and `t`
default to `0`, so 2D/brightfield code keeps working unchanged.

```python
import fastslide

with fastslide.FastSlide.from_file_path('stack.ome.tiff') as slide:
    # Inspect the stack extent.
    print(f"Focal planes (Z): {slide.z_count}")
    print(f"Time points (T):  {slide.t_count}")
    print(f"Z spacing: {slide.z_spacing_um} µm")   # None if unknown
    print(f"T interval: {slide.t_interval_s} s")   # None if unknown

    # Or as a single dict.
    print(slide.get_stack_info())
    # {'z_count': 5, 't_count': 7, 'z_spacing_um': 0.5, 't_interval_s': 10.0}

    # Read the 3rd focal plane at the 2nd time point.
    region = slide.read_region(
        location=(0, 0),
        level=0,
        size=(512, 512),
        z=2,   # focal-plane index (0 = first plane)
        t=1,   # time-point index (0 = first time point)
    ).numpy()

    # Fluorescence planes carry independent channels (not RGB). The numpy
    # shape depends on the image's internal layout (see "Pixel layout" below):
    #   - interleaved / CONTIGUOUS -> (height, width, channels)  [HWC]
    #   - band-separate / SEPARATE -> (channels, height, width)  [CHW]
    for t in range(slide.t_count):
        for z in range(slide.z_count):
            img = slide.read_region((0, 0), level=0, size=(512, 512), z=z, t=t)
            # Normalize to a known layout instead of assuming one:
            arr = img.to_interleaved().numpy()  # (512, 512, channels), HWC
```

Per-image stacks are also available on individual images of a multi-image
slide via `slide.images[i].read_region(..., z=, t=)` and
`slide.images[i].get_stack_info()`.

##### Pixel layout (interleaved vs. band-separate)

The byte layout of a returned `Image` is not fixed — it follows the source's
internal organization, exposed via `image.planar_config`:

| `planar_config`           | Memory layout     | `numpy()` shape             |
| ------------------------- | ----------------- | --------------------------- |
| `PlanarConfig.CONTIGUOUS` | interleaved, HWC  | `(height, width, channels)` |
| `PlanarConfig.SEPARATE`   | band-separate,CHW | `(channels, height, width)` |

Brightfield RGB is typically `CONTIGUOUS`; multi-channel fluorescence is
typically `SEPARATE`. Don't assume an axis order — inspect it, or normalize:

```python
img = slide.read_region((0, 0), level=0, size=(512, 512))

img.planar_config   # PlanarConfig.CONTIGUOUS or PlanarConfig.SEPARATE
img.is_interleaved  # True for HWC
img.is_separate     # True for CHW

# Force a specific layout (no-op + zero-copy if already in that layout).
hwc = img.to_interleaved().numpy()  # (H, W, C)
chw = img.to_separate().numpy()     # (C, H, W)
```

#### Example: Multiple Images / Scenes (Zeiss CZI, Olympus VSI)

Some files hold more than one navigable image. Zeiss CZI files expose each
acquisition **scene** as its own image; Olympus VSI files expose a
low-resolution "navigator" alongside one or more high-resolution "region"
images. FastSlide surfaces these through `slide.images`, an indexable sequence
of `SlideImageView`s. The top-level `FastSlide` accessors (`dimensions`,
`level_count`, `read_region`, ...) always forward to the **primary** image, so
single-image code is unaffected.

```python
import fastslide

with fastslide.FastSlide.from_file_path('scan.czi') as slide:
    print(f"Number of images: {slide.num_images}")  # == len(slide.images)
    print(f"Image names: {slide.images.names()}")    # e.g. ['scene 0', 'scene 1']
    print(f"Primary index: {slide.images.primary_index}")

    # Iterate every image (scene) and read each independently.
    for img in slide.images:
        print(f"[{img.index}] {img.name}: {img.dimensions}, "
              f"{img.level_count} levels, Z={img.z_count}, T={img.t_count}")
        region = img.read_region(location=(0, 0), level=0, size=(512, 512)).numpy()

    # Address a specific image by index, with full level + Z/T selection.
    scene1 = slide.images[1]
    tile = scene1.read_region((0, 0), level=0, size=(1024, 1024), z=0, t=0).numpy()

    # The primary image is also directly accessible.
    primary = slide.images.primary
```

Each `SlideImageView` is a full navigator with its own pyramid (`level_count`,
`level_dimensions`, `level_downsamples`), resolution (`mpp`), and Z/T stack
(`z_count`, `t_count`, `get_stack_info()`) — so an individual scene can itself
be a `C·X·Y·Z·T` volume.

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

#### Multi-dimensional Reading (Channels, Z focal planes, T time points)

For OME-TIFF and CZI stacks, `RegionSpec::plane` selects the focal plane (Z)
and time point (T); both default to the first plane, so 2D reads are
unaffected. Query the stack extent with `GetStackInfo()`.

```cpp
// Stack extent and physical spacing.
const fastslide::StackInfo stack = reader->GetStackInfo();
// stack.z_count, stack.t_count          : number of selectable planes (>= 1)
// stack.z_spacing_um, stack.t_interval_s : std::optional<double> (physical step)

// Read the 3rd focal plane at the 2nd time point.
fastslide::RegionSpec spec{
    .top_left = {0, 0},
    .size = {512, 512},
    .level = 0,
    .plane = {.z = 2, .t = 1},
};
auto result = reader->ReadRegion(spec);  // aifocore::Result<Image>
const fastslide::Image& image = result.value();  // check result.ok() first

// Channel memory layout follows the source and is reported per Image; do not
// assume interleaved vs. band-separate. Normalize when you need a fixed order.
const fastslide::PlanarConfig layout = image.GetPlanarConfig();
const bool interleaved = image.IsInterleaved();  // kContiguous (HWC)
const bool separate    = image.IsSeparate();     // kSeparate   (CHW)
auto hwc = image.ToInterleaved();  // zero-copy/no-op if already interleaved
auto chw = image.ToPlanar();       // zero-copy/no-op if already separate
```

#### Multiple Images / Scenes

`GetImageCount()`, `GetImageNames()` and `GetImage(index)` expose every
navigable image (Zeiss CZI scenes, Olympus VSI navigator/region images). The
reader's own `ReadRegion`/`GetStackInfo` forward to `GetPrimaryImageIndex()`.

```cpp
const int count = reader->GetImageCount();
const std::vector<std::string> names = reader->GetImageNames();

for (int i = 0; i < count; ++i) {
  auto image_or = reader->GetImage(i);  // aifocore::Result<const SlideImage*>
  if (!image_or.ok()) {
    continue;
  }
  const fastslide::SlideImage& image = *image_or.value();

  // Each image has its own name, pyramid, channels and Z/T stack.
  const std::string name = image.GetName();
  const fastslide::StackInfo stack = image.GetStackInfo();

  fastslide::RegionSpec spec{
      .top_left = {0, 0},
      .size = {512, 512},
      .level = 0,
      .plane = {.z = 0, .t = 0},
  };
  auto region = image.ReadRegion(spec);  // aifocore::Result<Image>
}
```

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
region = slide.read_region((100, 200), level=2, size=(512, 512)).numpy()

# Convert coordinates between levels if needed
x0, y0 = slide.convert_level_native_to_level0(100, 200, level=2)
# Returns: (400, 800) - the level-0 equivalent
```

## Documentation

📖 **Complete documentation:** https://docs.aifo.dev/fastslide/

## Contributing

We welcome contributions. Please open an [issue](https://github.com/NKI-AI/fastslide/issues)
to discuss what you would like to change, or jump straight into a pull request.

## Third-Party Components

FastSlide incorporates the following third-party software into its source:

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

- **yxml**: from [https://dev.yorhel.nl/yxml](https://dev.yorhel.nl/yxml)
  - Licensed under: MIT License
  - Used for: Parsing of XML headers

- **tifffile**: from [cgohlke/tifffile/](https://github.com/cgohlke/tifffile/) by Christoph Gohlke
  - Licensed under: BSD-3-Clause
  - Used for: Test data files

- **jpeg-compressor**: from [richgel999/jpeg-compressor](https://github.com/richgel999/jpeg-compressor) by richgel999
  - Licensed under: Public domain
  - Used for: Alternative JPEG decompression, required in WASM builds.

- **thread-pool**: from [bshoshany/thread-pool](https://github.com/bshoshany/thread-pool) by Barak Shoshany
  - Licensed under: MIT License
  - Used for: Creating thread pool for decoding, etc.

- **libisyntax**: from [amspath/libisyntax](https://github.com/pvalkema/libisyntax) by Pieter Valkema
  - Licensed under: BSD-2 License
  - Used for: iSyntax decoding
  - Modifications: Library has been stripped to the minimal requirements.

- **jxrlib** from [4creators/jxrlib](https://github.com/4creators/jxrlib.git) by Microsoft
  - Licensed under: BSD-2-Clause License
  - Used for: Decoding of JPEG XR tiles in the Zeiss CZI reader
  - Modifications: Library has been modified to compile with Bazel and unused files removed.

Several other libraries are used, but these are dynamically (or statically where appropriate) linked.

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

See [LICENSE](LICENSE) for full details.
