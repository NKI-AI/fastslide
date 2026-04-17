project('aifocore', 'cpp',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

# Dependencies
threads_dep = dependency('threads')
fmt_dep = dependency('fmt', fallback : ['fmt', 'fmt_dep'])
cereal_dep = dependency('cereal', fallback : ['cereal', 'cereal_dep'])

# Include directories
inc = include_directories('include')

# Core library sources (only the compiled parts, rest is header-only)
aifocore_sources = [
  'src/utilities/thread_pool_singleton.cpp',
]

# Build the library
aifocore_lib = static_library('aifocore',
  aifocore_sources,
  include_directories : inc,
  dependencies : [threads_dep, fmt_dep, cereal_dep],
  install : false)

# Make library usable as dependency
aifocore_dep = declare_dependency(
  include_directories : inc,
  link_with : aifocore_lib,
  dependencies : [fmt_dep, cereal_dep]
)

meson.override_dependency('aifocore', aifocore_dep)

