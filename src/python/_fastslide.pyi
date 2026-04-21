"""
FastSlide: High-performance, thread-safe digital pathology slide reader
"""

from collections.abc import Mapping, Sequence
import enum
from typing import TypeAlias

class ImageFormat(enum.Enum):
    GRAY = 1

    RGB = 3

    RGBA = 4

    SPECTRAL = 0

GRAY: ImageFormat = ImageFormat.GRAY

RGB: ImageFormat = ImageFormat.RGB

RGBA: ImageFormat = ImageFormat.RGBA

SPECTRAL: ImageFormat = ImageFormat.SPECTRAL

class PlanarConfig(enum.Enum):
    CONTIGUOUS = 1

    SEPARATE = 2

CONTIGUOUS: PlanarConfig = PlanarConfig.CONTIGUOUS

SEPARATE: PlanarConfig = PlanarConfig.SEPARATE

class Image:
    @property
    def width(self) -> int: ...
    @property
    def height(self) -> int: ...
    @property
    def channels(self) -> int: ...
    @property
    def format(self) -> ImageFormat: ...
    @property
    def dtype(self) -> str: ...
    @property
    def planar_config(self) -> PlanarConfig: ...
    def numpy(self) -> object:
        """Get a numpy array view of the image data (zero-copy)"""

class CacheInspectionStats:
    @property
    def capacity_bytes(self) -> int: ...
    @capacity_bytes.setter
    def capacity_bytes(self, arg: int, /) -> None: ...
    @property
    def size(self) -> int: ...
    @size.setter
    def size(self, arg: int, /) -> None: ...
    @property
    def hits(self) -> int: ...
    @hits.setter
    def hits(self, arg: int, /) -> None: ...
    @property
    def misses(self) -> int: ...
    @misses.setter
    def misses(self, arg: int, /) -> None: ...
    @property
    def hit_ratio(self) -> float: ...
    @hit_ratio.setter
    def hit_ratio(self, arg: float, /) -> None: ...
    @property
    def memory_usage_mb(self) -> float: ...
    @memory_usage_mb.setter
    def memory_usage_mb(self, arg: float, /) -> None: ...
    @property
    def recent_keys(self) -> list[str]: ...
    @recent_keys.setter
    def recent_keys(self, arg: Sequence[str], /) -> None: ...
    @property
    def key_frequencies(self) -> dict[str, int]: ...
    @key_frequencies.setter
    def key_frequencies(self, arg: Mapping[str, int], /) -> None: ...

class TileCache:
    def get_stats(self) -> RuntimeCacheStats:
        """Get cache statistics"""

    def get_capacity_bytes(self) -> int:
        """Get cache capacity in bytes"""

    def get_size(self) -> int:
        """Get cache size (number of tiles)"""

    def clear(self) -> None:
        """Clear cache"""

class CacheManager:
    @staticmethod
    def create(capacity_bytes: int = 1073741824) -> CacheManager:
        """Create cache manager with the given byte capacity"""

    def clear(self) -> None:
        """Clear all cached tiles"""

    def get_basic_stats(self) -> RuntimeCacheStats:
        """Get basic cache statistics"""

    def get_detailed_stats(self) -> CacheInspectionStats:
        """Get detailed cache statistics"""

    def resize(self, new_capacity_bytes: int) -> None:
        """Resize cache capacity (in bytes)"""

class RuntimeCacheStats:
    @property
    def capacity_bytes(self) -> int: ...
    @capacity_bytes.setter
    def capacity_bytes(self, arg: int, /) -> None: ...
    @property
    def size(self) -> int: ...
    @size.setter
    def size(self, arg: int, /) -> None: ...
    @property
    def hits(self) -> int: ...
    @hits.setter
    def hits(self, arg: int, /) -> None: ...
    @property
    def misses(self) -> int: ...
    @misses.setter
    def misses(self, arg: int, /) -> None: ...
    @property
    def hit_ratio(self) -> float: ...
    @hit_ratio.setter
    def hit_ratio(self, arg: float, /) -> None: ...
    @property
    def memory_usage_bytes(self) -> int: ...
    @memory_usage_bytes.setter
    def memory_usage_bytes(self, arg: int, /) -> None: ...

CacheStats: TypeAlias = RuntimeCacheStats

class AssociatedImages:
    def __getitem__(self, name: str) -> Image:
        """Get associated image by name (lazy loaded)"""

    def __contains__(self, name: str) -> bool:
        """Check if associated image exists"""

    def keys(self) -> list[str]:
        """Get list of associated image names"""

    def get_dimensions(self, name: str) -> tuple:
        """Get dimensions of associated image"""

    def get_cache_size(self) -> int:
        """Get number of cached images in memory"""

    def clear_cache(self) -> None:
        """Clear the associated image cache"""

class AssociatedData:
    def __getitem__(self, name: str) -> object:
        """
        Get associated data by name (lazy loaded)

        Returns:
            str for XML data
            bytes for binary data
        """

    def __contains__(self, name: str) -> bool:
        """Check if associated data exists"""

    def keys(self) -> list[str]:
        """Get list of associated data names (non-image)"""

    def get_type(self, name: str) -> str:
        """Get data type without loading"""

    def get_cache_size(self) -> int:
        """Get number of cached data items in memory"""

    def clear_cache(self) -> None:
        """Clear the associated data cache"""

class FastSlide:
    @staticmethod
    def from_file_path(file_path: object) -> FastSlide:
        """Create FastSlide from file path (accepts str or pathlib.Path)"""

    @staticmethod
    def from_uri(uri: str) -> FastSlide:
        """Create FastSlide from URI (future)"""

    def read_region(self, location: tuple[int, int], level: int, size: tuple[int, int]) -> Image:
        """
        Read a region from the slide using level-native coordinates

        Args:
            location: Tuple (x, y) coordinates in level-native space
            level: Pyramid level (0=highest resolution)
            size: Tuple (width, height) of region

        Returns:
            fastslide.Image: Image object. Call .numpy() to get array view.

        Note: Coordinates are in level-native space. To convert from level-0
        coordinates, use convert_level0_to_level_native().
        """

    def convert_level0_to_level_native(self, x: int, y: int, level: int) -> tuple:
        """
        Convert level-0 coordinates to level-native coordinates

        Args:
            x: X coordinate in level-0 space
            y: Y coordinate in level-0 space
            level: Target level

        Returns:
            tuple: (level_native_x, level_native_y)
        """

    def convert_level_native_to_level0(self, x: int, y: int, level: int) -> tuple:
        """
        Convert level-native coordinates to level-0 coordinates

        Args:
            x: X coordinate in level-native space
            y: Y coordinate in level-native space
            level: Source level

        Returns:
            tuple: (level_0_x, level_0_y)
        """

    @property
    def dimensions(self) -> tuple:
        """Slide dimensions (width, height) at level 0"""

    @property
    def level_dimensions(self) -> tuple:
        """Tuple of (width, height) for each level"""

    @property
    def level_downsamples(self) -> tuple:
        """Tuple of downsample factors for each level"""

    @property
    def level_count(self) -> int:
        """Number of levels in the slide"""

    @property
    def properties(self) -> dict:
        """Dictionary of slide properties and metadata"""

    @property
    def mpp(self) -> tuple:
        """Microns per pixel as (mpp_x, mpp_y) tuple"""

    @property
    def bounds(self) -> tuple:
        """
        Bounding box of non-empty region

        Returns tuple with coordinates and size:
          (x, y) tuple: Top-left coordinates (level 0)
          (width, height) tuple: Bounding box size
        """

    @property
    def format(self) -> str:
        """File format name"""

    @property
    def dtype(self) -> str:
        """Pixel data type (e.g. 'uint8', 'uint16')"""

    @property
    def quickhash(self) -> str:
        """SHA-256 quickhash (unique identifier, OpenSlide-compatible)"""

    @property
    def channel_metadata(self) -> list:
        """List of channel metadata dictionaries"""

    @property
    def associated_images(self) -> AssociatedImages:
        """Dictionary-like access to associated images (lazy loaded)"""

    @property
    def associated_data(self) -> AssociatedData:
        """
        Dictionary-like access to associated data: XML, binary (lazy loaded, MRXS only)
        """

    @property
    def closed(self) -> bool:
        """True if the slide reader is closed"""

    @property
    def source_path(self) -> str:
        """Source file path"""

    def get_best_level_for_downsample(self, downsample: float) -> int:
        """Get the best level for a given downsample factor"""

    def set_cache(self, cache: object) -> None:
        """Set cache (accepts TileCache, CacheManager, or None to disable)."""

    def get_cache(self) -> TileCache:
        """Get current cache"""

    @property
    def cache_enabled(self) -> bool:
        """True if caching is enabled"""

    def close(self) -> None:
        """Close the slide reader and release resources"""

    def set_visible_channels(self, channels: object) -> None:
        """
        Set which channels are visible during read operations

        Args:
            channels: Channel index (int), list/tuple of channel indices,
                      or None to show all channels

        Examples:
            slide.set_visible_channels(0)        # Show only channel 0
            slide.set_visible_channels([0, 2])   # Show channels 0 and 2
            slide.set_visible_channels(None)     # Show all channels

        Note: Channel indices are 0-based. Invalid indices will be ignored
        during read operations.
        """

    def get_visible_channels(self) -> list[int]:
        """
        Get currently visible channel indices

        Returns:
            list: List of visible channel indices (empty = all visible)
        """

    def show_all_channels(self) -> None:
        """
        Reset to show all channels

        Equivalent to set_visible_channels(None)
        """

    def __enter__(self) -> FastSlide: ...
    def __exit__(self, arg0: object, arg1: object, arg2: object, /) -> bool: ...

def get_supported_extensions() -> list[str]:
    """Get list of supported file extensions"""

def is_supported(filename: str) -> bool:
    """Check if file format is supported"""
