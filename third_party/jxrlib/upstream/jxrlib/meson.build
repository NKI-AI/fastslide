project('jxrlib', 'c',
  version: '0.0.0',
  default_options: [
    'default_library=static',
    'warning_level=0',
  ],
)

jxrlib_src_root = '.'

jxrlib_incdir = include_directories(
  jxrlib_src_root,
  jxrlib_src_root / 'common/include',
  jxrlib_src_root / 'image/sys',
  jxrlib_src_root / 'image/encode',
  jxrlib_src_root / 'image/decode',
  jxrlib_src_root / 'image/x86',
  jxrlib_src_root / 'jxrgluelib',
  jxrlib_src_root / 'jxrtestlib',
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

# Match upstream Makefile/Bazel (keep warnings off and define __ANSI__).
jxrlib_c_args = [
  '-D__ANSI__',
  '-DDISABLE_PERF_MEASUREMENT',
  '-w',
]

# Core JPEG-XR codec (sys + encode + decode).
jxrlib_jpegxr = static_library('jpegxr',
  [
    # image/sys
    jxrlib_src_root / 'image/sys/adapthuff.c',
    jxrlib_src_root / 'image/sys/image.c',
    jxrlib_src_root / 'image/sys/strcodec.c',
    jxrlib_src_root / 'image/sys/strPredQuant.c',
    jxrlib_src_root / 'image/sys/strTransform.c',
    # image/decode
    jxrlib_src_root / 'image/decode/decode.c',
    jxrlib_src_root / 'image/decode/postprocess.c',
    jxrlib_src_root / 'image/decode/segdec.c',
    jxrlib_src_root / 'image/decode/strdec.c',
    jxrlib_src_root / 'image/decode/strInvTransform.c',
    jxrlib_src_root / 'image/decode/strPredQuantDec.c',
    jxrlib_src_root / 'image/decode/JXRTranscode.c',
    # image/encode
    jxrlib_src_root / 'image/encode/encode.c',
    jxrlib_src_root / 'image/encode/segenc.c',
    jxrlib_src_root / 'image/encode/strenc.c',
    jxrlib_src_root / 'image/encode/strFwdTransform.c',
    jxrlib_src_root / 'image/encode/strPredQuantEnc.c',
  ],
  include_directories: jxrlib_incdir,
  c_args: jxrlib_c_args,
  dependencies: [m_dep],
  pic: true,
  install: false,
)

# Glue helpers + test utilities (upstream bundles jxrtestlib into libjxrglue).
jxrlib_glue = static_library('jxrglue',
  [
    # jxrgluelib
    jxrlib_src_root / 'jxrgluelib/JXRGlue.c',
    jxrlib_src_root / 'jxrgluelib/JXRMeta.c',
    jxrlib_src_root / 'jxrgluelib/JXRGluePFC.c',
    jxrlib_src_root / 'jxrgluelib/JXRGlueJxr.c',
    # jxrtestlib
    jxrlib_src_root / 'jxrtestlib/JXRTest.c',
    jxrlib_src_root / 'jxrtestlib/JXRTestBmp.c',
    jxrlib_src_root / 'jxrtestlib/JXRTestHdr.c',
    jxrlib_src_root / 'jxrtestlib/JXRTestPnm.c',
    jxrlib_src_root / 'jxrtestlib/JXRTestTif.c',
    jxrlib_src_root / 'jxrtestlib/JXRTestYUV.c',
  ],
  include_directories: jxrlib_incdir,
  c_args: jxrlib_c_args,
  dependencies: [m_dep],
  link_with: jxrlib_jpegxr,
  pic: true,
  install: false,
)

jxrlib_dep = declare_dependency(
  include_directories: jxrlib_incdir,
  link_with: [jxrlib_glue, jxrlib_jpegxr],
  dependencies: [m_dep],
  # Consumers that include <JXRGlue.h> must see __ANSI__ so guiddef.h defines FAR
  # as empty (otherwise it becomes `_far` and breaks C++ parsing on non-Windows).
  compile_args: [
    '-D__ANSI__',
    '-DDISABLE_PERF_MEASUREMENT',
  ],
)

# Allow consumers to do:
#   dependency('jxrlib', fallback: ['jxrlib', 'jxrlib_dep'])
meson.override_dependency('jxrlib', jxrlib_dep)


