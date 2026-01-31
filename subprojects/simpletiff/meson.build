project('simpletiff', 'cpp',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

# Dependencies
threads_dep = dependency('threads')
aifocore_dep = dependency('aifocore', fallback : ['aifocore', 'aifocore_dep'])
zstd_dep = dependency('libzstd', fallback : ['zstd', 'libzstd_dep'])
zlib_dep = dependency('zlib-ng', fallback : ['zlib-ng', 'zlib-ng_dep'])
png_dep = dependency('libpng',
  fallback : ['libpng', 'libpng_dep'],
  required : false)

# JPEG decoder: jpgd or libjpeg-turbo
use_jpgd = get_option('use_jpgd')
if use_jpgd
  # Use jpeg-compressor (jpgd) - available via wrap file
  # The wrap file handles both monorepo and copybara-translated locations
  jpgd_proj = subproject('jpeg-compressor', default_options: [
    'default_library=static',
    'warning_level=0'
  ])
  jpgd_dep = jpgd_proj.get_variable('jpgd_dep')
  jpeg_dep = jpgd_dep
else
  jpeg_dep = dependency('libjpeg', required : true)
endif

# Include directories
inc = include_directories('include')

# Core library sources
simpletiff_sources = [
  'src/tiff_parser.cpp',
  'src/reader.cpp',
  'src/deflate.cpp',
  'src/lzw.cpp',
  'src/predictor.cpp',
  'src/zstd.cpp',
  'src/index.cpp',
  'src/jpeg2000.cpp',
  'src/ndpi_mcu_tiling.cpp',
]

# Add conditional JPEG I/O implementation
if use_jpgd
  simpletiff_sources += 'src/io_utils_jpgd.cpp'
else
  simpletiff_sources += 'src/io_utils.cpp'
endif

# Optional PNG writer
simpletiff_deps = [threads_dep, jpeg_dep, zstd_dep, zlib_dep, aifocore_dep]
if png_dep.found()
  simpletiff_sources += 'src/image_writer.cpp'
  simpletiff_deps += png_dep
  add_project_arguments('-DSIMPLETIFF_HAS_PNG', language : 'cpp')
endif

# Build the library
simpletiff_lib = library('simpletiff',
  simpletiff_sources,
  include_directories : inc,
  dependencies : simpletiff_deps,
  install : true)

# Public headers
headers_to_install = [
  'include/simpletiff/index.h',
  'include/simpletiff/tiff_parser.h',
  'include/simpletiff/reader.h',
  'include/simpletiff/io_utils.h',
  'include/simpletiff/decompression.h',
  'include/simpletiff/deflate.h',
  'include/simpletiff/lzw.h',
  'include/simpletiff/predictor.h',
  'include/simpletiff/zstd.h',
  'include/simpletiff/tiff_constants.h',
  'include/simpletiff/errors.h',
]

if png_dep.found()
  headers_to_install += 'include/simpletiff/image_writer.h'
endif

install_headers(headers_to_install, subdir : 'simpletiff')

# Make library usable as dependency
simpletiff_dep = declare_dependency(
  include_directories : inc,
  link_with : simpletiff_lib,
  dependencies : simpletiff_deps)

meson.override_dependency('simpletiff', simpletiff_dep)

# Examples
subdir('examples')

# Tests (if enabled)
if get_option('build_tests')
  subdir('tests')
endif

# Benchmarks (if enabled)
if get_option('build_benchmarks')
  subdir('benchmarks')
endif

# Python bindings (if enabled)
if get_option('build_python_bindings')
  # Python module
  py = import('python').find_installation(pure: false)
  py_dep = py.dependency()
  
  # pybind11 dependency
  pybind11_dep = dependency('pybind11', fallback : ['pybind11', 'pybind11_dep'])
  
  # Python extension module
  py.extension_module('_simpletiff',
    'python/simpletiff_bindings.cpp',
    include_directories : inc,
    dependencies : [pybind11_dep, py_dep, threads_dep, jpeg_dep, zstd_dep, aifocore_dep],
    link_with : simpletiff_lib,
    install : true,
    subdir : 'simpletiff'
  )
  
  # Install Python package files
  py.install_sources(
    'python/simpletiff/__init__.py',
    'python/simpletiff/_simpletiff.pyi',
    'python/simpletiff/py.typed',
    subdir : 'simpletiff'
  )
endif

