# Copyright 2025 Jonas Teuwen. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Unit tests for ``fastslide.xyz_pyramid.XYZPyramid``.

These tests use a lightweight fake slide instead of a real WSI file so the
pyramid behaviour (zoom-to-native-level mapping, resolutions, tile grids, edge
tiles, channel ordering) can be checked deterministically without test fixtures
on disk.
"""

from __future__ import annotations

import math
from typing import Protocol

import numpy as np
import pytest

from fastslide.xyz_pyramid import XYZPyramid


class _RegionLike(Protocol):
    """Structural type shared by the fake regions used in these tests."""

    def to_interleaved(self) -> "_RegionLike": ...

    def numpy(self) -> np.ndarray: ...


class _FakeRegion:
    """Stand-in for ``fastslide.Image`` exposing ``numpy()``."""

    def __init__(self, array: np.ndarray) -> None:
        self._array = array

    @property
    def channels(self) -> int:
        return self._array.shape[2] if self._array.ndim == 3 else 1

    def to_interleaved(self) -> "_FakeRegion":
        return self

    def numpy(self) -> np.ndarray:
        return self._array


class _FakeSlide:
    """Minimal slide implementing the ``XYZPyramid`` slide protocol.

    Each pyramid level is a downsample of level 0 as given by ``downsamples``
    (ascending, starting at 1.0). ``read_region`` returns a synthetic array of
    exactly the requested size and records the most recent call for assertions.
    """

    def __init__(
        self,
        width: int,
        height: int,
        downsamples: tuple[float, ...] = (1.0,),
        channels: int = 3,
        fmt: str = "fake",
        mpp: tuple[float, float] | None = (0.25, 0.25),
    ) -> None:
        self._width = width
        self._height = height
        self._downsamples = downsamples
        self._channels = channels
        self.format = fmt
        self._mpp = mpp
        self.last_call: dict[str, object] | None = None

    @property
    def dimensions(self) -> tuple[int, int]:
        return (self._width, self._height)

    @property
    def level_count(self) -> int:
        return len(self._downsamples)

    @property
    def level_dimensions(self) -> tuple[tuple[int, int], ...]:
        return tuple(
            (max(1, math.floor(self._width / d)), max(1, math.floor(self._height / d))) for d in self._downsamples
        )

    @property
    def level_downsamples(self) -> tuple[float, ...]:
        return self._downsamples

    @property
    def mpp(self) -> tuple[float, float] | None:
        return self._mpp

    def read_region(self, location: tuple[int, int], level: int, size: tuple[int, int]) -> _RegionLike:
        self.last_call = {"location": location, "level": level, "size": size}
        w, h = size
        array = np.zeros((h, w, self._channels), dtype=np.uint8)
        return _FakeRegion(array)


def test_zoom_levels_match_native_levels() -> None:
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0, 16.0))
    pyramid = XYZPyramid(slide, tile_size=256)
    assert pyramid.max_zoom == 2
    assert pyramid.min_zoom == 0
    assert pyramid.num_zoom_levels == 3


def test_resolutions_are_reversed_downsamples() -> None:
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0, 16.0))
    pyramid = XYZPyramid(slide, tile_size=256)
    # z=0 is the coarsest native level; resolutions descend to 1.0 at full res.
    assert pyramid.resolutions == [16.0, 4.0, 1.0]


def test_level_for_zoom_mapping() -> None:
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0, 16.0))
    pyramid = XYZPyramid(slide, tile_size=256)
    assert pyramid.level_for_zoom(0) == 2  # coarsest
    assert pyramid.level_for_zoom(2) == 0  # full resolution
    assert pyramid.resolution_for_zoom(0) == 16.0
    assert pyramid.resolution_for_zoom(2) == 1.0


def test_zoom_dimensions() -> None:
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0, 16.0))
    pyramid = XYZPyramid(slide, tile_size=256)
    assert pyramid.zoom_dimensions(2) == (1024, 1024)  # native level 0
    assert pyramid.zoom_dimensions(0) == (64, 64)  # native level 2


def test_tile_grid_size() -> None:
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0, 16.0))
    pyramid = XYZPyramid(slide, tile_size=256)
    assert pyramid.tile_grid_size(2) == (4, 4)  # 1024/256
    assert pyramid.tile_grid_size(1) == (1, 1)  # 256/256
    assert pyramid.tile_grid_size(0) == (1, 1)  # 64/256


def test_tile_grid_size_out_of_range() -> None:
    pyramid = XYZPyramid(_FakeSlide(1024, 1024, downsamples=(1.0, 4.0)), tile_size=256)
    with pytest.raises(ValueError):
        pyramid.tile_grid_size(99)


def test_tile_count() -> None:
    pyramid = XYZPyramid(_FakeSlide(1024, 1024, downsamples=(1.0, 4.0, 16.0)), tile_size=256)
    # z0: 1x1, z1: 1x1, z2: 4x4
    assert pyramid.tile_count == 1 + 1 + 16


def test_get_tile_interior_size_and_level() -> None:
    pytest.importorskip("PIL")
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0))
    pyramid = XYZPyramid(slide, tile_size=256)
    tile = pyramid.get_tile(1, 0, 0)  # z=1 -> native level 0 (full res)
    assert tile.size == (256, 256)
    assert slide.last_call is not None
    assert slide.last_call["level"] == 0
    assert slide.last_call["size"] == (256, 256)


def test_get_tile_reads_coarsest_native_level() -> None:
    pytest.importorskip("PIL")
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0, 16.0))
    pyramid = XYZPyramid(slide, tile_size=256)
    pyramid.get_tile(0, 0, 0)  # z=0 -> coarsest native level 2 (64x64)
    assert slide.last_call is not None
    assert slide.last_call["level"] == 2
    assert slide.last_call["size"] == (64, 64)


def test_get_tile_location_is_level_native() -> None:
    pytest.importorskip("PIL")
    # Level 0 is 1024x1024, level 1 is 256x256. With a 128 px tile the coarse
    # zoom has a 2x2 grid, so a non-(0, 0) tile pins down the coordinate space.
    slide = _FakeSlide(1024, 1024, downsamples=(1.0, 4.0))
    pyramid = XYZPyramid(slide, tile_size=128)
    pyramid.get_tile(0, 1, 1)  # z=0 -> coarsest native level 1 (256x256)
    assert slide.last_call is not None
    assert slide.last_call["level"] == 1
    # Locations are level-native pixels: (1*128, 1*128), not level-0 scaled
    # (which would be (512, 512) if downsamples were applied here).
    assert slide.last_call["location"] == (128, 128)
    assert slide.last_call["size"] == (128, 128)


def test_get_tile_edge_size() -> None:
    pytest.importorskip("PIL")
    slide = _FakeSlide(1000, 1000)  # single level
    pyramid = XYZPyramid(slide, tile_size=256)
    # full-res bottom-right tile: 1000 - 3*256 = 232 px each axis
    tile = pyramid.get_tile(0, 3, 3)
    assert tile.size == (232, 232)


def test_get_tile_out_of_range() -> None:
    pytest.importorskip("PIL")
    pyramid = XYZPyramid(_FakeSlide(1000, 1000), tile_size=256)
    with pytest.raises(ValueError):
        pyramid.get_tile(0, 4, 0)
    with pytest.raises(ValueError):
        pyramid.get_tile(0, 0, -1)
    with pytest.raises(ValueError):
        pyramid.get_tile(99, 0, 0)


class _PlanarRegion:
    """Region whose ``numpy()`` is CHW until ``to_interleaved()`` is called."""

    def __init__(self, hwc: np.ndarray) -> None:
        self._hwc = hwc
        self._interleaved = False

    def to_interleaved(self) -> "_PlanarRegion":
        self._interleaved = True
        return self

    def numpy(self) -> np.ndarray:
        if self._interleaved:
            return self._hwc
        # Band-separate (CHW) view with deliberately "wrong" ordering.
        return np.ascontiguousarray(np.transpose(self._hwc, (2, 0, 1)))


class _PlanarSlide(_FakeSlide):
    def read_region(self, location: tuple[int, int], level: int, size: tuple[int, int]) -> _RegionLike:
        self.last_call = {"location": location, "level": level, "size": size}
        w, h = size
        hwc = np.zeros((h, w, 3), dtype=np.uint8)
        hwc[..., 0] = 200  # distinct red channel to catch channel swaps
        return _PlanarRegion(hwc)


def test_get_tile_converts_planar_to_interleaved() -> None:
    pytest.importorskip("PIL")
    slide = _PlanarSlide(512, 512)
    pyramid = XYZPyramid(slide, tile_size=256)
    tile = pyramid.get_tile(0, 0, 0).convert("RGB")
    # If the planar (CHW) buffer had been used directly the order would be
    # wrong; with to_interleaved() the red channel is preserved as HWC.
    assert tile.size == (256, 256)
    assert tile.getpixel((0, 0)) == (200, 0, 0)


def test_get_tile_bytes_formats() -> None:
    pytest.importorskip("PIL")
    pyramid = XYZPyramid(_FakeSlide(512, 512), tile_size=256)
    jpeg = pyramid.get_tile_bytes(0, 0, 0, fmt="jpeg")
    png = pyramid.get_tile_bytes(0, 0, 0, fmt="png")
    assert jpeg[:2] == b"\xff\xd8"  # JPEG SOI marker
    assert png[:8] == b"\x89PNG\r\n\x1a\n"  # PNG signature


def test_get_tile_bytes_bad_format() -> None:
    pytest.importorskip("PIL")
    pyramid = XYZPyramid(_FakeSlide(512, 512), tile_size=256)
    with pytest.raises(ValueError):
        pyramid.get_tile_bytes(0, 0, 0, fmt="gif")


def test_invalid_tile_size() -> None:
    with pytest.raises(ValueError):
        XYZPyramid(_FakeSlide(512, 512), tile_size=0)


def test_info_payload() -> None:
    slide = _FakeSlide(1000, 800, downsamples=(1.0, 4.0), fmt="svs")
    pyramid = XYZPyramid(slide, tile_size=256)
    info = pyramid.info()
    assert info["dimensions"] == [1000, 800]
    assert info["tile_size"] == 256
    assert info["max_zoom"] == 1
    assert info["min_zoom"] == 0
    assert info["num_zoom_levels"] == 2
    assert info["resolutions"] == [4.0, 1.0]
    assert info["level_count"] == 2
    assert info["mpp"] == [0.25, 0.25]
    assert info["level_dimensions"] == [[1000, 800], [250, 200]]
    assert info["level_downsamples"] == [1.0, 4.0]


def test_info_mpp_none_when_unavailable() -> None:
    slide = _FakeSlide(1000, 800, mpp=None)
    info = XYZPyramid(slide, tile_size=256).info()
    assert info["mpp"] is None
