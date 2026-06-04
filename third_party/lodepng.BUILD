load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "lodepng",
    srcs = [
        "lodepng.cpp",
        "lodepng_util.cpp",
        "pngdetail.cpp",
    ],
    hdrs = [
        "lodepng.h",
        "lodepng_util.h",
    ],
    include_prefix = "lodepng",
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@zlib",
    ],
)