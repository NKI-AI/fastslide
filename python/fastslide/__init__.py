"""FastSlide: High-performance, thread-safe digital pathology slide reader.

This package provides high-performance reading of whole slide images (WSI) commonly
used in digital pathology. It supports multiple formats including Aperio SVS,
3DHISTECH MRXS, and QPTIFF.

Example:
    >>> import fastslide
    >>> slide = fastslide.FastSlide.from_file_path("slide.mrxs")
    >>> region = slide.read_region(location=(0, 0), level=0, size=(1024, 1024))
    >>> print(slide.dimensions)
"""

__version__ = "0.1.0"

from fastslide._fastslide import *

__all__ = [
    "FastSlide",
    "CacheManager",
    "TileCache",
    "RuntimeGlobalCacheManager",
    "CacheInspectionStats",
    "CacheStats",
    "RuntimeCacheStats",
    "AssociatedImages",
    "AssociatedData",
]
