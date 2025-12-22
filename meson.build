# FastSlide Meson Build File
# Modern Meson build system for FastSlide library with static linking

project('fastslide', 'cpp', 'c',
  version: '0.2.0',
  license: 'Apache-2.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'werror=false',
    'default_library=static',  # Build static libs with PIC
    'b_staticpic=true'  # Enable PIC for later shared lib creation
  ]
)

# Import filesystem module for path checks
fs = import('fs')

# Compiler and language settings
cpp = meson.get_compiler('cpp')
cpp_std = cpp.get_argument_syntax() == 'msvc' ? 'c++20' : 'c++20'

# Detect WASM environment
is_wasm = host_machine.cpu_family() == 'wasm32' or host_machine.cpu_family() == 'wasm64'
if is_wasm
  message('Building for WebAssembly')
  add_project_arguments(['-fexceptions', '-UNDEBUG'], language: ['cpp'])
  
  # Override thread dependency to avoid -pthread in WASM
  # We use declare_dependency() so it is "found" but adds no flags
  # This prevents the linker from trying to use shared memory/atomics
  meson.override_dependency('threads', declare_dependency())
endif

# Add warning suppressions for third-party libraries (abseil, pybind11, etc.)
if cpp.get_id() == 'clang' or cpp.get_id() == 'appleclang'
  add_project_arguments([
    '-Wno-nullability-extension',       # Abseil nullability warnings
    '-Wno-gcc-compat',                  # Abseil gcc compatibility warnings
    '-Wno-gnu-zero-variadic-macro-arguments',  # GNU variadic macro warnings
  ], language: 'cpp')
endif

# Suppress pybind11 and other third-party library warnings
add_project_arguments([
  '-Wno-deprecated-declarations',
  '-Wno-unused-parameter',
  '-Wno-defaulted-function-deleted',  # Move constructor/assignment warnings
  '-Wno-unused-variable',             # Third-party unused variables
  '-Wno-unused-function',             # Third-party unused functions
  '-Wno-unused-but-set-variable',     # Third-party set but unused
  '-Wno-sign-compare',                # Third-party sign comparison warnings
  '-Wno-implicit-fallthrough',        # Third-party switch fallthrough
], language: 'cpp')

# Suppress warnings for C libraries (libjpeg-turbo, etc.)
add_project_arguments([
  '-Wno-deprecated-declarations',
  '-Wno-unused-parameter',
  '-Wno-unused-variable',
  '-Wno-unused-function',
  '-Wno-sign-compare',
  '-Wno-implicit-fallthrough',
], language: 'c')

# Suppress linker warnings about duplicate libraries
if cpp.get_id() == 'clang' or cpp.get_id() == 'appleclang'
  add_project_link_arguments([
    '-Wl,-no_warn_duplicate_libraries',  # macOS linker: suppress duplicate library warnings
  ], language: 'cpp')
endif

# Include directories
incdir = include_directories('include')

# ============================================================================
# External Dependencies
# ============================================================================

# Use subproject wraps for static linking
# Use subproject() with default_options to force static linking
# The wraps will override the dependency names automatically

# JPEG decoding dependencies
# - iSyntax currently hard-depends on jpeg-compressor (jpgd)
# - MRXS can be configured to use either jpgd or libjpeg-turbo
use_jpgd = get_option('use_jpgd')

# Always make jpgd available (required by iSyntax reader).
# The wrap handles both monorepo and copybara-translated locations.
jpgd_proj = subproject('jpeg-compressor', default_options: [
  'default_library=static',
  'warning_level=0'
])
jpgd_dep = jpgd_proj.get_variable('jpgd_dep')

if use_jpgd
  jpeg_dep = jpgd_dep
else
  libjpeg_turbo_proj = subproject('libjpeg-turbo', default_options: [
    'default_library=static',
    'prefer_static=true',
    'warning_level=0'  # Suppress all warnings from libjpeg-turbo
  ])
  jpeg_dep = dependency('libjpeg', fallback: ['libjpeg-turbo'], static: true)
endif

# Build dependencies for simpletiff and other libraries
# zstd (required by simpletiff and others) - provides 'libzstd'
zstd_opts = [
  'prefer_static=false',
  'warning_level=0',
  'default_library=static'
]

zstd_proj = subproject('zstd', default_options: zstd_opts)
zstd_dep = dependency('libzstd', fallback: ['zstd'], static: true)

# libwebp (required by simpletiff and others) - provides multiple webp libraries
libwebp_proj = subproject('libwebp', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'
])
libwebp_dep = dependency('libwebp', fallback: ['libwebp'], static: true)

# liblzma (xz)
liblzma_opts = [
  'prefer_static=false',
  'warning_level=0',
  'default_library=static'
]

liblzma_proj = subproject('liblzma', default_options: liblzma_opts)
liblzma_dep = dependency('liblzma', fallback: ['liblzma'], static: is_wasm)

# zlib (required for MRXS format decompression)
zlib_proj = subproject('zlib', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'
])
zlib_dep = dependency('zlib', fallback: ['zlib'], static: true)

# SimpleTIFF - our custom TIFF library
# Available via wrap file which handles both monorepo and copybara-translated locations
simpletiff_proj = subproject('simpletiff', default_options: [
  'default_library=static',
  'warning_level=0',
  'use_jpgd=' + (use_jpgd ? 'true' : 'false'),  # Pass jpgd option to simpletiff
  'build_tests=false',
  'build_benchmarks=false',
  'build_examples=false',  # Disable examples (especially for WASM)
  'build_python_bindings=false'
])
simpletiff_dep = simpletiff_proj.get_variable('simpletiff_dep')

# Python dependencies (for bindings)
python3 = import('python').find_installation('python3', required: false)
pybind11_proj = subproject('pybind11', default_options: [
  'default_library=static',
  'warning_level=0'
], required: false)
pybind11_dep = dependency('pybind11', fallback: ['pybind11'], required: false)

# Testing dependencies (from wrap, statically linked)
gtest_proj = subproject('gtest', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'
], required: false)
gtest_dep = dependency('gtest', fallback: ['gtest'], static: true, required: false)
gtest_main_dep = dependency('gtest_main', fallback: ['gtest'], static: true, required: false)

# ============================================================================
# Internal Dependencies (AIFO Core)
# ============================================================================

# aifocore - available via subproject (symlink in monorepo, actual directory after copybara)
# This provides the thread pool singleton and header-only utilities
aifocore_proj = subproject('aifocore', default_options: [
  'default_library=static',
  'warning_level=0'
])
aifocore_dep = aifocore_proj.get_variable('aifocore_dep')

# FastSlide only needs specific parts of aifocore (all header-only):
# 1. aifocore/concepts/numeric.h - Provides Size type used throughout fastslide
# 2. aifocore/status/result.h - Provides status handling macros (RETURN_IF_ERROR, etc.)
# 3. aifocore/utilities/fmt.h - Provides formatting utilities
#
# These are separated so only the minimal required headers need to be synced
# to public repositories. Each dependency is header-only with no runtime dependencies.
#
# Files required from aifocore for public sync:
#   - include/aifocore/concepts/numeric.h
#   - include/aifocore/status/result.h  
#   - include/aifocore/utilities/fmt.h

# Note: We need to declare fmt and cereal first before using them in aifocore deps
# (moved below after fmt and cereal are defined)

# pugixml from wrap (statically linked)
pugixml_proj = subproject('pugixml', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'
], required: false)
pugixml_dep = dependency('pugixml', fallback: ['pugixml'], static: true, required: false)

# Highway SIMD library (header-only with optional compiled components)
highway_proj = subproject('highway', default_options: [
  'default_library=static',
  'cpp_std=c++17,c++14,c++11',
  'warning_level=0'
])
highway_dep = highway_proj.get_variable('hwy_dep')

# fmt from wrap (statically linked) - required by aifocore
fmt_proj = subproject('fmt', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'
])
fmt_dep = dependency('fmt', fallback: ['fmt'], static: true)

# cereal from wrap (statically linked) - required by aifocore
cereal_proj = subproject('cereal', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'
])
cereal_dep = dependency('cereal', fallback: ['cereal'], static: true)

# ============================================================================
# Third-party vendored dependencies
# ============================================================================

# lodepng - detect location (monorepo vs copybara)
# Monorepo: ../../third_party/lodepng/upstream
# Copybara: third_party/lodepng/upstream
lodepng_base_path = ''
if fs.is_dir('../../third_party/lodepng/upstream')
  message('Found lodepng in monorepo location: ../../third_party/lodepng/upstream')
  lodepng_base_path = '../../third_party/lodepng/upstream'
elif fs.is_dir('third_party/lodepng/upstream')
  message('Found lodepng in copybara location: third_party/lodepng/upstream')
  lodepng_base_path = 'third_party/lodepng/upstream'
else
  error('lodepng not found. Please ensure it exists in ../../third_party/lodepng/upstream or third_party/lodepng/upstream')
endif

lodepng_incdir = include_directories(lodepng_base_path)
lodepng_lib = static_library('lodepng',
  lodepng_base_path / 'lodepng/lodepng.cpp',
  include_directories: lodepng_incdir,
  pic: true,
  override_options: ['warning_level=0']  # Suppress warnings for third-party code
)
lodepng_dep = declare_dependency(
  include_directories: lodepng_incdir,
  link_with: lodepng_lib
)

# CLI11 - detect location (monorepo vs copybara)
# Monorepo: ../../third_party/CLI11/upstream
# Copybara: third_party/CLI11/upstream
cli11_base_path = ''
if fs.is_dir('../../third_party/CLI11/upstream')
  message('Found CLI11 in monorepo location: ../../third_party/CLI11/upstream')
  cli11_base_path = '../../third_party/CLI11/upstream'
elif fs.is_dir('third_party/CLI11/upstream')
  message('Found CLI11 in copybara location: third_party/CLI11/upstream')
  cli11_base_path = 'third_party/CLI11/upstream'
else
  error('CLI11 not found. Please ensure it exists in ../../third_party/CLI11/upstream or third_party/CLI11/upstream')
endif

cli11_incdir = include_directories(cli11_base_path / 'include')
cli11_dep = declare_dependency(
  include_directories: cli11_incdir
)

# ============================================================================
# All Source Files (Monolithic Build)
# ============================================================================

# Collect all source files for monolithic build
src_prefix = 'src/'
fastslide_sources = [
  # Core sources
  src_prefix + 'image.cpp',
  src_prefix + 'slide_reader.cpp',
  
  # Utilities
  src_prefix + 'utilities/colors.cpp',
  src_prefix + 'utilities/hash.cpp',
  src_prefix + 'utilities/sha-256.c',
    
  # Runtime
  src_prefix + 'runtime/global_cache_manager.cpp',
  src_prefix + 'runtime/io/binary_utils.cpp',
  src_prefix + 'runtime/io/file_reader.cpp',
  src_prefix + 'runtime/lru_tile_cache.cpp',
  src_prefix + 'runtime/plugin_loader.cpp',
  src_prefix + 'runtime/reader_registry.cpp',
  src_prefix + 'runtime/register_builtin_formats.cpp',
  src_prefix + 'runtime/tile_writer.cpp',
  
  # Runtime tile writer strategies
  src_prefix + 'runtime/tile_writer/direct/direct_strategy.cpp',
  src_prefix + 'runtime/tile_writer/direct/fills.cpp',
  src_prefix + 'runtime/tile_writer/direct/copy_rgb8.cpp',
  src_prefix + 'runtime/tile_writer/direct/copy_planar.cpp',
  src_prefix + 'runtime/tile_writer/blended/blended_strategy.cpp',
  src_prefix + 'runtime/tile_writer/blended/accumulate.cpp',
  src_prefix + 'runtime/tile_writer/blended/srgb_linear.cpp',
  src_prefix + 'runtime/tile_writer/blended/resample_mks.cpp',
  src_prefix + 'runtime/tile_writer/blended/mks_kernel.cpp',
  
  # Readers
  src_prefix + 'readers/tiff_based_reader.cpp',
  
  # Aperio format
  src_prefix + 'readers/aperio/aperio_format_plugin.cpp',
  src_prefix + 'readers/aperio/metadata_parser.cpp',
  src_prefix + 'readers/aperio/aperio.cpp',
  src_prefix + 'readers/aperio/aperio_plan_builder.cpp',
  src_prefix + 'readers/aperio/aperio_tile_executor.cpp',
  
  # QPTIFF format
  src_prefix + 'readers/qptiff/metadata_parser.cpp',
  src_prefix + 'readers/qptiff/qptiff_format_plugin.cpp',
  src_prefix + 'readers/qptiff/qptiff.cpp',
  src_prefix + 'readers/qptiff/qptiff_metadata_loader.cpp',
  src_prefix + 'readers/qptiff/qptiff_plan_builder.cpp',
  src_prefix + 'readers/qptiff/qptiff_tile_executor.cpp',
  
  # MRXS format - conditional JPEG decoder
  src_prefix + 'readers/mrxs/mrxs_format_plugin.cpp',
  src_prefix + 'readers/mrxs/mrxs_ini_parser.cpp',
  src_prefix + 'readers/mrxs/mrxs.cpp',
  src_prefix + 'readers/mrxs/spatial_index.cpp',
  src_prefix + 'readers/mrxs/mrxs_plan_builder.cpp',
  src_prefix + 'readers/mrxs/mrxs_tile_executor.cpp',
  src_prefix + 'readers/mrxs/mrxs_data_reader.cpp',
  src_prefix + 'readers/mrxs/mrxs_index_reader.cpp',
]

# Add conditional JPEG decoder implementation
if use_jpgd
  fastslide_sources += src_prefix + 'readers/mrxs/mrxs_decoder_jpgd.cpp'
else
  fastslide_sources += src_prefix + 'readers/mrxs/mrxs_decoder.cpp'
endif

# iSyntax format (reader + vendored libisyntax)
isyntax_sources = [
  src_prefix + 'readers/isyntax/isyntax.cpp',
  src_prefix + 'readers/isyntax/isyntax_plan_builder.cpp',
  src_prefix + 'readers/isyntax/isyntax_tile_executor.cpp',
]

# Vendored libisyntax sources (mirror Bazel exclusions to avoid duplicate symbols).
# We list sources explicitly instead of using globbing to keep this working on
# older Meson versions.
isyntax_third_party_sources = [
  src_prefix + 'readers/isyntax/third_party/base64.cpp',
  src_prefix + 'readers/isyntax/third_party/cache.cpp',
  src_prefix + 'readers/isyntax/third_party/color.cpp',
  src_prefix + 'readers/isyntax/third_party/decompress.cpp',
  src_prefix + 'readers/isyntax/third_party/dwt.cpp',
  src_prefix + 'readers/isyntax/third_party/file.cpp',
  src_prefix + 'readers/isyntax/third_party/jpeg.cpp',
  src_prefix + 'readers/isyntax/third_party/open.cpp',
  src_prefix + 'readers/isyntax/third_party/open_helpers.cpp',
  src_prefix + 'readers/isyntax/third_party/reader.cpp',
  src_prefix + 'readers/isyntax/third_party/seektable.cpp',
  src_prefix + 'readers/isyntax/third_party/tile.cpp',
  src_prefix + 'readers/isyntax/third_party/xml_header_reader.cpp',
  src_prefix + 'readers/isyntax/third_party/xml_parser.cpp',
  src_prefix + 'readers/isyntax/third_party/xml_semantics.cpp',
  src_prefix + 'readers/isyntax/third_party/platform/platform.cpp',
  src_prefix + 'readers/isyntax/third_party/utils/arena_allocator.cpp',
  src_prefix + 'readers/isyntax/third_party/utils/block_allocator.cpp',
  src_prefix + 'readers/isyntax/third_party/utils/math_utils.cpp',
  src_prefix + 'readers/isyntax/third_party/utils/mathutils.cpp',
  src_prefix + 'readers/isyntax/third_party/third_party/yxml.c',
]

# Force `isyntax.c` to compile as C++ by copying it to a generated .cpp file.
isyntax_core_cpp = configure_file(
  input: src_prefix + 'readers/isyntax/third_party/isyntax.c',
  output: 'isyntax_core.cpp',
  copy: true
)

fastslide_sources += isyntax_sources
fastslide_sources += isyntax_third_party_sources
fastslide_sources += isyntax_core_cpp

fastslide_sources_list = fastslide_sources

# All header files
fastslide_headers = [
  'include/fastslide/fastslide.h',
  'include/fastslide/image.h',
  'include/fastslide/metadata.h',
  'include/fastslide/slide_options.h',
  'include/fastslide/slide_reader.h',
  'include/fastslide/associated_data.h',
  'include/fastslide/core/metadata.h',
  'include/fastslide/core/slide_descriptor.h',
  'include/fastslide/core/tile_plan.h',
  'include/fastslide/core/tile_request.h',
  'include/fastslide/runtime/cache_interface.h',
  'include/fastslide/runtime/format_descriptor.h',
  'include/fastslide/runtime/global_cache_manager.h',
  'include/fastslide/runtime/lru_tile_cache.h',
  'include/fastslide/runtime/plugin_loader.h',
  'include/fastslide/runtime/reader_dependencies.h',
  'include/fastslide/runtime/reader_registry.h',
  'include/fastslide/runtime/tile_writer.h',
  'include/fastslide/runtime/io/binary_utils.h',
  'include/fastslide/utilities/cache.h',
  'include/fastslide/utilities/colors.h',
  'include/fastslide/utilities/hash.h',
  'include/fastslide/utilities/tile_cache_manager.h',
  'include/fastslide/utilities/tiff/directory_processor.h',
  'include/fastslide/utilities/tiff/planar_interleaver.h',
  'include/fastslide/utilities/tiff/tiff_cache_service.h',
  'include/fastslide/utilities/tiff/tiff_file.h',
  'include/fastslide/utilities/tiff/tiff_pool.h',
  'include/fastslide/utilities/tiff/tile_utilities.h',
  'include/fastslide/readers/readers.h',
  'include/fastslide/readers/aperio/aperio_format_plugin.h',
  'include/fastslide/readers/aperio/metadata_parser.h',
  'include/fastslide/readers/qptiff/metadata_parser.h',
  'include/fastslide/readers/qptiff/qptiff_format_plugin.h',
  'include/fastslide/readers/mrxs/mrxs_decoder.h',
  'include/fastslide/readers/mrxs/mrxs_format_plugin.h',
  'include/fastslide/readers/mrxs/mrxs_ini_parser.h',
  'include/fastslide/readers/reader_factory.h',
  'include/fastslide/readers/tiff_based_reader.h',
  'include/fastslide/readers/tiff_reader_factory.h',
  'include/fastslide/readers/aperio/aperio.h',
  'include/fastslide/readers/qptiff/qptiff.h',
  'include/fastslide/readers/qptiff/qptiff_metadata_loader.h',
  'include/fastslide/readers/qptiff/qptiff_plan_builder.h',
  'include/fastslide/readers/qptiff/qptiff_tile_executor.h',
  'include/fastslide/readers/mrxs/mrxs.h',
  'include/fastslide/readers/mrxs/spatial_index.h',
  'include/fastslide/readers/mrxs/mrxs_internal.h',
  'include/fastslide/readers/mrxs/mrxs_plan_builder.h',
  'include/fastslide/readers/mrxs/mrxs_tile_executor.h',
  'include/fastslide/readers/isyntax/isyntax.h',
  'include/fastslide/readers/isyntax/isyntax_plan_builder.h',
  'include/fastslide/readers/isyntax/isyntax_tile_executor.h',
]

# ============================================================================
# Main FastSlide Shared Library (Monolithic)
# ============================================================================

# Build both static and shared versions explicitly
# Static library (all deps embedded)
# Add parent directories to include path for Highway's foreach_target.h
# which needs to find 'aifo/fastslide/src/...' paths
fastslide_static = static_library('fastslide',
  fastslide_sources,
  include_directories: [
    incdir,
    lodepng_incdir,
    include_directories('../..'),  # Access aifo/fastslide/... paths for Highway
    include_directories('src/readers/isyntax/third_party'),
    include_directories('src/readers/isyntax/third_party/platform'),
    include_directories('src/readers/isyntax/third_party/third_party'),
    include_directories('src/readers/isyntax/third_party/utils'),
  ],
  dependencies: [
    # Internal dependencies
    aifocore_dep,
    # External dependencies
    simpletiff_dep,  # Using simpletiff instead of libtiff
    zstd_dep,
    libwebp_dep,
    liblzma_dep,
    zlib_dep,
    jpeg_dep,
    jpgd_dep,  # Required by iSyntax (jpeg-compressor/jpgd)
    pugixml_dep,
    fmt_dep,
    cereal_dep,
    lodepng_dep,
    highway_dep,
  ],
  pic: true,
  install: false  # Static library only used for building, not installed
)

if is_wasm
  # WASM builds only use static library
  fastslide_lib = fastslide_static
else
  # Shared library (all deps embedded)
  # Add parent directories to include path for Highway's foreach_target.h
  fastslide_shared = shared_library('fastslide',
    fastslide_sources,
    include_directories: [
      incdir,
      lodepng_incdir,
      include_directories('../..'),  # Access aifo/fastslide/... paths for Highway
      include_directories('src/readers/isyntax/third_party'),
      include_directories('src/readers/isyntax/third_party/platform'),
      include_directories('src/readers/isyntax/third_party/third_party'),
      include_directories('src/readers/isyntax/third_party/utils'),
    ],
    dependencies: [
      # Internal dependencies
      aifocore_dep,
      # External dependencies
      simpletiff_dep,
      zstd_dep,
      libwebp_dep,
      liblzma_dep,
      zlib_dep,
      jpeg_dep,
      jpgd_dep,  # Required by iSyntax (jpeg-compressor/jpgd)
      pugixml_dep,
      fmt_dep,
      cereal_dep,
      lodepng_dep,
      highway_dep,
      # Internal dependencies (header-only)
    ],
    install: true,
    version: meson.project_version(),
    soversion: '2',
    install_dir: python3.found() ? python3.get_install_dir() / 'fastslide' : get_option('libdir')
  )

  # Use shared library as default for linking
  fastslide_lib = fastslide_shared
endif

# ============================================================================
# C API Library
# ============================================================================

# C API sources
c_api_sources = [
  src_prefix + 'c/fastslide.cpp',
  src_prefix + 'c/image.cpp',
  src_prefix + 'c/registry.cpp',
  src_prefix + 'c/slide_reader.cpp',
  src_prefix + 'c/utilities.cpp',
]

# C API headers
c_api_headers = [
  'include/fastslide/c/fastslide.h',
  'include/fastslide/c/image.h',
  'include/fastslide/c/registry.h',
  'include/fastslide/c/slide_reader.h',
  'include/fastslide/c/utilities.h',
]

# C API library dependencies
c_api_deps = [
  aifocore_dep,
  fmt_dep,
  cereal_dep,
  zstd_dep,
  lodepng_dep,  # For PNG I/O
  highway_dep,
]

# C API library
fastslide_c_lib = library('fastslide_c',
  c_api_sources,
  include_directories: [incdir, lodepng_incdir],
  dependencies: c_api_deps,
  link_with: fastslide_lib,
  install: false,  # C API only for development, not installed in Python package
  version: meson.project_version(),
  soversion: '2'
)

# ============================================================================
# Python Bindings
# ============================================================================

# Python binding sources
python_sources = [
  src_prefix + 'python/cache.cpp',
  src_prefix + 'python/fastslide.cpp',
  src_prefix + 'python/reader.cpp',
]

# Python binding headers
python_headers = [
  'include/fastslide/python/cache.h',
  'include/fastslide/python/reader.h',
]

# Python extension module (statically linked)
if not is_wasm and python3.found() and pybind11_dep.found()
  # Use shared_module for Python extensions
  # Note: Python extensions need explicit dependencies and includes since they don't get them from link_with
  # On macOS, we need to explicitly export the PyInit symbol
  # The module name is _fastslide' per PYBIND11_MODULE
  python_link_args = []
  if host_machine.system() == 'darwin'
    python_link_args = ['-Wl,-exported_symbol,_PyInit__fastslide', '-Wl,-rpath,@loader_path']
  else
    # On Linux, set rpath to look in the same directory as the extension module
    python_link_args = ['-Wl,-rpath,$ORIGIN']
  endif
  
  fastslide_python_ext = python3.extension_module('_fastslide',
    python_sources,
    include_directories: [incdir],
  dependencies: [
      pybind11_dep,
      aifocore_dep,
      fmt_dep,
      cereal_dep,
      simpletiff_dep,  # Using simpletiff instead of libtiff
      zstd_dep,
      libwebp_dep,
      liblzma_dep,
      zlib_dep,
      highway_dep,
    ],
    link_args: python_link_args,
    link_with: fastslide_lib,
    install: true,
    install_rpath: '$ORIGIN',
    subdir: 'fastslide'
  )
  
  # Install Python __init__.py
  python3.install_sources(
    'python/fastslide/__init__.py',
    subdir: 'fastslide'
  )
endif

# ============================================================================
# Executables
# ============================================================================

# WASM Wrapper
if is_wasm
  executable('fastslide_multiplex',
    'wasm/fastslide_wrapper.cpp',
    include_directories: [incdir],
    dependencies: [
      aifocore_dep,
      fmt_dep,
      cereal_dep,
      simpletiff_dep,
      zstd_dep,
      libwebp_dep,
      liblzma_dep,
      zlib_dep,
      highway_dep,
    ],
    link_with: fastslide_lib,
    link_args: [
      '-sWASM=1',
      '-sEXPORT_ES6=1',
      '-sMODULARIZE=1',
      '-sENVIRONMENT=web,worker,node',
      '-sNO_EXIT_RUNTIME=1',
      '-sALLOW_MEMORY_GROWTH=1',
      '-sINITIAL_MEMORY=134217728',
      '-sMAXIMUM_MEMORY=4294967296',
      '-sFILESYSTEM=1',
      '-sFORCE_FILESYSTEM=1',
      '-sEXPORT_NAME=createFastSlideModule',
      '-sEXPORTED_RUNTIME_METHODS=[\'FS\',\'WORKERFS\']',
      '-lembind',
      '-lworkerfs.js',
      '-fexceptions',
      '-sASSERTIONS=1',
      '-sDISABLE_EXCEPTION_CATCHING=0',
      '-sUSE_PTHREADS=0',  # Explicitly disable pthreads
      '-O2'
    ],
    name_suffix: 'js',
    install: true
  )
endif

# Slide information tool (not built for WASM)
if not is_wasm
  slidetool_exe = executable('fastslidetool',
    'tools/fastslidetool.cpp',
    include_directories: [incdir],
    dependencies: [
      aifocore_dep,
      fmt_dep,
      cereal_dep,
      lodepng_dep,
      highway_dep,
      cli11_dep
    ],
    link_with: fastslide_lib,
    install: true
  )
endif

# ============================================================================
# Tests (disabled for WASM builds)
# ============================================================================

if not is_wasm and gtest_dep.found() and gtest_main_dep.found()
  # Common test dependencies
  test_deps = [
    gtest_dep,
    gtest_main_dep,
    aifocore_dep,
    fmt_dep,
    cereal_dep,
    zstd_dep,
    libwebp_dep,
    liblzma_dep,
    zlib_dep,
    simpletiff_dep,
    highway_dep,
  ]
  
  # Core tests
  image_test = executable('image_test',
    src_prefix + 'image_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('image_test', image_test)

  slide_reader_test = executable('slide_reader_test',
    src_prefix + 'slide_reader_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('slide_reader_test', slide_reader_test)

  channel_selection_test = executable('channel_selection_test',
    src_prefix + 'channel_selection_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('channel_selection_test', channel_selection_test)

  # Core component tests
  tile_request_test = executable('tile_request_test',
    src_prefix + 'core/tile_request_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tile_request_test', tile_request_test)

  tile_plan_test = executable('tile_plan_test',
    src_prefix + 'core/tile_plan_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tile_plan_test', tile_plan_test)

  region_conversion_test = executable('region_conversion_test',
    src_prefix + 'core/region_conversion_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('region_conversion_test', region_conversion_test)

  # Runtime tests
  tile_writer_test = executable('tile_writer_test',
    src_prefix + 'runtime/tile_writer_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tile_writer_test', tile_writer_test)

  reader_registry_test = executable('reader_registry_test',
    src_prefix + 'runtime/reader_registry_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('reader_registry_test', reader_registry_test)

  lru_tile_cache_test = executable('lru_tile_cache_test',
    src_prefix + 'runtime/lru_tile_cache_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('lru_tile_cache_test', lru_tile_cache_test)

  cache_types_test = executable('cache_types_test',
    src_prefix + 'runtime/cache_types_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('cache_types_test', cache_types_test)

  # Format tests
  aperio_metadata_parser_test = executable('aperio_metadata_parser_test',
    src_prefix + 'readers/aperio/metadata_parser_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('aperio_metadata_parser_test', aperio_metadata_parser_test)

  qptiff_metadata_parser_test = executable('qptiff_metadata_parser_test',
    src_prefix + 'readers/qptiff/metadata_parser_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('qptiff_metadata_parser_test', qptiff_metadata_parser_test)
endif
