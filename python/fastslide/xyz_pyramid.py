# Copyright 2026 Jonas Teuwen. All Rights Reserved.
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
"""XYZ (slippy-map) tile pyramid generator for FastSlide.

This module turns a :class:`fastslide.FastSlide` slide into an XYZ tile
pyramid, the tiling scheme used by web map viewers such as OpenLayers and
Leaflet. Unlike Deep Zoom (DZI), an XYZ pyramid uses a top-left tile origin
with the y axis pointing down.

Each zoom level maps **one-to-one onto a native pyramid level of the slide**:
``z = 0`` is the coarsest native level and ``z = max_zoom`` is the
full-resolution level 0. The zoom resolutions are therefore the slide's own
``level_downsamples`` (which need not be powers of two), so a tile is always a
direct native read with no server-side resampling. Viewers such as OpenLayers
accept this kind of non-power-of-two ``resolutions`` array on a custom
``TileGrid`` and scale between levels on the client, so intermediate zooms come
for free without the server ever resampling.

Example:
    >>> import fastslide
    >>> from fastslide.xyz_pyramid import XYZPyramid
    >>> slide = fastslide.FastSlide.from_file_path("slide.svs")
    >>> pyramid = XYZPyramid(slide, tile_size=256)
    >>> png_bytes = pyramid.get_tile_bytes(pyramid.max_zoom, 0, 0, fmt="png")

The class is intentionally dependency-light: only ``Pillow`` (imported lazily)
is required on top of ``numpy``, which FastSlide already depends on.
"""

from __future__ import annotations

import io
import math
from typing import TYPE_CHECKING, Any, Protocol

if TYPE_CHECKING:
    from PIL import Image as PILImage


class _SlideLike(Protocol):
    """Structural type for the slide objects accepted by :class:`XYZPyramid`.

    Both :class:`fastslide.FastSlide` and :class:`fastslide.SlideImageView`
    satisfy this protocol, so a pyramid can be built from either a whole slide
    or one of its navigable sub-images.
    """

    @property
    def dimensions(self) -> tuple[int, int]: ...

    @property
    def level_count(self) -> int: ...

    @property
    def level_dimensions(self) -> tuple[tuple[int, int], ...]: ...

    @property
    def level_downsamples(self) -> tuple[float, ...]: ...

    @property
    def mpp(self) -> tuple[float, float]: ...

    def read_region(self, location: tuple[int, int], level: int, size: tuple[int, int]) -> Any: ...


_JPEG_FORMATS = frozenset({"jpg", "jpeg"})
_PNG_FORMATS = frozenset({"png"})


class XYZPyramid:
    """Generates XYZ tiles from a FastSlide slide's native pyramid levels.

    Each zoom level maps directly onto one native pyramid level: ``z = 0`` is
    the coarsest native level and ``z = max_zoom`` is full resolution (level 0).
    The per-zoom resolutions are the slide's ``level_downsamples`` (possibly
    non-power-of-two), so every tile is a direct native read -- no resampling.
    Tiles are addressed by ``(z, x, y)`` with ``x`` the column (increasing
    rightwards) and ``y`` the row (increasing downwards).

    Attributes:
        tile_size: Edge length of a (square) full tile in pixels.
        max_zoom: Highest zoom level (full resolution, native level 0).
    """

    def __init__(self, slide: _SlideLike, tile_size: int = 256) -> None:
        """Initializes the pyramid.

        Args:
            slide: An opened ``FastSlide`` (or ``SlideImageView``) instance.
            tile_size: Edge length of a square tile in pixels. Must be > 0.

        Raises:
            ValueError: If ``tile_size`` is not a positive integer.
        """
        if tile_size <= 0:
            raise ValueError(f"tile_size must be positive, got {tile_size}")

        self._slide = slide
        self._tile_size = int(tile_size)

        width, height = slide.dimensions
        self._width = int(width)
        self._height = int(height)

        self._level_count = int(slide.level_count)
        self._downsamples = tuple(float(d) for d in slide.level_downsamples)
        self._level_dimensions = tuple((int(w), int(h)) for w, h in slide.level_dimensions)

    @property
    def slide(self) -> _SlideLike:
        """The underlying slide object backing this pyramid."""
        return self._slide

    @property
    def tile_size(self) -> int:
        """Edge length of a full tile in pixels."""
        return self._tile_size

    @property
    def max_zoom(self) -> int:
        """Highest zoom level (full resolution, native level 0)."""
        return self._level_count - 1

    @property
    def min_zoom(self) -> int:
        """Lowest zoom level (coarsest native level)."""
        return 0

    @property
    def num_zoom_levels(self) -> int:
        """Total number of zoom levels (one per native level)."""
        return self._level_count

    @property
    def level0_dimensions(self) -> tuple[int, int]:
        """Full-resolution slide dimensions as ``(width, height)``."""
        return (self._width, self._height)

    @property
    def resolutions(self) -> list[float]:
        """Per-zoom resolutions (level-0 px per tile px), ``z`` ordered.

        Index ``z`` holds the downsample of the native level that zoom maps to,
        ordered coarsest (``z = 0``) to finest (``z = max_zoom``). This is the
        ``resolutions`` array an OpenLayers ``TileGrid`` expects.
        """
        return [self.resolution_for_zoom(z) for z in range(self._level_count)]

    def level_for_zoom(self, z: int) -> int:
        """Returns the native pyramid level backing zoom ``z``.

        Raises:
            ValueError: If ``z`` is outside ``[0, max_zoom]``.
        """
        self._check_zoom(z)
        return self._level_count - 1 - z

    def resolution_for_zoom(self, z: int) -> float:
        """Returns the downsample (level-0 px per tile px) at zoom ``z``."""
        return self._downsamples[self.level_for_zoom(z)]

    def zoom_dimensions(self, z: int) -> tuple[int, int]:
        """Returns the native level pixel size ``(width, height)`` at zoom ``z``."""
        return self._level_dimensions[self.level_for_zoom(z)]

    def tile_grid_size(self, z: int) -> tuple[int, int]:
        """Returns the tile grid as ``(cols, rows)`` at zoom ``z``.

        Args:
            z: Zoom level.

        Raises:
            ValueError: If ``z`` is outside ``[0, max_zoom]``.
        """
        level_w, level_h = self.zoom_dimensions(z)
        cols = math.ceil(level_w / self._tile_size)
        rows = math.ceil(level_h / self._tile_size)
        return (cols, rows)

    @property
    def tile_count(self) -> int:
        """Total number of tiles across every zoom level."""
        total = 0
        for z in range(self.num_zoom_levels):
            cols, rows = self.tile_grid_size(z)
            total += cols * rows
        return total

    def get_tile(self, z: int, x: int, y: int) -> "PILImage.Image":
        """Renders a single XYZ tile.

        The tile is read directly from the native level backing zoom ``z`` with
        no resampling; ``(x, y)`` index whole ``tile_size`` blocks of that
        level. Edge tiles are simply clipped to the level bounds.

        Args:
            z: Zoom level in ``[0, max_zoom]``.
            x: Tile column in ``[0, cols - 1]``.
            y: Tile row in ``[0, rows - 1]``.

        Returns:
            A Pillow image of size up to ``(tile_size, tile_size)``. Tiles on
            the right/bottom edges may be smaller when the level does not
            divide evenly into whole tiles.

        Raises:
            ValueError: If the tile address is out of range.
        """
        level = self.level_for_zoom(z)
        cols, rows = self.tile_grid_size(z)
        if not (0 <= x < cols and 0 <= y < rows):
            raise ValueError(f"tile ({z}, {x}, {y}) out of range; grid at z={z} is {cols}x{rows}")

        # Level-native coordinates: (x, y) tile blocks of this level's own grid.
        level_w, level_h = self._level_dimensions[level]
        px = x * self._tile_size
        py = y * self._tile_size
        w = min(self._tile_size, level_w - px)
        h = min(self._tile_size, level_h - py)

        region = self._slide.read_region((px, py), level, (w, h))
        # ``to_interleaved`` guarantees HWC channel order (no-op when the image
        # is already interleaved); band-separate slides would otherwise yield a
        # CHW view with the wrong channel ordering.
        return self._array_to_pil(region.to_interleaved().numpy())

    def get_tile_bytes(
        self,
        z: int,
        x: int,
        y: int,
        fmt: str = "jpeg",
        quality: int = 85,
    ) -> bytes:
        """Renders a tile and encodes it to image bytes.

        Args:
            z: Zoom level.
            x: Tile column.
            y: Tile row.
            fmt: Output format, one of ``"jpeg"``/``"jpg"`` or ``"png"``.
            quality: JPEG quality in ``[1, 100]`` (ignored for PNG).

        Returns:
            The encoded image as raw bytes.

        Raises:
            ValueError: If the tile address is out of range or ``fmt`` is
                unsupported.
        """
        fmt_lower = fmt.lower()
        tile = self.get_tile(z, x, y)
        buffer = io.BytesIO()

        if fmt_lower in _JPEG_FORMATS:
            if tile.mode not in ("RGB", "L"):
                tile = tile.convert("RGB")
            tile.save(buffer, format="JPEG", quality=quality)
        elif fmt_lower in _PNG_FORMATS:
            tile.save(buffer, format="PNG")
        else:
            raise ValueError(f"unsupported tile format: {fmt!r}")

        return buffer.getvalue()

    def info(self) -> dict[str, Any]:
        """Returns a JSON-serializable description of the pyramid.

        The dictionary is consumed by the example viewer to configure the
        OpenLayers tile grid and populate the slide info panel.
        """
        return {
            "dimensions": [self._width, self._height],
            "tile_size": self._tile_size,
            "min_zoom": self.min_zoom,
            "max_zoom": self.max_zoom,
            "num_zoom_levels": self.num_zoom_levels,
            "resolutions": self.resolutions,
            "level_count": self._level_count,
            "level_dimensions": [[w, h] for w, h in self._level_dimensions],
            "level_downsamples": [d for d in self._downsamples],
            "mpp": self._slide_mpp(),
        }

    def _check_zoom(self, z: int) -> None:
        if not (0 <= z <= self.max_zoom):
            raise ValueError(f"zoom {z} out of range [0, {self.max_zoom}]")

    @staticmethod
    def _array_to_pil(array: Any) -> "PILImage.Image":
        """Converts an HWC uint8 numpy array to a Pillow image."""
        from PIL import Image as PILImage

        if array.ndim == 3 and array.shape[2] == 1:
            array = array[:, :, 0]

        if array.ndim == 2:
            return PILImage.fromarray(array, mode="L")

        channels = array.shape[2]
        if channels == 3:
            return PILImage.fromarray(array, mode="RGB")
        if channels == 4:
            return PILImage.fromarray(array, mode="RGBA")
        # Fall back to the first three channels for multiplex/spectral data.
        return PILImage.fromarray(array[:, :, :3].copy(), mode="RGB")

    def _slide_mpp(self) -> list[float] | None:
        try:
            mpp = self._slide.mpp
        except (RuntimeError, ValueError, AttributeError, KeyError):
            return None
        if not mpp:
            return None
        try:
            mpp_x, mpp_y = float(mpp[0]), float(mpp[1])
        except (TypeError, ValueError, IndexError):
            return None
        if mpp_x <= 0 and mpp_y <= 0:
            return None
        return [mpp_x, mpp_y]
