# Benchmarks

# Find Google Benchmark
benchmark_dep = dependency('benchmark', required: false)

if not benchmark_dep.found()
  # Try using subproject/wrap - disable tests in the subproject
  benchmark_proj = subproject('google-benchmark', 
    required: false,
    default_options: ['tests=disabled'])
  if benchmark_proj.found()
    benchmark_dep = benchmark_proj.get_variable('google_benchmark_dep')
  endif
endif

# Find libtiff for comparison
libtiff_dep = dependency('libtiff-4', required: false)

if benchmark_dep.found() and libtiff_dep.found()
  bench_tile_reading = executable('bench_tile_reading',
    'bench_tile_reading.cpp',
    dependencies: [simpletiff_dep, benchmark_dep, libtiff_dep],
    include_directories: inc)
  
  benchmark('tile_reading', bench_tile_reading)
  
  message('Benchmarks enabled (Google Benchmark and libtiff found)')
else
  if not benchmark_dep.found()
    warning('Google Benchmark not found, benchmarks disabled')
  endif
  if not libtiff_dep.found()
    warning('libtiff not found, benchmarks disabled')
  endif
endif

