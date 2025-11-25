#!/usr/bin/env python3
"""Example script demonstrating SimpleTIFF Python API.

This script shows how to use the SimpleTIFF Python bindings to:
- Open TIFF files
- Access page metadata
- Read full pages and regions
- Iterate over pages
- Read individual tiles
"""

import sys

import numpy as np

import simpletiff


def main():
    """Main example function."""
    if len(sys.argv) < 2:
        print("Usage: python example_simpletiff.py <tiff_file>")
        sys.exit(1)

    tiff_path = sys.argv[1]

    print(f"Opening TIFF file: {tiff_path}")
    print()

    # Context manager usage (recommended)
    with simpletiff.SimpleTiffReader.from_file_path(tiff_path) as reader:
        # Access file metadata
        print("=== File Metadata ===")
        print(f"Number of pages: {reader.num_pages}")
        print(f"Is BigTIFF: {reader.is_bigtiff}")
        print(f"File size: {reader.file_size:,} bytes")
        print()

        # Access first page
        print("=== First Page (using indexing) ===")
        page = reader[0]
        print(f"Width: {page.width}")
        print(f"Height: {page.height}")
        print(f"Samples per pixel: {page.samples_per_pixel}")
        print(f"Bits per sample: {page.bits_per_sample}")
        print(f"Compression: {page.compression.name} (code: {page.compression_code})")
        print(f"Storage type: {page.storage_type}")
        print(f"Is tiled: {page.is_tiled}")

        # Demonstrate compression enum usage
        if page.compression == simpletiff.Compression.JPEG:
            print("  → JPEG compressed image")
        elif page.compression == simpletiff.Compression.ZSTD:
            print("  → ZSTD compressed image")
        elif page.compression == simpletiff.Compression.LZW:
            print("  → LZW compressed image")
        elif page.compression == simpletiff.Compression.NONE:
            print("  → Uncompressed image")

        if page.is_tiled:
            print(f"Tile width: {page.tile_width}")
            print(f"Tile height: {page.tile_height}")
            print(f"Number of tiles (X x Y): {page.num_tiles_x} x {page.num_tiles_y}")
        print()

        # Read full page
        print("=== Reading Full Page ===")
        data = page.read()
        print(f"Data shape: {data.shape}")
        print(f"Data dtype: {data.dtype}")
        print(f"Min value: {data.min()}")
        print(f"Max value: {data.max()}")
        print(f"Mean value: {data.mean():.2f}")
        print()

        # Read a region
        print("=== Reading Region ===")
        # Read a 256x256 region from top-left corner
        region_size = min(256, page.width), min(256, page.height)
        region_data = page.read_region((0, 0), region_size)
        print(f"Region shape: {region_data.shape}")
        print(f"Region dtype: {region_data.dtype}")
        print()

        # Read a region from center
        center_x = page.width // 2 - 128
        center_y = page.height // 2 - 128
        if center_x >= 0 and center_y >= 0:
            center_region = page.read_region((center_x, center_y), (256, 256))
            print(f"Center region shape: {center_region.shape}")
            print()

        # Read individual tile (if tiled)
        if page.is_tiled and page.num_tiles_x > 0 and page.num_tiles_y > 0:
            print("=== Reading Individual Tile ===")
            tile_data = page.read_tile(0)
            print(f"Tile 0 shape: {tile_data.shape}")
            print(f"Tile 0 dtype: {tile_data.dtype}")
            print()

        # Iterate over all pages
        print("=== Iterating Over All Pages ===")
        for i, page in enumerate(reader):
            print(
                f"Page {i}: {page.width}x{page.height}, "
                f"{page.samples_per_pixel} channels, "
                f"{page.bits_per_sample} bits/sample, "
                f"storage: {page.storage_type}"
            )
        print()

        # Access pages using negative indexing
        print("=== Negative Indexing ===")
        last_page = reader[-1]
        print(f"Last page: {last_page.width}x{last_page.height}, {last_page.samples_per_pixel} channels")
        print()

        # Use get_page method
        print("=== Using get_page Method ===")
        page_0 = reader.get_page(0)
        print(f"Page 0 via get_page(): {page_0.width}x{page_0.height}")
        print()

    print("Successfully closed TIFF file (context manager)")


if __name__ == "__main__":
    main()
