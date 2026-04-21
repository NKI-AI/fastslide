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
"""Benchmark: GIL-release behaviour of ``FastSlide.read_region``.

Reads ``--num-regions`` tiles from a slide both serially and from a
``ThreadPoolExecutor`` and reports the resulting wall-clock speedup.

Usage:
    bazelisk run //aifo/fastslide/benchmarks:gil_release_benchmark -- \\
        --slide /abs/path/to/slide.svs \\
        --workers 8 --num-regions 32 --tile-size 2048

If the C++ binding releases the GIL via
``nb::call_guard<nb::gil_scoped_release>()`` the parallel run should beat
the serial baseline; if it holds the GIL, parallel will be slower than
serial because of thread-pool overhead.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

from fastslide import FastSlide


def _pick_level(target: FastSlide, tile_size: int) -> int:
    """Pick a mid-pyramid level large enough to host the tile grid.

    The highest-resolution level often has its corners filled with empty
    background, so tiles decode in microseconds. A mid-pyramid level is
    typically dense with tissue and forces real decode/spatial-index work.
    """
    candidates: list[int] = [
        level for level in range(target.level_count) if min(target.level_dimensions[level]) >= 4 * tile_size
    ]
    if not candidates:
        return max(0, target.level_count - 1)
    return candidates[len(candidates) // 2]


def _build_coords(target: FastSlide, level: int, tile_size: int, count: int) -> list[tuple[int, int]]:
    """Build a tile grid centred on the slide so we land in actual tissue."""
    w, h = target.level_dimensions[level]
    cols = max(1, min(8, w // tile_size))
    rows = max(1, (count + cols - 1) // cols)
    span_w = cols * tile_size
    span_h = rows * tile_size
    x0 = max(0, (w - span_w) // 2)
    y0 = max(0, (h - span_h) // 2)
    coords: list[tuple[int, int]] = []
    for r in range(rows):
        for c in range(cols):
            if len(coords) >= count:
                break
            x = x0 + c * tile_size
            y = y0 + r * tile_size
            if x + tile_size > w or y + tile_size > h:
                continue
            coords.append((x, y))
    return coords


def _build_two_disjoint_grids(
    target: FastSlide, level: int, tile_size: int, count_each: int
) -> tuple[list[tuple[int, int]], list[tuple[int, int]]]:
    """Return two non-overlapping coord lists.

    Both serial and parallel timing runs need cold (uncached) tiles,
    otherwise the second run gets a free LRU hit.
    """
    coords = _build_coords(target, level, tile_size, count=2 * count_each)
    return coords[0::2], coords[1::2]


def _read_one(target: FastSlide, xy: tuple[int, int], level: int, tile_size: int) -> np.ndarray:
    img = target.read_region(xy, level, (tile_size, tile_size))
    return np.asarray(img.numpy()).copy()


def _time_serial(
    target: FastSlide,
    coords: list[tuple[int, int]],
    level: int,
    tile_size: int,
) -> tuple[float, list[np.ndarray]]:
    start = time.perf_counter()
    results = [_read_one(target, xy, level, tile_size) for xy in coords]
    return time.perf_counter() - start, results


def _time_parallel(
    target: FastSlide,
    coords: list[tuple[int, int]],
    level: int,
    tile_size: int,
    workers: int,
) -> tuple[float, list[np.ndarray]]:
    start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(lambda xy: _read_one(target, xy, level, tile_size), coords))
    return time.perf_counter() - start, results


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Measure GIL-release speedup of FastSlide.read_region by "
            "comparing serial reads to ThreadPoolExecutor reads."
        )
    )
    parser.add_argument(
        "--slide",
        type=Path,
        required=True,
        help="Path to a slide file readable by FastSlide.",
    )
    parser.add_argument(
        "--num-regions",
        type=int,
        default=16,
        help="Number of tiles to read in each (serial / parallel) run.",
    )
    parser.add_argument(
        "--tile-size",
        type=int,
        default=2048,
        help="Edge length in pixels of every read_region tile.",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=os.cpu_count() or 1,
        help="ThreadPoolExecutor workers for the parallel run.",
    )
    parser.add_argument(
        "--level",
        type=int,
        default=None,
        help="Pyramid level to read from. Auto-picks a mid-level if unset.",
    )
    parser.add_argument(
        "--check-correctness",
        action="store_true",
        help=("After timing, re-read the parallel grid serially and assert byte-identical results across runs."),
    )
    parser.add_argument(
        "--repeats",
        type=int,
        default=1,
        help="Repeat the serial+parallel measurement N times and report each.",
    )
    return parser.parse_args(argv)


def _run_once(
    slide: FastSlide,
    level: int,
    tile_size: int,
    num_regions: int,
    workers: int,
) -> tuple[float, float, int]:
    serial_coords, parallel_coords = _build_two_disjoint_grids(slide, level, tile_size, count_each=num_regions)
    if not serial_coords or not parallel_coords:
        raise SystemExit(
            f"Slide too small for {num_regions} disjoint {tile_size}px "
            f"tiles at level {level}. Try --tile-size or --num-regions."
        )

    # Warm-up: prime decoder/page-cache state without polluting the LRU
    # tile cache for the measurement coords.
    warm = _build_coords(slide, level, tile_size, count=1)
    if warm:
        _read_one(slide, warm[0], level, tile_size)

    serial_t, _ = _time_serial(slide, serial_coords, level, tile_size)
    parallel_t, _ = _time_parallel(slide, parallel_coords, level, tile_size, workers)
    return serial_t, parallel_t, len(serial_coords)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)

    if not args.slide.is_file():
        print(f"error: slide not found: {args.slide}", file=sys.stderr)
        return 2

    slide = FastSlide.from_file_path(str(args.slide))
    try:
        level = args.level if args.level is not None else _pick_level(slide, args.tile_size)
        if not (0 <= level < slide.level_count):
            print(
                f"error: --level={level} out of range [0, {slide.level_count}).",
                file=sys.stderr,
            )
            return 2

        lw, lh = slide.level_dimensions[level]
        cores = os.cpu_count() or 1
        print(f"slide          : {args.slide}")
        print(f"level          : {level}  ({lw} x {lh} px)")
        print(f"tile size      : {args.tile_size}")
        print(f"tiles per run  : {args.num_regions}")
        print(f"workers        : {args.workers} (host has {cores} cores)")
        print(f"repeats        : {args.repeats}")
        print()
        print(f"{'run':>4} {'serial (s)':>12} {'parallel (s)':>14} {'speedup':>10} {'tiles':>7}")
        print("-" * 51)

        speedups: list[float] = []
        for run in range(1, args.repeats + 1):
            serial_t, parallel_t, ntiles = _run_once(
                slide,
                level,
                args.tile_size,
                args.num_regions,
                args.workers,
            )
            speedup = serial_t / parallel_t if parallel_t > 0 else float("inf")
            speedups.append(speedup)
            print(f"{run:>4} {serial_t:>12.3f} {parallel_t:>14.3f} {speedup:>9.2f}x {ntiles:>7}")

        if len(speedups) > 1:
            mean = sum(speedups) / len(speedups)
            best = max(speedups)
            worst = min(speedups)
            print("-" * 51)
            print(f"speedup        : mean={mean:.2f}x best={best:.2f}x worst={worst:.2f}x")

        if args.check_correctness:
            print()
            print("checking correctness (parallel vs serial bytes)...")
            _, par_coords = _build_two_disjoint_grids(slide, level, args.tile_size, count_each=args.num_regions)
            serial_imgs = [_read_one(slide, xy, level, args.tile_size) for xy in par_coords]
            with ThreadPoolExecutor(max_workers=args.workers) as pool:
                par_imgs = list(
                    pool.map(
                        lambda xy: _read_one(slide, xy, level, args.tile_size),
                        par_coords,
                    )
                )
            for i, (a, b) in enumerate(zip(serial_imgs, par_imgs)):
                if not np.array_equal(a, b):
                    print(
                        f"correctness FAILED at tile {i} {par_coords[i]}",
                        file=sys.stderr,
                    )
                    return 1
            print(f"correctness OK ({len(par_coords)} tiles match)")
    finally:
        slide.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
