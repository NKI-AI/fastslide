# Try to find gtest, fall back to subproject if not found
gtest_dep = dependency('gtest',
  main : true,
  fallback : ['gtest', 'gtest_main_dep'],
  required : get_option('build_tests'))

if gtest_dep.found()
  # Tests now colocated with source files
  test_index = executable('test_index',
    '../src/index_test.cpp',
    dependencies : [simpletiff_dep, gtest_dep])

  test('index_test', test_index)

  test_lzw = executable('test_lzw',
    '../src/lzw_test.cpp',
    dependencies : [simpletiff_dep, gtest_dep])

  test('lzw_test', test_lzw)

  test_predictor = executable('test_predictor',
    '../src/predictor_test.cpp',
    dependencies : [simpletiff_dep, gtest_dep])

  test('predictor_test', test_predictor)

  test_reader = executable('test_reader',
    '../src/reader_test.cpp',
    dependencies : [simpletiff_dep, gtest_dep])

  test('reader_test', test_reader)
endif
