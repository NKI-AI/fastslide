load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

licenses(["notice"])

config_setting(
    name = "is_windows",
    constraint_values = ["@platforms//os:windows"],
)

# Expand version.h from a template
expand_template(
    name = "version_h",
    out = "include/dicom/version.h",
    substitutions = {
        "@DCM_VERSION@": "1.3.0",
        "@DCM_SUFFIXED_VERSION@": "1.3.0",
        "@DCM_VERSION_MAJOR@": "1",
        "@DCM_VERSION_MINOR@": "3",
        "@DCM_VERSION_MICRO@": "0",
        "@DCM_ABI_VERSION_MAJOR@": "1",
        "@DCM_ABI_VERSION_MINOR@": "3",
        "@DCM_ABI_VERSION_PATCH@": "0",
        "@DCM_STATIC@": "1",
    },
    template = "include/dicom/version.h.in",
)

cc_library(
    name = "libdicom_version_h",
    hdrs = ["include/dicom/version.h"],
    includes = [
        "include/dicom",
    ],
)

cc_binary(
    name = "dicom_dict_build",
    srcs = [
        "config.h",
        "include/dicom/dicom.h",
        "src/dicom-dict-build.c",
        "src/dicom-dict-tables.c",
        "src/dicom-dict-tables.h",
        "src/pdicom.h",
    ],
    includes = ["include"],
    deps = [
        ":libdicom_version_h",  # For version.h
        "@uthash",
    ],
)

# Use it to generate both the .c and .h files
genrule(
    name = "generate_dict_lookup",
    outs = [
        "src/dicom-dict-lookup.c",
        "src/dicom-dict-lookup.h",
    ],
    cmd = "$(location :dicom_dict_build) $(OUTS)",
    tools = [":dicom_dict_build"],
)

# Main dicom library
cc_library(
    name = "libdicom",
    srcs = [
        "config.h",
        "src/dicom.c",
        "src/dicom-data.c",
        "src/dicom-dict.c",
        "src/dicom-dict-tables.c",
        "src/dicom-dict-tables.h",
        "src/dicom-file.c",
        "src/dicom-io.c",
        "src/dicom-parse.c",
        "src/getopt.c",
        "src/pdicom.h",
        ":generate_dict_lookup",
    ],
    hdrs = [
        "include/dicom/dicom.h",
        "include/dicom/version.h",
        "src/dicom-dict-lookup.h",
    ],
    copts = [
        "-DBUILDING_LIBDICOM",
    ] + select({
        ":is_windows": [],
        "//conditions:default": ["-fvisibility=hidden"],
    }),
    includes = [
        "include",
        "src",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":libdicom_version_h",
        "@uthash",
    ],
)

# CLI tools
cc_binary(
    name = "dcm-dump",
    srcs = ["tools/dcm-dump.c"],
    deps = [":libdicom"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "dcm-getframe",
    srcs = ["tools/dcm-getframe.c"],
    deps = [":libdicom"],
    visibility = ["//visibility:public"],
)

# Generate config.h
genrule(
    name = "configure",
    outs = ["config.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        "#pragma once",
        "#ifdef _WIN32",
        "#define HAVE_IO_H 1",
        "#else",
        "#define HAVE_UNISTD_H 1",
        "#endif",
        "EOF",
    ]),
)
