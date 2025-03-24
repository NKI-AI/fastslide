project(
  'fastslide',
  'cpp',
  version : '0.1.0',
  license : 'LGPL-2.1-only',
  default_options : [
    'buildtype=debugoptimized',
    'cpp_std=c++17',  # Use C++17 for better compatibility with Apple Clang
    'warning_level=3',
  ],
)

# Add compiler flags
cpp = meson.get_compiler('cpp')
add_project_arguments(
  cpp.get_supported_arguments([
    '-Wall',
    '-Wextra',
    '-pedantic',
    '-Wno-c++11-extensions',  # Suppress C++11 extension warnings
    '-Wno-c++17-extensions',  # Suppress C++17 extension warnings for structured bindings
    '-Wno-deprecated-declarations',  # Suppress deprecation warnings
    '-Wno-unused-parameter',  # Suppress unused parameter warnings
  ]),
  language : 'cpp'
)

# Dependencies
openslide_dep = dependency('openslide')
glib_dep = dependency('glib-2.0')  # Required for GError and other GLib types
filesystem_dep = dependency('threads') # For std::filesystem support

# Try to find fmt as a system dependency, if not available use subproject
fmt_dep = dependency('fmt', required : false)
if not fmt_dep.found()
  fmt_proj = subproject('fmt')
  fmt_dep = fmt_proj.get_variable('fmt_dep')
endif

# Add compiler flags for fmt
add_project_arguments('-DFMT_USE_CONSTEXPR=0', language : 'cpp')  # Disable constexpr usage in fmt

# Add a define to indicate we want to use OpenSlide internal APIs (risky)
add_project_arguments('-D_OPENSLIDE_ALLOW_INTERNAL_ACCESS=1', language : 'cpp')

# Include directories
incdir = include_directories('src/cpp')

# Library sources
fastslide_sources = [
  'src/cpp/fastslide.cpp',
]

# Library build
fastslide_lib = library(
  'fastslide',
  fastslide_sources,
  dependencies : [openslide_dep, glib_dep, fmt_dep],
  install : true,
  include_directories : incdir,
)

# Provide fastslide dependency for other subprojects
fastslide_dep = declare_dependency(
  link_with : fastslide_lib,
  include_directories : incdir,
  dependencies : [fmt_dep],
)

# Test executable
test_exe = executable(
  'test_fastslide',
  'test_fastslide.cpp',
  dependencies : [fastslide_dep, filesystem_dep],
  install : true,
)

# Install headers
install_headers('src/cpp/fastslide.h')

# Generate pkg-config file
pkg = import('pkgconfig')
pkg.generate(
  fastslide_lib,
  description : 'Modern C++17 wrapper for OpenSlide library',
  url : 'https://github.com/yourusername/fastslide',
)
