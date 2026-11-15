"""Region-read throughput of FastSlide and OpenSlide through their Python APIs.

For each slide: N random 512x512 level-0 regions (fixed seed, same coordinates for
both libraries), read from ONE open handle by 1, 8 and 32 threads. Each read is
converted to a NumPy array. Both libraries run with default settings (FastSlide:
no tile cache; OpenSlide: its default per-handle cache). The file is read once
beforehand so it sits in the OS page cache. Reported: median regions/s over
REPEATS runs, with min and max.
"""
import json
import random
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import openslide

import fastslide
from fastslide import FastSlide

N, SIZE, REPEATS, THREADS = 1000, 512, 5, (1, 8, 32)


def warm(path):
    import os
    paths = [path]
    if path.endswith(".mrxs"):
        d = path[:-5]
        paths += [os.path.join(d, f) for f in os.listdir(d)]
    if path.endswith(".dcm"):
        d = os.path.dirname(path)
        paths = [os.path.join(d, f) for f in os.listdir(d)]
    for p in paths:
        with open(p, "rb") as fh:
            while fh.read(1 << 24):
                pass


def run(read, coords, threads):
    t0 = time.perf_counter()
    if threads == 1:
        for c in coords:
            read(c)
    else:
        with ThreadPoolExecutor(threads) as ex:
            list(ex.map(read, coords))
    return len(coords) / (time.perf_counter() - t0)


out = {"fastslide": fastslide.__version__, "openslide": openslide.__library_version__,
       "openslide_python": openslide.__version__, "n": N, "size": SIZE, "repeats": REPEATS, "results": {}}
for arg in sys.argv[1:]:
    label, path = arg.split("=", 1)
    warm(path)
    f = FastSlide.from_file_path(path)
    o = openslide.OpenSlide(path)
    W, H = f.dimensions
    assert (W, H) == o.dimensions, (label, f.dimensions, o.dimensions)
    rng = random.Random(0)
    coords = [(rng.randrange(0, W - SIZE), rng.randrange(0, H - SIZE)) for _ in range(N)]

    def rf(c):
        return f.read_region(c, 0, (SIZE, SIZE)).numpy()

    def ro(c):
        return np.asarray(o.read_region(c, 0, (SIZE, SIZE)))

    res = {}
    for name, fn in (("fastslide", rf), ("openslide", ro)):
        for th in THREADS:
            run(fn, coords[:100], th)  # warm-up
            r = [run(fn, coords, th) for _ in range(REPEATS)]
            res[f"{name}_{th}"] = (statistics.median(r), min(r), max(r))
    out["results"][label] = res
    line = f"{label:14s} {W}x{H} "
    for th in THREADS:
        a, b = res[f"fastslide_{th}"], res[f"openslide_{th}"]
        line += f"| T={th}: FS {a[0]:7.0f} [{a[1]:.0f}-{a[2]:.0f}]  OS {b[0]:7.0f} [{b[1]:.0f}-{b[2]:.0f}]  x{a[0] / b[0]:.2f} "
    print(line, flush=True)
json.dump(out, open("bench.json", "w"), indent=1)
