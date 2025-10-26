# FastSlide Meson Build File
# Modern Meson build system for FastSlide library with static linking

project('fastslide', 'cpp', 'c',
  version: '0.1.0',
  license: 'Apache-2.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'werror=false',
    'default_library=static',  # Build static libs with PIC
    'b_staticpic=true'  # Enable PIC for later shared lib creation
  ]
)

# Compiler and language settings
cpp = meson.get_compiler('cpp')
cpp_std = cpp.get_argument_syntax() == 'msvc' ? 'c++20' : 'c++20'

# Add warning suppressions for third-party libraries (abseil, pybind11, libtiff, etc.)
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

# Suppress warnings for C libraries (libtiff, libjpeg-turbo, etc.)
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
aifocore_incdir = include_directories('include')  # aifocore headers are in the same include dir

# ============================================================================
# External Dependencies
# ============================================================================

# Use subproject wraps for static linking
# Use subproject() with default_options to force static linking
# The wraps will override the dependency names automatically

# libjpeg-turbo from wrap (statically linked)
# The wrap provides 'libjpeg' as dependency_names
libjpeg_turbo_proj = subproject('libjpeg-turbo', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'  # Suppress all warnings from libjpeg-turbo
])
jpeg_dep = dependency('libjpeg', fallback: ['libjpeg-turbo'], static: true)

# abseil-cpp from wrap (statically linked)
abseil_proj = subproject('abseil-cpp', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'  # Suppress all warnings from abseil
])
absl_log_dep = dependency('absl_log', fallback: ['abseil-cpp'], static: true)
absl_status_dep = dependency('absl_status', fallback: ['abseil-cpp'], static: true)
absl_strings_dep = dependency('absl_strings', fallback: ['abseil-cpp'], static: true)
absl_flags_dep = dependency('absl_flags', fallback: ['abseil-cpp'], static: true)

# Build libtiff dependencies BEFORE libtiff and force them as fallbacks
# This ensures libtiff uses our wrapped static versions instead of system libs

# Step 1: Build all dependencies that libtiff needs
# zstd (required by libtiff) - provides 'libzstd'
zstd_proj = subproject('zstd', default_options: [
  'default_library=shared',
  'prefer_static=false',
  'warning_level=0'
])
zstd_dep = dependency('libzstd', fallback: ['zstd'], static: false)

# libwebp (required by libtiff) - provides multiple webp libraries
libwebp_proj = subproject('libwebp', default_options: [
  'default_library=static',
  'prefer_static=true',
  'warning_level=0'
])
libwebp_dep = dependency('libwebp', fallback: ['libwebp'], static: true)

# liblzma (xz)
liblzma_proj = subproject('liblzma', default_options: [
  'default_library=shared',
  'prefer_static=false',
  'warning_level=0'
])
liblzma_dep = dependency('liblzma', fallback: ['liblzma'], static: false)

# Step 2: Build libtiff with wrap-mode=forcefallback for its dependencies
# This tells libtiff to use subprojects instead of system libraries
libtiff_proj = subproject('libtiff', default_options: [
  'default_library=static',
  'prefer_static=true',
  'zstd=enabled',
  'webp=enabled',
  'jpeg=enabled',
  'lzma=enabled',
  'jbig=disabled',
  'lerc=disabled',
  'wrap_mode=forcefallback',  # Force using subprojects for dependencies
  'warning_level=0'  # Suppress all warnings from libtiff
])
libtiff_dep = dependency('libtiff-4', fallback: ['libtiff'], static: true)

# zlib (required by libtiff and others) - use wrap subproject
zlib_proj = subproject('zlib', default_options: [
  'default_library=shared',
  'warning_level=0'
])
zlib_dep = zlib_proj.get_variable('zlib_dep')


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

# FastSlide only needs specific parts of aifocore (all header-only):
# 1. aifocore/concepts/numeric.h - Provides Size type used throughout fastslide
# 2. aifocore/status/status_macros.h - Provides status handling macros (RETURN_IF_ERROR, etc.)
# 3. aifocore/utilities/fmt.h - Provides formatting utilities
#
# These are separated so only the minimal required headers need to be synced
# to public repositories. Each dependency is header-only with no runtime dependencies.
#
# Files required from aifocore for public sync:
#   - include/aifocore/concepts/numeric.h
#   - include/aifocore/status/status_macros.h  
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

# Now define aifocore dependencies with fmt and cereal
aifocore_concepts_dep = declare_dependency(
  include_directories: incdir,
  dependencies: [fmt_dep, cereal_dep],
  compile_args: ['-DAIFOCORE_CONCEPTS_ONLY']
)

aifocore_status_dep = declare_dependency(
  include_directories: incdir,
  compile_args: ['-DAIFOCORE_STATUS_ONLY']
)

aifocore_utilities_dep = declare_dependency(
  include_directories: incdir,
  dependencies: [fmt_dep],
  compile_args: ['-DAIFOCORE_UTILITIES_ONLY']
)

# Combined dependency for convenience
aifocore_dep = declare_dependency(
  include_directories: incdir,
  dependencies: [aifocore_concepts_dep, aifocore_status_dep, aifocore_utilities_dep, fmt_dep, cereal_dep]
)

# ============================================================================
# Third-party vendored dependencies
# ============================================================================

lodepng_incdir = include_directories('third_party/lodepng/upstream')
lodepng_lib = static_library('lodepng',
  'third_party/lodepng/upstream/lodepng/lodepng.cpp',
  include_directories: lodepng_incdir,
  pic: true,
  override_options: ['warning_level=0']  # Suppress warnings for third-party code
)
lodepng_dep = declare_dependency(
  include_directories: lodepng_incdir,
  link_with: lodepng_lib
)

# ============================================================================
# All Source Files (Monolithic Build)
# ============================================================================

# Collect all source files for monolithic build
fastslide_sources = [
  # aifocore sources (needed for ThreadPoolManager)
  'src/aifocore/utilities/thread_pool_singleton.cc',
  
  # Core sources
  'src/fastslide/image.cpp',
  'src/fastslide/slide_reader.cpp',
  
  # Utilities
  'src/fastslide/utilities/cache.cpp',
  'src/fastslide/utilities/colors.cpp',
  'src/fastslide/utilities/hash.cpp',
  'src/fastslide/utilities/sha-256.c',
  'src/fastslide/utilities/tile_cache_manager.cpp',
  
  # TIFF utilities
  'src/fastslide/utilities/tiff/directory_processor.cpp',
  'src/fastslide/utilities/tiff/planar_interleaver.cpp',
  'src/fastslide/utilities/tiff/tiff_cache_service.cpp',
  'src/fastslide/utilities/tiff/tiff_file.cpp',
  'src/fastslide/utilities/tiff/tiff_pool.cpp',
  'src/fastslide/utilities/tiff/tile_utilities.cpp',
  
  # Runtime
  'src/fastslide/runtime/global_cache_manager.cpp',
  'src/fastslide/runtime/io/binary_utils.cpp',
  'src/fastslide/runtime/io/file_reader.cpp',
  'src/fastslide/runtime/lru_tile_cache.cpp',
  'src/fastslide/runtime/plugin_loader.cpp',
  'src/fastslide/runtime/reader_registry.cpp',
  'src/fastslide/runtime/register_builtin_formats.cpp',
  'src/fastslide/runtime/tile_writer.cpp',
  
  # Runtime tile writer strategies
  'src/fastslide/runtime/tile_writer/direct/direct_strategy.cpp',
  'src/fastslide/runtime/tile_writer/direct/fills.cpp',
  'src/fastslide/runtime/tile_writer/direct/copy_rgb8.cpp',
  'src/fastslide/runtime/tile_writer/direct/copy_planar.cpp',
  'src/fastslide/runtime/tile_writer/blended/blended_strategy.cpp',
  'src/fastslide/runtime/tile_writer/blended/accumulate.cpp',
  'src/fastslide/runtime/tile_writer/blended/srgb_linear.cpp',
  'src/fastslide/runtime/tile_writer/blended/resample_mks.cpp',
  'src/fastslide/runtime/tile_writer/blended/mks_kernel.cpp',
  
  # Readers
  'src/fastslide/readers/tiff_based_reader.cpp',
  
  # Aperio format
  'src/fastslide/readers/aperio/aperio_format_plugin.cpp',
  'src/fastslide/readers/aperio/metadata_parser.cpp',
  'src/fastslide/readers/aperio/aperio.cpp',
  'src/fastslide/readers/aperio/aperio_plan_builder.cpp',
  'src/fastslide/readers/aperio/aperio_tile_executor.cpp',
  
  # QPTIFF format
  'src/fastslide/readers/qptiff/metadata_parser.cpp',
  'src/fastslide/readers/qptiff/qptiff_format_plugin.cpp',
  'src/fastslide/readers/qptiff/qptiff.cpp',
  'src/fastslide/readers/qptiff/qptiff_metadata_loader.cpp',
  'src/fastslide/readers/qptiff/qptiff_plan_builder.cpp',
  'src/fastslide/readers/qptiff/qptiff_tile_executor.cpp',
  
  # MRXS format
  'src/fastslide/readers/mrxs/mrxs_decoder.cpp',
  'src/fastslide/readers/mrxs/mrxs_format_plugin.cpp',
  'src/fastslide/readers/mrxs/mrxs_ini_parser.cpp',
  'src/fastslide/readers/mrxs/mrxs.cpp',
  'src/fastslide/readers/mrxs/spatial_index.cpp',
  'src/fastslide/readers/mrxs/mrxs_plan_builder.cpp',
  'src/fastslide/readers/mrxs/mrxs_tile_executor.cpp',
  'src/fastslide/readers/mrxs/mrxs_data_reader.cpp',
  'src/fastslide/readers/mrxs/mrxs_index_reader.cpp',
]

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
]

# ============================================================================
# Main FastSlide Shared Library (Monolithic)
# ============================================================================

# Build both static and shared versions explicitly
# Static library (all deps embedded)
fastslide_static = static_library('fastslide',
  fastslide_sources,
  include_directories: [incdir, lodepng_incdir],
  dependencies: [
    # External dependencies
    absl_log_dep,
    absl_status_dep,
    absl_strings_dep,
    libtiff_dep,
    zstd_dep,
    libwebp_dep,
    liblzma_dep,
    zlib_dep,
    jpeg_dep,
    pugixml_dep,
    fmt_dep,
    cereal_dep,
    lodepng_dep,
    highway_dep,
  ],
  pic: true,
  install: false  # Static library only used for building, not installed
)

# Shared library (all deps embedded)
fastslide_shared = shared_library('fastslide',
  fastslide_sources,
  include_directories: [incdir, lodepng_incdir],
  dependencies: [
    # External dependencies
    absl_log_dep,
    absl_status_dep,
    absl_strings_dep,
    libtiff_dep,
    zstd_dep,
    libwebp_dep,
    liblzma_dep,
    zlib_dep,
    jpeg_dep,
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

# ============================================================================
# C API Library
# ============================================================================

# C API sources
c_api_sources = [
  'src/fastslide/c/fastslide.cpp',
  'src/fastslide/c/image.cpp',
  'src/fastslide/c/registry.cpp',
  'src/fastslide/c/slide_reader.cpp',
  'src/fastslide/c/utilities.cpp',
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
  absl_log_dep,
  absl_status_dep,
  absl_strings_dep,
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
  'src/fastslide/python/cache.cpp',
  'src/fastslide/python/fastslide.cpp',
  'src/fastslide/python/reader.cpp',
]

# Python binding headers
python_headers = [
  'include/fastslide/python/cache.h',
  'include/fastslide/python/reader.h',
]

# Python extension module (statically linked)
if python3.found() and pybind11_dep.found()
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
      absl_log_dep,
      absl_status_dep,
      absl_strings_dep,
      fmt_dep,
      cereal_dep,
      libtiff_dep,
      zstd_dep,
      libwebp_dep,
      liblzma_dep,
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

# Slide information tool
slidetool_exe = executable('fastslidetool',
  'tools/fastslidetool.cpp',
  include_directories: [incdir],
  dependencies: [
    absl_flags_dep,
    absl_log_dep,
    fmt_dep,
    cereal_dep,
    lodepng_dep,
    highway_dep
  ],
  link_with: fastslide_lib,
  install: true
)

# ============================================================================
# Tests
# ============================================================================

if gtest_dep.found() and gtest_main_dep.found()
  # Common test dependencies
  test_deps = [
    gtest_dep,
    gtest_main_dep,
    absl_log_dep,
    absl_status_dep,
    absl_strings_dep,
    fmt_dep,
    cereal_dep,
    zstd_dep,
    libwebp_dep,
    liblzma_dep,
    libtiff_dep,
    highway_dep,
  ]
  
  # Core tests
  image_test = executable('image_test',
    'src/fastslide/image_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('image_test', image_test)

  slide_reader_test = executable('slide_reader_test',
    'src/fastslide/slide_reader_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('slide_reader_test', slide_reader_test)

  channel_selection_test = executable('channel_selection_test',
    'src/fastslide/channel_selection_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('channel_selection_test', channel_selection_test)

  # Core component tests
  tile_request_test = executable('tile_request_test',
    'src/fastslide/core/tile_request_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tile_request_test', tile_request_test)

  tile_plan_test = executable('tile_plan_test',
    'src/fastslide/core/tile_plan_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tile_plan_test', tile_plan_test)

  region_conversion_test = executable('region_conversion_test',
    'src/fastslide/core/region_conversion_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('region_conversion_test', region_conversion_test)

  # Runtime tests
  tile_writer_test = executable('tile_writer_test',
    'src/fastslide/runtime/tile_writer_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tile_writer_test', tile_writer_test)

  reader_registry_test = executable('reader_registry_test',
    'src/fastslide/runtime/reader_registry_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('reader_registry_test', reader_registry_test)

  lru_tile_cache_test = executable('lru_tile_cache_test',
    'src/fastslide/runtime/lru_tile_cache_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('lru_tile_cache_test', lru_tile_cache_test)

  cache_types_test = executable('cache_types_test',
    'src/fastslide/runtime/cache_types_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('cache_types_test', cache_types_test)

  # Utilities tests
  cache_test = executable('cache_test',
    'src/fastslide/utilities/cache_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('cache_test', cache_test)

  tiff_pool_test = executable('tiff_pool_test',
    'src/fastslide/utilities/tiff/tiff_pool_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tiff_pool_test', tiff_pool_test)

  tiff_file_test = executable('tiff_file_test',
    'src/fastslide/utilities/tiff/tiff_file_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tiff_file_test', tiff_file_test)

  tile_utilities_test = executable('tile_utilities_test',
    'src/fastslide/utilities/tiff/tile_utilities_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('tile_utilities_test', tile_utilities_test)

  # Format tests
  aperio_metadata_parser_test = executable('aperio_metadata_parser_test',
    'src/fastslide/readers/aperio/metadata_parser_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('aperio_metadata_parser_test', aperio_metadata_parser_test)

  qptiff_metadata_parser_test = executable('qptiff_metadata_parser_test',
    'src/fastslide/readers/qptiff/metadata_parser_test.cpp',
    include_directories: [incdir],
    dependencies: test_deps,
    link_with: fastslide_lib
  )
  test('qptiff_metadata_parser_test', qptiff_metadata_parser_test)
endif

# ============================================================================
# Installation
# ============================================================================
# Only install runtime components (shared lib, tool, Python extension)
# Development files (headers, static libs, pkg-config) are not installed
# to keep the Python package lightweight

# NOTE: Headers and pkg-config files are intentionally not installed.
# This is a Python package first, and the C++ library is embedded.
# If you need the C++ development files, install from source with:
#   meson setup builddir -Dinstall_dev_files=true
