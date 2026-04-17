# SimpleTIFF Benchmarks

This directory contains performance benchmarks comparing SimpleTIFF against libtiff.

## Running Benchmarks

```bash
# From project root
meson compile -C builddir
./builddir/benchmarks/bench_tile_reading

# With multiple repetitions for statistical analysis
./builddir/benchmarks/bench_tile_reading --benchmark_repetitions=3

# Filter specific benchmarks
./builddir/benchmarks/bench_tile_reading --benchmark_filter=Sequential
```

## Benchmark Results

### Tile Reading Performance (checkerboard.svs)

Results on Apple M2 (3 repetitions, mean ± stddev):

#### File Kept Open (normal usage)

| Benchmark                 | Time (ms) | Throughput (GB/s) | Tiles/sec | vs libtiff          |
| ------------------------- | --------- | ----------------- | --------- | ------------------- |
| **SimpleTIFF Sequential** | 634 ± 1   | 1.15              | 6257      | **1.05x faster** ✨ |
| **SimpleTIFF Random**     | 667 ± 1   | 1.09              | 5951      | **1.07x faster** ✨ |
| **libtiff Sequential**    | 668 ± 7   | 1.09              | 5941      | 1.0x (baseline)     |
| **libtiff Random**        | 717 ± 30  | 1.02              | 5544      | 0.93x               |

#### Open/Close Per Tile (worst-case scenario)

| Benchmark                          | Time (ms) | Throughput (MB/s) | Tiles/sec | vs libtiff          |
| ---------------------------------- | --------- | ----------------- | --------- | ------------------- |
| **SimpleTIFF Random + Open/Close** | 2773 ± 2  | 268               | 1431      | 0.32x (3.1x slower) |
| **libtiff Random + Open/Close**    | 885 ± 5   | 841               | 4487      | 1.0x (baseline)     |

**Performance Optimization Journey:**

- **Initial**: 6000 ms → SimpleTIFF was **9.4x slower** than libtiff
- **After thread_local buffers**: 928 ms → **6.5x speedup**, 1.4x slower
- **After zero-copy JPEG composition**: 635 ms → **9.4x total speedup**, **now faster than libtiff!** 🚀

**Note**: Both implementations read tiles by the exact same index:

- SimpleTIFF: `ReadTile(index, fd, page_index, tile_index, buffer)`
- libtiff: `TIFFReadEncodedTile(tif, tile_index, buffer, size)`

### Key Observations

1. **SimpleTIFF is faster than libtiff (file kept open)!** 🎉

   - Sequential tile reading: **5% faster** (634ms vs 668ms)
   - Random tile reading: **7% faster** (667ms vs 717ms)
   - Achieves 1.15 GB/s throughput and 6257 tiles/second

2. **Open/Close Overhead Analysis**:

   - **SimpleTIFF**: 4.2x slower when opening/closing per tile (2773ms vs 667ms)
     - Overhead dominated by full TIFF parsing on each open
   - **libtiff**: 1.2x slower when opening/closing per tile (885ms vs 717ms)
     - More incremental parsing reduces overhead
   - **Recommendation**: For best SimpleTIFF performance, keep file open when reading multiple tiles

3. **Key Optimizations Applied**:

   - **thread_local buffers**: Eliminates 200,000+ allocations per benchmark
   - **Zero-copy JPEG composition**: Direct memcpy instead of vector::insert
   - **JPEG tables caching**: Load once per page, reuse across tiles
   - Combined optimizations: **9.4x speedup** over initial implementation

4. **Sequential vs Random (file kept open)**: Both libraries show <1% performance difference
   - Indicates excellent tile indexing in both implementations
   - Random access doesn't significantly impact read performance

## Implementation Details

**Optimizations Applied:**

- ✅ `thread_local` buffers for temp_buffer, jpeg_tables, jpeg_stream
- ✅ Zero-copy JPEG stream composition with direct memcpy
- ✅ JPEG tables cached per page (not re-read for each tile)
- ✅ Single-pass buffer filling instead of multiple insert operations

**Potential Future Enhancements:**

- Parallel tile decompression (minimal gains since already faster than libtiff)
- Tile-level LRU cache for frequently accessed tiles
- SIMD optimizations for predictor operations (LZW decompression)

## Test Data

- **checkerboard.svs**: Whole-slide imaging test file with tiled JPEG compression
  - Tile dimensions: typically 240x240 or 256x256
  - Number of tiles: ~3900 (varies by file)
  - Compression: JPEG

## Requirements

- Google Benchmark (automatically downloaded via Meson wrap)
- libtiff-4 (system package)
