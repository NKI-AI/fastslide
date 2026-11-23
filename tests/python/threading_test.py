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
"""Tests for the process-wide threading settings (read parallelism, pool size)."""

import os
import threading
from collections.abc import Iterator
from pathlib import Path

import numpy as np
import pytest

import fastslide

SAMPLE_SLIDE_CANDIDATES = [
    "tests/test_data/CMU-1-Small-Region.svs",
    "CMU-1-Small-Region.svs",
    str(Path(__file__).resolve().parents[1] / "test_data" / "CMU-1-Small-Region.svs"),
]


@pytest.fixture(autouse=True)
def restore_settings() -> Iterator[None]:
    """Leave the process-wide settings as each test found them."""
    mode = fastslide.get_read_parallelism()
    num_threads = fastslide.get_num_threads()
    yield
    fastslide.set_read_parallelism(mode)
    fastslide.set_num_threads(num_threads)


@pytest.fixture
def sample_slide_path() -> str:
    path = next((c for c in SAMPLE_SLIDE_CANDIDATES if os.path.exists(c)), None)
    if path is None:
        pytest.skip("CMU-1-Small-Region.svs not available")
    return path


def read_rgb(slide: fastslide.FastSlide, x: int, y: int, size: int) -> np.ndarray:
    return np.asarray(slide.read_region((x, y), 0, (size, size)).to_interleaved().numpy()).copy()


class TestReadParallelism:
    def test_enum_values(self) -> None:
        assert {m.name for m in fastslide.ReadParallelism} == {"THREAD_POOL", "CALLING_THREAD"}

    @pytest.mark.skipif("FASTSLIDE_READ_PARALLELISM" in os.environ, reason="mode set from the environment")
    def test_default_is_thread_pool(self) -> None:
        assert fastslide.get_read_parallelism() == fastslide.ReadParallelism.THREAD_POOL

    def test_set_and_get(self) -> None:
        fastslide.set_read_parallelism(fastslide.ReadParallelism.CALLING_THREAD)
        assert fastslide.get_read_parallelism() == fastslide.ReadParallelism.CALLING_THREAD
        fastslide.set_read_parallelism(fastslide.ReadParallelism.THREAD_POOL)
        assert fastslide.get_read_parallelism() == fastslide.ReadParallelism.THREAD_POOL

    def test_rejects_non_enum(self) -> None:
        with pytest.raises(TypeError):
            fastslide.set_read_parallelism("calling_thread")  # type: ignore[arg-type]


class TestNumThreads:
    def test_positive(self) -> None:
        assert fastslide.get_num_threads() >= 1

    def test_set_and_get(self) -> None:
        fastslide.set_num_threads(3)
        assert fastslide.get_num_threads() == 3

    def test_zero_means_hardware_threads(self) -> None:
        fastslide.set_num_threads(0)
        assert fastslide.get_num_threads() >= 1

    def test_rejects_negative(self) -> None:
        with pytest.raises(TypeError):
            fastslide.set_num_threads(-1)


class TestReadsAcrossSettings:
    def test_same_pixels_in_both_modes(self, sample_slide_path: str) -> None:
        # 1000 px spans several stored tiles, so the pool path is exercised.
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            fastslide.set_read_parallelism(fastslide.ReadParallelism.THREAD_POOL)
            pooled = read_rgb(slide, 100, 200, 1000)
            fastslide.set_read_parallelism(fastslide.ReadParallelism.CALLING_THREAD)
            on_caller = read_rgb(slide, 100, 200, 1000)
        np.testing.assert_array_equal(pooled, on_caller)

    def test_reads_continue_while_pool_is_resized(self, sample_slide_path: str) -> None:
        with fastslide.FastSlide.from_file_path(sample_slide_path) as slide:
            expected = read_rgb(slide, 0, 0, 800)
            stop = threading.Event()
            errors: list[Exception] = []

            def reader() -> None:
                try:
                    while not stop.is_set():
                        np.testing.assert_array_equal(read_rgb(slide, 0, 0, 800), expected)
                except Exception as e:
                    errors.append(e)

            threads = [threading.Thread(target=reader) for _ in range(4)]
            for t in threads:
                t.start()
            for n in (1, 4, 2, 0):
                fastslide.set_num_threads(n)
            stop.set()
            for t in threads:
                t.join(timeout=60)
            assert not any(t.is_alive() for t in threads), "a reader hung"
            assert not errors, errors[0]
