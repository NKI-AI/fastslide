load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # MIT License

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "uthash",
    hdrs = glob(["src/*.h"]),
    includes = ["src"],
    textual_hdrs = glob(["src/*.h"]),
    visibility = ["//visibility:public"],
    deps = [],
)
