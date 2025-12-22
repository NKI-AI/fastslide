"""Typed structures for CZI parsing and tile access."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path


class Compression(IntEnum):
    """CZI compression modes (subset)."""

    NONE = 0
    JPEG = 1
    LZW = 2
    JXR = 4
    ZSTD0 = 5
    ZSTD1 = 6


class PixelType(IntEnum):
    """CZI pixel types (subset)."""

    GRAY8 = 0
    GRAY16 = 1
    GRAY32FLOAT = 2
    BGR24 = 3
    BGR48 = 4
    BGRA32 = 9


@dataclass(frozen=True, slots=True)
class CziHeader:
    path: Path
    major: int
    minor: int
    primary_file_guid: bytes
    file_guid: bytes
    subblk_dir_pos: int
    meta_pos: int
    att_dir_pos: int


@dataclass(frozen=True, slots=True)
class Subblock:
    """Directory entry for one subblock (tile)."""

    index: int
    file_pos: int
    pixel_type: int
    compression: int
    pyramid_type: int
    ndimensions: int

    # Parsed dimensions / derived fields (OpenSlide-like)
    x: int
    y: int
    w: int
    h: int
    scene: int
    channel: int
    z_index: int
    downsample_i: int


@dataclass(frozen=True, slots=True)
class Tile:
    """Tile reference suitable for reading/decoding."""

    path: Path
    subblock: Subblock


@dataclass(frozen=True, slots=True)
class LevelInfo:
    downsample: int
    tile_count: int
    width: int
    height: int


@dataclass(frozen=True, slots=True)
class Bounds:
    """Integer bounds in level-0 pixel coordinates."""

    x: int
    y: int
    width: int
    height: int
