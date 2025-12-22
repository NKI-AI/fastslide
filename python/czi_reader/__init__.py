"""Zeiss CZI reader (header + multi-level tiles + decoding)."""

from .reader import CziReader
from .structs import (
    Bounds,
    CziHeader,
    LevelInfo,
    Subblock,
    Tile,
)

__all__ = [
    "Bounds",
    "CziHeader",
    "CziReader",
    "LevelInfo",
    "Subblock",
    "Tile",
]
