project('lodepng', 'cpp',
  version : '0.0.0',
  default_options : ['warning_level=0', 'cpp_std=c++17'])

# lodepng ships a single translation unit. Build it as a static library.
lodepng_lib = static_library('lodepng',
  'lodepng.cpp',
  cpp_args : ['-w'],
  pic : true,
  install : false)

# include_directories('.') exposes both "lodepng.h" and, via the shim added by
# the packagefiles overlay at lodepng/lodepng.h, the prefixed
# "lodepng/lodepng.h" form used by fastslide.
lodepng_dep = declare_dependency(
  link_with : lodepng_lib,
  include_directories : include_directories('.'))

meson.override_dependency('lodepng', lodepng_dep)
