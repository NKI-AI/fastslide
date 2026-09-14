load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "lodepng",
    # `pngdetail.cpp` is upstream's command-line inspector and defines its own
    # `main`. Linked into a shared library it shadows the real entrypoint, which
    # silently turned every test binary depending on this target into the
    # pngdetail CLI.
    srcs = [
        "lodepng.cpp",
        "lodepng_util.cpp",
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