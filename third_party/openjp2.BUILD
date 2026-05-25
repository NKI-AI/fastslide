load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # 2-clauses BSD license

package(default_visibility = ["//visibility:public"])

exports_files(["LICENSE"])

VERSION_MAJOR = "2"

VERSION_MINOR = "4"

VERSION_MICRO = "0"

cc_library(
    name = "openjp2",
    srcs = [
        "src/lib/openjp2/bio.c",
        "src/lib/openjp2/cidx_manager.c",
        "src/lib/openjp2/cio.c",
        "src/lib/openjp2/dwt.c",
        "src/lib/openjp2/event.c",
        "src/lib/openjp2/function_list.c",
        "src/lib/openjp2/image.c",
        "src/lib/openjp2/invert.c",
        "src/lib/openjp2/j2k.c",
        "src/lib/openjp2/jp2.c",
        "src/lib/openjp2/mct.c",
        "src/lib/openjp2/mqc.c",
        "src/lib/openjp2/openjpeg.c",
        "src/lib/openjp2/opj_clock.c",
        "src/lib/openjp2/opj_malloc.c",
        "src/lib/openjp2/phix_manager.c",
        "src/lib/openjp2/pi.c",
        "src/lib/openjp2/ppix_manager.c",
        "src/lib/openjp2/sparse_array.c",
        "src/lib/openjp2/t1.c",
        "src/lib/openjp2/t2.c",
        "src/lib/openjp2/tcd.c",
        "src/lib/openjp2/tgt.c",
        "src/lib/openjp2/thix_manager.c",
        "src/lib/openjp2/thread.c",
        "src/lib/openjp2/tpix_manager.c",
    ],
    hdrs = [
        "config/opj_config.h",
        "config/opj_config_private.h",
        "src/lib/openjp2/bio.h",
        "src/lib/openjp2/cidx_manager.h",
        "src/lib/openjp2/cio.h",
        "src/lib/openjp2/dwt.h",
        "src/lib/openjp2/event.h",
        "src/lib/openjp2/function_list.h",
        "src/lib/openjp2/image.h",
        "src/lib/openjp2/indexbox_manager.h",
        "src/lib/openjp2/invert.h",
        "src/lib/openjp2/j2k.h",
        "src/lib/openjp2/jp2.h",
        "src/lib/openjp2/mct.h",
        "src/lib/openjp2/mqc.h",
        "src/lib/openjp2/mqc_inl.h",
        "src/lib/openjp2/openjpeg.h",
        "src/lib/openjp2/opj_clock.h",
        "src/lib/openjp2/opj_codec.h",
        "src/lib/openjp2/opj_common.h",
        "src/lib/openjp2/opj_includes.h",
        "src/lib/openjp2/opj_intmath.h",
        "src/lib/openjp2/opj_inttypes.h",
        "src/lib/openjp2/opj_malloc.h",
        "src/lib/openjp2/opj_stdint.h",
        "src/lib/openjp2/pi.h",
        "src/lib/openjp2/sparse_array.h",
        "src/lib/openjp2/t1.h",
        "src/lib/openjp2/t1_luts.h",
        "src/lib/openjp2/t2.h",
        "src/lib/openjp2/tcd.h",
        "src/lib/openjp2/tgt.h",
        "src/lib/openjp2/thread.h",
        "src/lib/openjp2/tls_keys.h",
    ],
    copts = [
        "-Wno-unused-function",
        "-Wno-implicit-int-float-conversion",
        "-Wno-unused-but-set-variable",
    ],
    includes = [
        "config",
        "src/lib/openjp2",
    ],
    visibility = ["//visibility:public"],
)

genrule(
    name = "opj_config_h",
    outs = ["config/opj_config.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        "#define OPJ_HAVE_STDINT_H 1",
        "#define OPJ_VERSION_MAJOR {}".format(VERSION_MAJOR),
        "#define OPJ_VERSION_MINOR {}".format(VERSION_MINOR),
        "#define OPJ_VERSION_BUILD {}".format(VERSION_MICRO),
        "EOF",
    ]),
)

genrule(
    name = "opj_config_private_h",
    outs = ["config/opj_config_private.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        "#define OPJ_HAVE_INTTYPES_H 1",
        '#define OPJ_PACKAGE_VERSION "2.3.1"',
        "#define USE_JPIP 1",
        "",
        "#if !defined(_MSC_VER) && !defined(_WIN32)",
        "",
        "#define OPJ_HAVE_FSEEKO ON",
        "#define OPJ_HAVE_MEMALIGN",
        "#define OPJ_HAVE_POSIX_MEMALIGN",
        "#if !defined(_POSIX_C_SOURCE)",
        "#if defined(OPJ_HAVE_FSEEKO) || defined(OPJ_HAVE_POSIX_MEMALIGN)",
        "#define _POSIX_C_SOURCE 200112L",
        "#endif",
        "#endif",
        "",
        "#endif",
        "EOF",
    ]),
)
