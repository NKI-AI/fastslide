project('fastslide', 'c', 'cpp',
  version : '0.7.4',
  license : 'Apache-2.0',
  meson_version : '>=1.3',
  default_options : [
    'warning_level=1',
    'cpp_std=c++20',
    'default_library=static',
    # Optimized by default (mirrors the Bazel `--config=release` -O3 -DNDEBUG).
    # Meson's built-in default is `buildtype=debug` (-O0). Override on the CLI
    # with e.g. `meson setup builddir --buildtype=debug` for an unoptimized build.
    'buildtype=release',
    'b_ndebug=if-release',
  ])

fs = import('fs')
cpp = meson.get_compiler('cpp')
is_wasm = host_machine.cpu_family() == 'wasm32' or host_machine.cpu_family() == 'wasm64'

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# NKI-AI libraries (local symlinks in the monorepo, git wraps standalone).
aifocore_dep = dependency('aifocore')
simpletiff_dep = dependency('simpletiff')

# SIMD + codecs. All have wrap fallbacks; system copies are used when present.
hwy_dep = dependency('libhwy', fallback : ['highway', 'hwy_dep'])
zlib_dep = dependency('zlib')
zstd_dep = dependency('libzstd')
png_dep = dependency('libpng')
pugixml_dep = dependency('pugixml')
json_dep = dependency('nlohmann_json')
openjpeg_dep = dependency('libopenjp2')
dicom_dep = dependency('libdicom')
lodepng_dep = dependency('lodepng')
jxrlib_dep = dependency('jxrlib')

fastslide_deps = [
  aifocore_dep, simpletiff_dep, hwy_dep, zlib_dep, zstd_dep, png_dep,
  pugixml_dep, json_dep, openjpeg_dep, dicom_dep, lodepng_dep, jxrlib_dep,
]

# JPEG decoder backend (libjpeg by default, jpgd opt-in).
if get_option('jpeg_decoder') == 'libjpeg'
  fastslide_deps += dependency('libjpeg')
  jpeg_decoder_src = 'src/runtime/decoders/jpeg_decoder_libjpeg.cpp'
else
  fastslide_deps += dependency('jpgd', fallback : ['jpeg-compressor', 'jpgd_dep'])
  jpeg_decoder_src = 'src/runtime/decoders/jpeg_decoder_jpgd.cpp'
endif

# ---------------------------------------------------------------------------
# Library sources
# ---------------------------------------------------------------------------
inc = include_directories('include')

# isyntax.c is compiled as C++20 (it uses <cstdio>, <memory>, ...). Copy it to a
# .cpp so Meson picks the C++ compiler; includes resolve via `inc`.
isyntax_as_cpp = fs.copyfile('src/readers/isyntax/third_party/isyntax.c',
  'isyntax_core_impl.cpp')

fastslide_sources = [
  'src/image.cpp',
  'src/self_image_view.cpp',
  'src/slide_image.cpp',
  'src/slide_reader.cpp',
  'src/utilities/colors.cpp',
  'src/utilities/hash.cpp',
  'src/utilities/sha-256.c',
  'src/utilities/tiff/tile_utilities.cpp',
  # readers/aperio
  'src/readers/aperio/aperio.cpp',
  'src/readers/aperio/aperio_format_plugin.cpp',
  'src/readers/aperio/aperio_plan_builder.cpp',
  'src/readers/aperio/aperio_tile_executor.cpp',
  'src/readers/aperio/metadata_parser.cpp',
  # readers/bif
  'src/readers/bif/bif.cpp',
  'src/readers/bif/bif_format_plugin.cpp',
  'src/readers/bif/bif_plan_builder.cpp',
  'src/readers/bif/bif_spatial_index.cpp',
  'src/readers/bif/bif_stitcher.cpp',
  'src/readers/bif/bif_tile_executor.cpp',
  'src/readers/bif/bif_xml.cpp',
  # readers/czi
  'src/readers/czi/czi.cpp',
  'src/readers/czi/czi_embedded.cpp',
  'src/readers/czi/czi_format_plugin.cpp',
  'src/readers/czi/czi_parse.cpp',
  'src/readers/czi/czi_plan_builder.cpp',
  'src/readers/czi/czi_scene_image.cpp',
  'src/readers/czi/czi_spatial_index.cpp',
  'src/readers/czi/czi_tile_executor.cpp',
  # readers/dicom
  'src/readers/dicom/dicom.cpp',
  'src/readers/dicom/dicom_decode.cpp',
  'src/readers/dicom/dicom_format_plugin.cpp',
  'src/readers/dicom/dicom_magic.cpp',
  'src/readers/dicom/dicom_plan_builder.cpp',
  'src/readers/dicom/dicom_tile_executor.cpp',
  # readers/generictiff
  'src/readers/generictiff/generictiff.cpp',
  'src/readers/generictiff/generictiff_format_plugin.cpp',
  'src/readers/generictiff/generictiff_plan_builder.cpp',
  'src/readers/generictiff/generictiff_tile_executor.cpp',
  # readers/imagejtiff
  'src/readers/imagejtiff/imagejtiff.cpp',
  'src/readers/imagejtiff/imagejtiff_plan_builder.cpp',
  # readers/isyntax (third_party, minus isyntax.c handled above)
  'src/readers/isyntax/isyntax.cpp',
  'src/readers/isyntax/isyntax_plan_builder.cpp',
  'src/readers/isyntax/isyntax_tile_executor.cpp',
  'src/readers/isyntax/third_party/base64.cpp',
  'src/readers/isyntax/third_party/cache.cpp',
  'src/readers/isyntax/third_party/color.cpp',
  'src/readers/isyntax/third_party/decompress.cpp',
  'src/readers/isyntax/third_party/dwt.cpp',
  'src/readers/isyntax/third_party/file.cpp',
  'src/readers/isyntax/third_party/jpeg.cpp',
  'src/readers/isyntax/third_party/open.cpp',
  'src/readers/isyntax/third_party/open_helpers.cpp',
  'src/readers/isyntax/third_party/platform/platform.cpp',
  'src/readers/isyntax/third_party/reader.cpp',
  'src/readers/isyntax/third_party/seektable.cpp',
  'src/readers/isyntax/third_party/third_party/yxml.c',
  'src/readers/isyntax/third_party/tile.cpp',
  'src/readers/isyntax/third_party/utils/arena_allocator.cpp',
  'src/readers/isyntax/third_party/utils/block_allocator.cpp',
  'src/readers/isyntax/third_party/utils/math_utils.cpp',
  'src/readers/isyntax/third_party/utils/mathutils.cpp',
  'src/readers/isyntax/third_party/xml_header_reader.cpp',
  'src/readers/isyntax/third_party/xml_parser.cpp',
  'src/readers/isyntax/third_party/xml_semantics.cpp',
  # readers/mrxs
  'src/readers/mrxs/mrxs.cpp',
  'src/readers/mrxs/mrxs_associated_data.cpp',
  'src/readers/mrxs/mrxs_data_reader.cpp',
  'src/readers/mrxs/mrxs_decoder.cpp',
  'src/readers/mrxs/mrxs_format_plugin.cpp',
  'src/readers/mrxs/mrxs_index_reader.cpp',
  'src/readers/mrxs/mrxs_ini_parser.cpp',
  'src/readers/mrxs/mrxs_layer_parser.cpp',
  'src/readers/mrxs/mrxs_metadata_loader.cpp',
  'src/readers/mrxs/mrxs_pipeline.cpp',
  'src/readers/mrxs/mrxs_plan_builder.cpp',
  'src/readers/mrxs/mrxs_position_reader.cpp',
  'src/readers/mrxs/mrxs_tile_executor.cpp',
  'src/readers/mrxs/spatial_index.cpp',
  # readers/ndpitiff
  'src/readers/ndpitiff/ndpitiff.cpp',
  'src/readers/ndpitiff/ndpitiff_format_plugin.cpp',
  'src/readers/ndpitiff/ndpitiff_plan_builder.cpp',
  'src/readers/ndpitiff/ndpitiff_tile_executor.cpp',
  # readers/olympusvsi
  'src/readers/olympusvsi/olympusvsi.cpp',
  'src/readers/olympusvsi/olympusvsi_ets.cpp',
  'src/readers/olympusvsi/olympusvsi_format_plugin.cpp',
  'src/readers/olympusvsi/olympusvsi_plan_builder.cpp',
  'src/readers/olympusvsi/olympusvsi_tile_executor.cpp',
  'src/readers/olympusvsi/olympusvsi_vsi_metadata.cpp',
  # readers/ometiff
  'src/readers/ometiff/metadata_parser.cpp',
  'src/readers/ometiff/ometiff.cpp',
  'src/readers/ometiff/ometiff_format_plugin.cpp',
  'src/readers/ometiff/ometiff_metadata_loader.cpp',
  'src/readers/ometiff/ometiff_plan_builder.cpp',
  # readers/omezarr
  'src/readers/omezarr/omezarr.cpp',
  'src/readers/omezarr/omezarr_codec.cpp',
  'src/readers/omezarr/omezarr_format_plugin.cpp',
  'src/readers/omezarr/omezarr_metadata.cpp',
  'src/readers/omezarr/omezarr_plan_builder.cpp',
  'src/readers/omezarr/omezarr_tile_executor.cpp',
  # readers/philipstiff
  'src/readers/philipstiff/philipstiff.cpp',
  'src/readers/philipstiff/philipstiff_plan_builder.cpp',
  'src/readers/philipstiff/philipstiff_tile_executor.cpp',
  # readers/qptiff
  'src/readers/qptiff/metadata_parser.cpp',
  'src/readers/qptiff/qptiff.cpp',
  'src/readers/qptiff/qptiff_format_plugin.cpp',
  'src/readers/qptiff/qptiff_metadata_loader.cpp',
  'src/readers/qptiff/qptiff_plan_builder.cpp',
  # readers (shared)
  'src/readers/simpletiff_plan_builder_utils.cpp',
  'src/readers/tiff_based_reader.cpp',
  'src/readers/tiff_quickhash.cpp',
  # runtime
  'src/runtime/decoders/bmp_decoder.cpp',
  'src/runtime/decoders/j2k_decoder.cpp',
  'src/runtime/decoders/jpeg_decoder_common.cpp',
  jpeg_decoder_src,
  'src/runtime/decoders/jpeg_xr_decoder.cpp',
  'src/runtime/decoders/png_decoder.cpp',
  'src/runtime/global_cache_manager.cpp',
  'src/runtime/io/binary_utils.cpp',
  'src/runtime/io/file_reader.cpp',
  'src/runtime/lru_tile_cache.cpp',
  'src/runtime/plugin_loader.cpp',
  'src/runtime/reader_registry.cpp',
  'src/runtime/tile_writer.cpp',
  'src/runtime/tile_writer/bilinear_blit_hwy.cpp',
  'src/runtime/tile_writer/canvas_config.cpp',
  'src/runtime/tile_writer/direct/copy_planar.cpp',
  'src/runtime/tile_writer/direct/copy_rgb8.cpp',
  'src/runtime/tile_writer/direct/fills.cpp',
  'src/runtime/tile_writer/paint_direct.cpp',
  'src/runtime/tile_writer/paint_dispatch.cpp',
  'src/runtime/tile_writer/paint_rgb.cpp',
  'src/runtime/tile_writer/rgb_blit.cpp',
]

# -D__ANSI__ is required by the JPEG-XR decoder and CZI; jxrlib_dep already
# propagates it, but set it library-wide to match the Bazel per-target flag.
# `-Wno-unused-parameter` is GCC/Clang-only; drop it on MSVC (cl.exe rejects it).
fastslide_cpp_args = ['-D__ANSI__'] + cpp.get_supported_arguments(['-Wno-unused-parameter'])

# When building the Python wheel (build_python) the static library and public
# headers are NOT installed: meson-python would otherwise dump them into the
# wheel. Only the nanobind extension + Python package belong in the wheel.
install_cpp = not get_option('build_python')

fastslide_lib = library('fastslide',
  fastslide_sources, isyntax_as_cpp,
  include_directories : inc,
  dependencies : fastslide_deps,
  cpp_args : fastslide_cpp_args,
  install : install_cpp)

# The built-in format registrar self-registers all readers from a single static
# initializer (g_builtin_registrar in register_builtin_formats.cpp). Nothing
# references its symbols, so the linker would discard the whole translation unit
# from a static archive, leaving no formats registered ("No reader registered
# for extension: .vsi"). Bazel marks this one target alwayslink = 1; mirror that
# by building it separately and force-linking it via link_whole below.
fastslide_register_lib = static_library('fastslide_register',
  'src/runtime/register_builtin_formats.cpp',
  include_directories : inc,
  dependencies : fastslide_deps,
  cpp_args : fastslide_cpp_args,
  install : false)

if install_cpp
  install_subdir('include/fastslide', install_dir : get_option('includedir'))
endif

fastslide_dep = declare_dependency(
  include_directories : inc,
  link_with : fastslide_lib,
  link_whole : fastslide_register_lib,
  dependencies : fastslide_deps)

meson.override_dependency('fastslide', fastslide_dep)

# ---------------------------------------------------------------------------
# Python extension (nanobind)
# ---------------------------------------------------------------------------
# Built only for wheels (meson-python sets -Dbuild_python=true). nanobind and
# robin-map are resolved from subprojects/*.wrap; with --wrap-mode=forcefallback
# they compile from source, so the resulting `_fastslide` extension is fully
# self-contained (all codecs are static via default_library=static).
if get_option('build_python')
  py = import('python').find_installation(pure : false)
  nanobind_dep = dependency('nanobind')

  py.extension_module('_fastslide',
    [
      'src/python/fastslide.cpp',
      'src/python/cache.cpp',
      'src/python/reader.cpp',
    ],
    include_directories : inc,
    dependencies : [nanobind_dep, fastslide_dep],
    cpp_args : fastslide_cpp_args,
    install : true,
    subdir : 'fastslide')

  # Pure-Python package + checked-in nanobind stub (matches the Bazel wheel);
  # both install by basename into the `fastslide` package dir.
  py.install_sources(
    [
      'python/fastslide/__init__.py',
      'src/python/_fastslide.pyi',
    ],
    subdir : 'fastslide')
endif

# ---------------------------------------------------------------------------
# CLI tool
# ---------------------------------------------------------------------------
if get_option('build_tool')
  executable('fastslidetool',
    'tools/fastslidetool.cpp',
    include_directories : [inc, include_directories('third_party')],
    dependencies : [fastslide_dep],
    install : true)
endif

# ---------------------------------------------------------------------------
# C API shared library
# ---------------------------------------------------------------------------
if get_option('build_c_api')
  c_api_sources = [
    'src/c/fastslide.cpp',
    'src/c/image.cpp',
    'src/c/registry.cpp',
    'src/c/slide_image.cpp',
    'src/c/slide_reader.cpp',
    'src/c/utilities.cpp',
  ]
  if get_option('shared_c_api')
    # Self-contained shared C API for the Java FFM native artifact. The export
    # macro FASTSLIDE_API only expands to __declspec(dllexport) (Windows) /
    # visibility("default") (Unix) when FASTSLIDE_BUILDING_DLL is defined, so
    # define it here. gnu_symbol_visibility=hidden keeps everything but the C
    # API out of the export table. With default_library=static, all wrap deps
    # link in statically, producing a self-contained fastslide_c.{dll,so,dylib}.
    fastslide_c_lib = shared_library('fastslide_c',
      c_api_sources,
      include_directories : inc,
      dependencies : [fastslide_dep],
      cpp_args : fastslide_cpp_args + ['-DFASTSLIDE_BUILDING_DLL'],
      gnu_symbol_visibility : 'hidden',
      install : false)
  else
    fastslide_c_lib = library('fastslide_c',
      c_api_sources,
      include_directories : inc,
      dependencies : [fastslide_dep],
      cpp_args : fastslide_cpp_args,
      install : true)
  endif
endif

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if get_option('build_tests')
  subdir('tests')
endif
