project('jpeg-compressor', 'cpp',
  version: '1.0.0',
  default_options: ['cpp_std=c++17', 'warning_level=2']
)

# Get dependencies from subprojects
hwy_dep = dependency('hwy', fallback: ['highway', 'hwy_dep'])
gtest_dep = dependency('gtest', fallback: ['gtest', 'gtest_dep'])
gtest_main_dep = dependency('gtest_main', fallback: ['gtest', 'gtest_main_dep'])

# jpgd_idct_highway library
# Add parent directories to include path for Highway's foreach_target.h
# which needs to find 'aifo/jpeg-compressor/jpgd_idct_highway.cpp'
jpgd_idct_highway_lib = static_library('jpgd_idct_highway',
  sources: ['jpgd_idct_highway.cpp'],
  dependencies: [hwy_dep],
  override_options: ['cpp_std=c++11'],
  include_directories: [
    include_directories('.'),
    include_directories('../..'),  # Access aifo/jpeg-compressor/...
  ],
  install: false
)

jpgd_idct_highway_dep = declare_dependency(
  link_with: jpgd_idct_highway_lib,
  include_directories: include_directories('.'),
  dependencies: [hwy_dep]
)

# jpgd_color_highway library
# Add parent directories to include path for Highway's foreach_target.h
# which needs to find 'aifo/jpeg-compressor/jpgd_color_highway.cpp'
jpgd_color_highway_lib = static_library('jpgd_color_highway',
  sources: ['jpgd_color_highway.cpp'],
  dependencies: [hwy_dep],
  override_options: ['cpp_std=c++11'],
  include_directories: [
    include_directories('.'),
    include_directories('../..'),  # Access aifo/jpeg-compressor/...
  ],
  install: false
)

jpgd_color_highway_dep = declare_dependency(
  link_with: jpgd_color_highway_lib,
  include_directories: include_directories('.'),
  dependencies: [hwy_dep]
)

# jpgd main library
jpgd_lib = static_library('jpgd',
  sources: ['jpgd.cpp'],
  dependencies: [jpgd_color_highway_dep, jpgd_idct_highway_dep],
  cpp_args: [
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-unused-variable'
  ],
  include_directories: include_directories('.'),
  install: false
)

jpgd_dep = declare_dependency(
  link_with: jpgd_lib,
  include_directories: include_directories('..'),  # Parent directory to allow jpeg-compressor/ prefix
  dependencies: [jpgd_color_highway_dep, jpgd_idct_highway_dep]
)

# Tests
idct_test_exe = executable('idct_test',
  sources: ['idct_test.cpp'],
  dependencies: [jpgd_idct_highway_dep, gtest_main_dep],
  install: false
)

test('idct_test', idct_test_exe)

color_conversion_test_exe = executable('color_conversion_test',
  sources: ['color_conversion_test.cpp'],
  dependencies: [jpgd_color_highway_dep, gtest_main_dep],
  install: false
)

test('color_conversion_test', color_conversion_test_exe)

