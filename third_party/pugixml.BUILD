load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "pugixml",
    srcs = ["src/pugixml.cpp"],
    hdrs = [
        "src/pugiconfig.hpp",
        "src/pugixml.hpp",
    ],
    copts = select({
        # pugixml's XPath module uses `throw`. Emscripten disables C++
        # exceptions by default, so we have to opt back in for the wasm
        # build to compile.
        "@platforms//cpu:wasm32": ["-fexceptions"],
        "//conditions:default": [],
    }),
    includes = ["src"],
    visibility = ["//visibility:public"],
)
