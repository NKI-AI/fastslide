project(
  'fastslide',
  'cpp',
  version : '0.1.0',
  license : 'LGPL-2.1-only',
  default_options : [
    'buildtype=debugoptimized',
    'cpp_std=c++17',  # Upgraded from c++20 to ensure compatibility with filesystem
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
  ]),
  language : 'cpp'
)

# Dependencies
openslide_dep = dependency('openslide')
filesystem_dep = dependency('threads') # For std::filesystem support

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
  dependencies : [openslide_dep],
  install : true,
  include_directories : incdir,
)

# Provide fastslide dependency for other subprojects
fastslide_dep = declare_dependency(
  link_with : fastslide_lib,
  include_directories : incdir,
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
  description : 'Modern C++20 wrapper for OpenSlide library',
  url : 'https://github.com/yourusername/fastslide',
)
