"""Type stubs for _simpletiff C++ extension module."""

from typing import Tuple

import numpy as np
import numpy.typing as npt

from simpletiff import Compression

class SimpleTiffPage:
    """Represents a single page in a TIFF file.

    Provides access to page metadata and methods for reading image data.
    """

    @property
    def width(self) -> int:
        """Page width in pixels."""
        ...

    @property
    def ifd_index(self) -> int:
        """IFD index (parser enumeration order)."""
        ...

    @property
    def ifd_offset(self) -> int:
        """IFD offset in the TIFF file."""
        ...

    @property
    def parent_page_index(self) -> int | None:
        """Parent page index if this page is a SubIFD, else None."""
        ...

    @property
    def sub_page_indices(self) -> list[int]:
        """Indices of SubIFD child pages of this page."""
        ...

    @property
    def num_sub_pages(self) -> int:
        """Number of SubIFD child pages of this page."""
        ...

    @property
    def sub_pages(self) -> list["SimpleTiffPage"]:
        """SubIFD child pages of this page."""
        ...

    @property
    def height(self) -> int:
        """Page height in pixels."""
        ...

    @property
    def new_subfile_type(self) -> int:
        """NewSubfileType TIFF tag (254) as an integer bitmask."""
        ...

    @property
    def is_reduced_resolution(self) -> bool:
        """True if page is marked reduced-resolution (NewSubfileType bit 0)."""
        ...

    @property
    def samples_per_pixel(self) -> int:
        """Number of samples per pixel (channels)."""
        ...

    @property
    def bits_per_sample(self) -> int:
        """Number of bits per sample."""
        ...

    @property
    def photometric(self) -> int:
        """Photometric interpretation value."""
        ...

    @property
    def compression(self) -> Compression:
        """Compression type as Compression enum."""
        ...

    @property
    def compression_code(self) -> int:
        """Compression type as numeric code."""
        ...

    @property
    def storage_type(self) -> str:
        """Storage type: 'tiled', 'striped', or 'single_jpeg'."""
        ...

    @property
    def is_tiled(self) -> bool:
        """True if page uses tiled storage."""
        ...

    @property
    def tile_width(self) -> int:
        """Tile width in pixels (only for tiled pages).

        Raises:
            RuntimeError: If page is not tiled.
        """
        ...

    @property
    def tile_height(self) -> int:
        """Tile height in pixels (only for tiled pages).

        Raises:
            RuntimeError: If page is not tiled.
        """
        ...

    @property
    def num_tiles_x(self) -> int:
        """Number of tiles horizontally (only for tiled pages).

        Raises:
            RuntimeError: If page is not tiled.
        """
        ...

    @property
    def num_tiles_y(self) -> int:
        """Number of tiles vertically (only for tiled pages).

        Raises:
            RuntimeError: If page is not tiled.
        """
        ...

    def read(self) -> npt.NDArray[np.uint8] | npt.NDArray[np.uint16] | npt.NDArray[np.uint32]:
        """Read the full page as a NumPy array.

        Returns:
            Image data with shape (height, width, channels).
            dtype depends on bits_per_sample:
            - 8 bits: uint8
            - 16 bits: uint16
            - 32 bits: uint32

        Raises:
            RuntimeError: If reading fails.
        """
        ...

    def read_region(
        self, location: Tuple[int, int], size: Tuple[int, int]
    ) -> npt.NDArray[np.uint8] | npt.NDArray[np.uint16] | npt.NDArray[np.uint32]:
        """Read a region from the page.

        Args:
            location: Tuple (x, y) - top-left corner of the region.
            size: Tuple (width, height) - size of the region.

        Returns:
            Image data with shape (height, width, channels).

        Raises:
            RuntimeError: If reading fails.
        """
        ...

    def read_tile(self, tile_index: int) -> npt.NDArray[np.uint8] | npt.NDArray[np.uint16] | npt.NDArray[np.uint32]:
        """Read a single tile by index (tiled pages only).

        Args:
            tile_index: Linear tile index (row-major order).

        Returns:
            Tile data with shape (tile_height, tile_width, channels).

        Raises:
            RuntimeError: If page is not tiled or reading fails.
        """
        ...

class SimpleTiffReader:
    """High-performance TIFF file reader.

    Provides access to TIFF file metadata and pages.
    Supports context manager protocol for automatic resource cleanup.

    Example:
        >>> with SimpleTiffReader.from_file_path("image.tiff") as reader:
        ...     print(f"Pages: {reader.num_pages}")
        ...     page = reader[0]
        ...     data = page.read()
    """

    @staticmethod
    def from_file_path(file_path: str) -> SimpleTiffReader:
        """Create a SimpleTiffReader from a file path.

        Args:
            file_path: Path to the TIFF file.

        Returns:
            A new SimpleTiffReader instance.

        Raises:
            RuntimeError: If the file cannot be opened or is not a valid TIFF.
        """
        ...

    @property
    def num_pages(self) -> int:
        """Number of pages in the TIFF file."""
        ...

    @property
    def num_ifds(self) -> int:
        """Number of IFDs (directories) parsed, including SubIFDs."""
        ...

    @property
    def num_root_pages(self) -> int:
        """Number of root pages (not SubIFDs)."""
        ...

    @property
    def is_bigtiff(self) -> bool:
        """True if this is a BigTIFF file."""
        ...

    @property
    def file_size(self) -> int:
        """Size of the TIFF file in bytes."""
        ...

    @property
    def root_page_indices(self) -> list[int]:
        """Indices of root pages (not SubIFDs)."""
        ...

    @property
    def root_pages(self) -> list[SimpleTiffPage]:
        """Root pages (not SubIFDs)."""
        ...

    def get_page(self, index: int) -> SimpleTiffPage:
        """Get a page by index (0-based).

        Args:
            index: Page index (0-based).

        Returns:
            SimpleTiffPage object for the specified page.

        Raises:
            IndexError: If index is out of range.
        """
        ...

    def __getitem__(self, index: int) -> SimpleTiffPage:
        """Get a page by index (supports negative indexing).

        Args:
            index: Page index (supports negative indexing).

        Returns:
            SimpleTiffPage object for the specified page.

        Raises:
            IndexError: If index is out of range.
        """
        ...

    def __len__(self) -> int:
        """Get number of pages."""
        ...

    def __iter__(self):
        """Iterate over pages."""
        ...

    def __enter__(self) -> SimpleTiffReader:
        """Context manager entry."""
        ...

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        """Context manager exit."""
        ...

__version__: str
