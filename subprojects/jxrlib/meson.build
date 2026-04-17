project('jxrlib', 'c',
  version: '0.0.0',
  default_options: [
    'default_library=static',
    'warning_level=0',
  ],
)

jxrlib_incdir = include_directories(
  'common/include',
  'image/sys',
  'image/encode',
  'image/decode',
  'image/x86',
  'jxrgluelib',
  'jxrtestlib',
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
    'image/sys/adapthuff.c',
    'image/sys/image.c',
    'image/sys/strcodec.c',
    'image/sys/strPredQuant.c',
    'image/sys/strTransform.c',
    # image/decode
    'image/decode/decode.c',
    'image/decode/postprocess.c',
    'image/decode/segdec.c',
    'image/decode/strdec.c',
    'image/decode/strInvTransform.c',
    'image/decode/strPredQuantDec.c',
    'image/decode/JXRTranscode.c',
    # image/encode
    'image/encode/encode.c',
    'image/encode/segenc.c',
    'image/encode/strenc.c',
    'image/encode/strFwdTransform.c',
    'image/encode/strPredQuantEnc.c',
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
    'jxrgluelib/JXRGlue.c',
    'jxrgluelib/JXRMeta.c',
    'jxrgluelib/JXRGluePFC.c',
    'jxrgluelib/JXRGlueJxr.c',
    # jxrtestlib
    'jxrtestlib/JXRTest.c',
    'jxrtestlib/JXRTestBmp.c',
    'jxrtestlib/JXRTestHdr.c',
    'jxrtestlib/JXRTestPnm.c',
    'jxrtestlib/JXRTestTif.c',
    'jxrtestlib/JXRTestYUV.c',
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


