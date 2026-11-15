load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # MIT license

package(default_visibility = ["//visibility:public"])

exports_files(["LICENSE"])

# Little CMS 2 (lcms2) color-management engine. Built from the release tarball
# `src/*.c` core sources; the optional fast_float / threaded plugins are not
# compiled in. This is the same engine OpenSlide uses, so the sRGB output is
# byte-compatible.
cc_library(
    name = "lcms2",
    srcs = [
        "src/cmsalpha.c",
        "src/cmscam02.c",
        "src/cmscgats.c",
        "src/cmscnvrt.c",
        "src/cmserr.c",
        "src/cmsgamma.c",
        "src/cmsgmt.c",
        "src/cmshalf.c",
        "src/cmsintrp.c",
        "src/cmsio0.c",
        "src/cmsio1.c",
        "src/cmslut.c",
        "src/cmsmd5.c",
        "src/cmsmtrx.c",
        "src/cmsnamed.c",
        "src/cmsopt.c",
        "src/cmspack.c",
        "src/cmspcs.c",
        "src/cmsplugin.c",
        "src/cmsps2.c",
        "src/cmssamp.c",
        "src/cmssm.c",
        "src/cmstypes.c",
        "src/cmsvirt.c",
        "src/cmswtpnt.c",
        "src/cmsxform.c",
        "src/lcms2_internal.h",
    ],
    hdrs = [
        "include/lcms2.h",
        "include/lcms2_plugin.h",
    ],
    copts = [
        "-Wno-unused-function",
        "-Wno-unused-but-set-variable",
        "-Wno-unused-variable",
    ],
    includes = [
        "include",
        "src",
    ],
    visibility = ["//visibility:public"],
)
