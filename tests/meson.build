gtest_dep = dependency('gtest',
  main : true,
  fallback : ['gtest', 'gtest_main_dep'],
  required : get_option('build_tests'))

if gtest_dep.found()
  # `testing/minimal_slide_reader.h` is included with a `testing/` prefix
  # (Bazel strip_include_prefix = "src").
  src_inc = include_directories('../src')
  test_inc = [inc, src_inc]

  # name -> source path (relative to this tests/ directory).
  fastslide_tests = {
    'image_test' : '../src/image_test.cpp',
    'slide_reader_test' : '../src/slide_reader_test.cpp',
    'tile_request_test' : '../src/core/tile_request_test.cpp',
    'tile_plan_test' : '../src/core/tile_plan_test.cpp',
    'region_conversion_test' : '../src/core/region_conversion_test.cpp',
    'color_transform_test' : 'color_transform_test.cpp',
    'isyntax_math_utils_test' : 'isyntax_math_utils_test.cpp',
    'isyntax_cpp_test' : 'isyntax_cpp_test.cpp',
    'isyntax_jpeg_cpp_test' : 'isyntax_jpeg_cpp_test.cpp',
    'isyntax_cache_cpp_test' : 'isyntax_cache_cpp_test.cpp',
    'isyntax_open_cpp_test' : 'isyntax_open_cpp_test.cpp',
    'tile_writer_test' : '../src/runtime/tile_writer_test.cpp',
    'tile_writer_uint16_test' : '../src/runtime/tile_writer_uint16_test.cpp',
    'reader_registry_test' : '../src/runtime/reader_registry_test.cpp',
    'lru_tile_cache_test' : '../src/runtime/lru_tile_cache_test.cpp',
    'cache_interface_test' : '../src/runtime/cache_interface_test.cpp',
    'binary_utils_test' : '../src/runtime/io/binary_utils_test.cpp',
    'path_utils_test' : '../src/runtime/io/path_utils_test.cpp',
    'png_decoder_test' : '../src/runtime/decoders/png_decoder_test.cpp',
    'bmp_decoder_test' : '../src/runtime/decoders/bmp_decoder_test.cpp',
    'mrxs_index_reader_test' : '../src/readers/mrxs/mrxs_index_reader_test.cpp',
    'czi_spatial_index_test' : '../src/readers/czi/czi_spatial_index_test.cpp',
    'czi_parse_test' : '../src/readers/czi/czi_parse_test.cpp',
    'czi_scene_plane_test' : '../src/readers/czi/czi_scene_plane_test.cpp',
    'bif_test' : '../src/readers/bif/bif_test.cpp',
    'dicom_magic_test' : '../src/readers/dicom/dicom_magic_test.cpp',
    'aperio_metadata_parser_test' : '../src/readers/aperio/metadata_parser_test.cpp',
    'aperio_tile_executor_test' : '../src/readers/aperio/aperio_tile_executor_test.cpp',
    'qptiff_metadata_parser_test' : '../src/readers/qptiff/metadata_parser_test.cpp',
    'imagejtiff_test' : '../src/readers/imagejtiff/imagejtiff_test.cpp',
    'ometiff_metadata_loader_test' : '../src/readers/ometiff/ometiff_metadata_loader_test.cpp',
    'omezarr_metadata_test' : '../src/readers/omezarr/omezarr_metadata_test.cpp',
    'omezarr_codec_test' : '../src/readers/omezarr/omezarr_codec_test.cpp',
    'olympusvsi_ets_test' : '../src/readers/olympusvsi/olympusvsi_ets_test.cpp',
    'olympusvsi_plan_builder_test' : '../src/readers/olympusvsi/olympusvsi_plan_builder_test.cpp',
    'olympusvsi_vsi_metadata_test' : '../src/readers/olympusvsi/olympusvsi_vsi_metadata_test.cpp',
  }

  foreach test_name, test_src : fastslide_tests
    exe = executable(test_name, test_src,
      include_directories : test_inc,
      dependencies : [fastslide_dep, gtest_dep],
      cpp_args : ['-D__ANSI__'])
    test(test_name, exe,
      env : {'TEST_TMPDIR' : meson.current_build_dir()})
  endforeach

  # DICOM concatenation test needs on-disk test fixtures.
  dicom_concat = executable('dicom_concatenation_test',
    '../src/readers/dicom/dicom_concatenation_test.cpp',
    include_directories : test_inc,
    dependencies : [fastslide_dep, gtest_dep],
    cpp_args : ['-D__ANSI__'])
  test('dicom_concatenation_test', dicom_concat,
    env : {
      'FASTSLIDE_DICOM_TESTDATA_DIR' :
        meson.current_source_dir() / '../src/readers/dicom/testdata',
    })
endif
