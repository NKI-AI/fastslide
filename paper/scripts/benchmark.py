"""Region-read throughput of FastSlide and OpenSlide through their Python APIs.

For each slide: N random 512x512 level-0 regions (fixed seed, same coordinates for
both libraries), read from ONE open handle by 1 and 8 threads. Each read is
converted to a NumPy array. Neither library is given extra cache (FastSlide: no
tile cache; OpenSlide: its default per-handle cache). The file is read once
beforehand so it sits in the OS page cache. Reported: median regions/s over
REPEATS runs, with min and max, and the CPU-seconds used per wall-clock second.

FastSlide is measured in both of its read modes: THREAD_POOL (the default; the
tiles of a read are decoded on a process-wide pool) and CALLING_THREAD (a read
stays on the thread that called it, as in OpenSlide). The pool is sized to the
CPUs available to the process, as the README advises when that is fewer than
the hardware threads of the host; both numbers are printed and stored.
"""
import json
import os
import random
import resource
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import openslide

import fastslide
from fastslide import FastSlide

N, SIZE, REPEATS, THREADS = int(os.environ.get("BENCH_N", 1000)), 512, 5, (1, 8)
CPUS = len(os.sched_getaffinity(0))


def warm(path):
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


def cpu_seconds():
    r = resource.getrusage(resource.RUSAGE_SELF)
    return r.ru_utime + r.ru_stime


def run(read, coords, threads):
    """Return (regions per second, CPU-seconds per wall-clock second)."""
    c0 = cpu_seconds()
    t0 = time.perf_counter()
    if threads == 1:
        for c in coords:
            read(c)
    else:
        def discard(c):
            read(c)  # do not keep the arrays: 1,000 of them are about 1 GB

        with ThreadPoolExecutor(threads) as ex:
            list(ex.map(discard, coords))
    wall = time.perf_counter() - t0
    return len(coords) / wall, (cpu_seconds() - c0) / wall


fastslide.set_num_threads(CPUS)
out = {"fastslide": fastslide.__version__, "openslide": openslide.__library_version__,
       "openslide_python": openslide.__version__, "n": N, "size": SIZE, "repeats": REPEATS, "cpus_available": CPUS,
       "pool_threads": fastslide.get_num_threads(), "results": {}}
print(f"CPUs available to this process: {CPUS}; FastSlide pool threads: {fastslide.get_num_threads()}", flush=True)
MODES = {"fastslide_pool": fastslide.ReadParallelism.THREAD_POOL,
         "fastslide_calling": fastslide.ReadParallelism.CALLING_THREAD}
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
    for name, fn in (("fastslide_pool", rf), ("fastslide_calling", rf), ("openslide", ro)):
        if name in MODES:
            fastslide.set_read_parallelism(MODES[name])
        for th in THREADS:
            run(fn, coords[:100], th)  # warm-up
            runs = [run(fn, coords, th) for _ in range(REPEATS)]
            r = [x[0] for x in runs]
            res[f"{name}_{th}"] = (statistics.median(r), min(r), max(r), statistics.median(x[1] for x in runs))
    out["results"][label] = res
    print(f"{label} {W}x{H}", flush=True)
    for th in THREADS:
        print(f"  T={th}: " + "  ".join(
            f"{k} {v[0]:.0f} [{v[1]:.0f}-{v[2]:.0f}] cpu {v[3]:.1f}"
            for k, v in ((n, res[f"{n}_{th}"]) for n in ("fastslide_pool", "fastslide_calling", "openslide"))), flush=True)
json.dump(out, open(os.environ.get("BENCH_OUT", "bench.json"), "w"), indent=1)
