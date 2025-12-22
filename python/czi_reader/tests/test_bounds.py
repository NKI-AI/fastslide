from __future__ import annotations

from czi_reader.structs import Bounds
from czi_reader.reader import _clip_bounds_to_size, _size_from_bounds


def test_size_from_bounds_uses_offset_extent() -> None:
    b = Bounds(x=10, y=20, width=100, height=200)
    assert _size_from_bounds(b) == (110, 220)


def test_clip_bounds_to_size_clamps_max_extent() -> None:
    b = Bounds(x=10, y=20, width=200, height=300)  # x2=210, y2=320
    clipped = _clip_bounds_to_size(b, size_l0=(100, 200))
    assert clipped == Bounds(x=10, y=20, width=90, height=180)


def test_clip_bounds_to_size_handles_negative_and_inverted() -> None:
    b = Bounds(x=-50, y=-10, width=20, height=-5)  # inverted y2
    clipped = _clip_bounds_to_size(b, size_l0=(100, 200))
    # x clamps to 0, x2 clamps to 0; y clamps to 0, y2 clamps to 0.
    assert clipped == Bounds(x=0, y=0, width=0, height=0)
