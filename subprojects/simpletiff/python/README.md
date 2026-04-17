# SimpleTIFF Python Bindings

High-performance Python bindings for the SimpleTIFF library using pybind11.

## Features

- **Pythonic API**: Clean, intuitive interface following Python best practices
- **NumPy Integration**: Returns NumPy arrays directly with appropriate dtypes
- **Context Manager Support**: Automatic resource cleanup with `with` statement
- **Zero-Copy Operations**: Memory-mapped file access for maximum performance
- **Thread-Safe**: Can be used from multiple threads
- **Type Hints**: Full type annotations for IDE support and type checking

## Installation

### Building from Source

```bash
cd aifo/simpletiff
meson setup builddir -Dbuild_python_bindings=true
meson install -C builddir
```

## Usage

### Basic Example

```python
import simpletiff

# Open a TIFF file using context manager
with simpletiff.SimpleTiffReader.from_file_path("image.tiff") as reader:
    # Access file metadata
    print(f"Pages: {reader.num_pages}")
    print(f"BigTIFF: {reader.is_bigtiff}")

    # Access first page
    page = reader[0]
    print(f"Size: {page.width}x{page.height}")
    print(f"Channels: {page.samples_per_pixel}")
    print(f"Bit depth: {page.bits_per_sample}")

    # Check compression type (returns Compression enum)
    print(f"Compression: {page.compression.name}")
    if page.compression == simpletiff.Compression.JPEG:
        print("JPEG compressed!")

    # Read full page as NumPy array
    data = page.read()
    print(f"Shape: {data.shape}, dtype: {data.dtype}")

    # Read a region
    region = page.read_region((0, 0), (256, 256))
    print(f"Region shape: {region.shape}")
```

### Reading Regions

```python
with simpletiff.SimpleTiffReader.from_file_path("image.tiff") as reader:
    page = reader[0]

    # Read region: location=(x, y), size=(width, height)
    region = page.read_region(location=(100, 200), size=(512, 512))
```

### Iterating Over Pages

```python
with simpletiff.SimpleTiffReader.from_file_path("multi_page.tiff") as reader:
    # Iterate over all pages
    for i, page in enumerate(reader):
        print(f"Page {i}: {page.width}x{page.height}")

    # Access by index
    first_page = reader[0]
    last_page = reader[-1]  # Negative indexing supported
```

### Working with Tiled Images

```python
with simpletiff.SimpleTiffReader.from_file_path("tiled.tiff") as reader:
    page = reader[0]

    if page.is_tiled:
        print(f"Tile size: {page.tile_width}x{page.tile_height}")
        print(f"Grid: {page.num_tiles_x}x{page.num_tiles_y}")

        # Read individual tile
        tile = page.read_tile(0)
        print(f"Tile shape: {tile.shape}")
```

## API Reference

### Compression

Enum for TIFF compression types.

**Values:**

- `NONE = 1`: Uncompressed
- `LZW = 5`: LZW compression
- `JPEG = 7`: JPEG compression
- `DEFLATE = 8`: Deflate/ZIP compression
- `ZSTD = 50000`: ZSTD compression
- `UNKNOWN = 0`: Unknown/unsupported compression

**Methods:**

- `from_code(code: int) -> Compression`: Convert numeric code to enum

**Usage:**

```python
# Get compression as enum with name
print(page.compression.name)  # "JPEG"
print(page.compression.value)  # 7

# Compare with enum values
if page.compression == simpletiff.Compression.JPEG:
    print("JPEG compressed")

# Get raw numeric code if needed
print(page.compression_code)  # 7
```

### SimpleTiffReader

Main class for reading TIFF files.

**Factory Methods:**

- `from_file_path(file_path: str) -> SimpleTiffReader`: Create reader from file path

**Properties:**

- `num_pages: int`: Number of pages in the file
- `is_bigtiff: bool`: Whether this is a BigTIFF file
- `file_size: int`: File size in bytes

**Methods:**

- `get_page(index: int) -> SimpleTiffPage`: Get page by index
- `__getitem__(index: int) -> SimpleTiffPage`: Index access (supports negative indexing)
- `__len__() -> int`: Number of pages
- `__iter__()`: Iterate over pages

**Context Manager:**

- `__enter__() / __exit__()`: Automatic resource cleanup

### SimpleTiffPage

Represents a single page in a TIFF file.

**Properties:**

- `width: int`: Page width in pixels
- `height: int`: Page height in pixels
- `samples_per_pixel: int`: Number of channels
- `bits_per_sample: int`: Bits per sample (8, 16, 32)
- `photometric: int`: Photometric interpretation
- `compression: Compression`: Compression type as enum
- `compression_code: int`: Compression type as numeric code
- `storage_type: str`: "tiled", "striped", or "single_jpeg"
- `is_tiled: bool`: Whether page uses tiled storage

**Tiled Properties** (only for tiled pages):

- `tile_width: int`: Tile width in pixels
- `tile_height: int`: Tile height in pixels
- `num_tiles_x: int`: Number of tiles horizontally
- `num_tiles_y: int`: Number of tiles vertically

**Methods:**

- `read() -> np.ndarray`: Read full page
- `read_region(location: Tuple[int, int], size: Tuple[int, int]) -> np.ndarray`: Read region
- `read_tile(tile_index: int) -> np.ndarray`: Read single tile (tiled pages only)

## Data Types

The returned NumPy arrays have dtypes based on `bits_per_sample`:

- 8 bits → `np.uint8`
- 16 bits → `np.uint16`
- 32 bits → `np.uint32`

## Supported Formats

SimpleTIFF supports:

- **Compression**: JPEG (7), LZW (5), ZSTD (50000), Uncompressed (1)
- **Storage**: Tiled, Striped, Single JPEG
- **Color spaces**: RGB, Grayscale, YCbCr
- **Bit depths**: 8, 16, 32 bits per sample

## Performance Tips

1. **Use context manager**: Ensures proper cleanup of file descriptors
2. **Read regions when possible**: Avoid loading entire large images
3. **Thread safety**: Reader is thread-safe for concurrent page reads
4. **Memory mapping**: SimpleTIFF uses mmap for zero-copy reads

## License

Copyright 2025 SimpleTIFF Authors. See LICENSE file for details.
