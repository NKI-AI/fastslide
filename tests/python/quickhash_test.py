# Copyright 2026 AI for Oncology
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
"""Tests for the ``FastSlide.quickhash`` binding.

The recipes themselves are covered by the C++ suite against fixtures it writes
at run time. What is only reachable from Python is the binding contract: that
``quickhash`` is a read-only property, that it never yields an empty string,
and that a closed slide raises rather than returning something plausible.

These tests need a real slide and skip without one, matching the other Python
suites here. No slide data is checked in.
"""

from __future__ import annotations

import os
import re
from collections.abc import Generator

import pytest

import fastslide

#: A digest is exactly 64 lowercase hex characters. Anything else -- most of all
#: the empty string -- is a bug, not a slide that happens to have no hash.
QUICKHASH_PATTERN = re.compile(r"^[0-9a-f]{64}$")


@pytest.fixture(scope="session")
def sample_slide_path() -> str:
    """Path to any slide available in the working directory."""
    candidates = [
        "LuCa-7color_Scan1.qptiff",
        "CMU-1-Small-Region.svs",
        "tests/test_data/CMU-1-Small-Region.svs",
        "cmmu_3316_2016.mrxs",
    ]
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate
    pytest.skip("No sample slide file available")


@pytest.fixture
def slide(sample_slide_path: str) -> Generator[fastslide.FastSlide, None, None]:
    """An open slide, closed again after the test."""
    opened = fastslide.FastSlide.from_file_path(sample_slide_path)
    yield opened
    opened.close()


class TestQuickHash:
    """The ``quickhash`` property contract."""

    def test_is_a_well_formed_digest(self, slide: fastslide.FastSlide) -> None:
        assert QUICKHASH_PATTERN.match(slide.quickhash), f"Not a 64-character lowercase hex digest: {slide.quickhash!r}"

    def test_is_never_empty(self, slide: fastslide.FastSlide) -> None:
        """Readers used to signal "no hash" by returning "".

        That turned a missing digest into a valid-looking value in Python,
        where every unhashable slide compared equal to every other one.
        """
        assert slide.quickhash != ""

    def test_is_stable_across_calls(self, slide: fastslide.FastSlide) -> None:
        assert slide.quickhash == slide.quickhash

    def test_is_stable_across_reopens(self, sample_slide_path: str) -> None:
        with fastslide.FastSlide.from_file_path(sample_slide_path) as first:
            first_hash = first.quickhash
        with fastslide.FastSlide.from_file_path(sample_slide_path) as second:
            second_hash = second.quickhash
        assert first_hash == second_hash

    def test_is_read_only(self, slide: fastslide.FastSlide) -> None:
        with pytest.raises(AttributeError):
            slide.quickhash = "0" * 64  # type: ignore[misc]

    def test_closed_slide_raises(self, sample_slide_path: str) -> None:
        closed = fastslide.FastSlide.from_file_path(sample_slide_path)
        closed.close()
        with pytest.raises(RuntimeError, match="slide reader is closed"):
            _ = closed.quickhash
