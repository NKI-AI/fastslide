load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

licenses(["notice"])

exports_files(["COPYRIGHT"])

cc_binary(
    name = "make_tables",
    srcs = [
        "src/make-tables.c",
    ],
)

genrule(
    name = "openslide_tables",
    outs = ["src/openslide-tables.c"],
    cmd = "./$(location //:make_tables) $@",
    tools = [
        "//:make_tables",
    ],
)

genrule(
    name = "configure",
    outs = ["config.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        '#define FOPEN_CLOEXEC_FLAG "e"',
        "#define HAVE_DLFCN_H 1",
        "#define HAVE_FCNTL 1",
        "#define HAVE_FSEEKO 1",
        "#define HAVE_INTTYPES_H 1",
        "#define HAVE_MEMORY_H 1",
        "#define HAVE_STDINT_H 1",
        "#define HAVE_STDLIB_H 1",
        "#define HAVE_STRINGS_H 1",
        "#define HAVE_STRING_H 1",
        "#define HAVE_SYS_STAT_H 1",
        "#define HAVE_SYS_TYPES_H 1",
        "#define HAVE_UNISTD_H 1",
        "#define HAVE_VISIBILITY 1",
        '#define SUFFIXED_VERSION "3.4.1"',
        "",
        "#ifndef _DARWIN_USE_64_BIT_INODE",
        "#define _DARWIN_USE_64_BIT_INODE 1",
        "#endif",
        "EOF",
    ]),
)

OPENSLIDE_SOURCES = [
    "src/openslide.c",
    "src/openslide-cache.c",
    "src/openslide-decode-dicom.c",
    "src/openslide-decode-dicom.h",
    "src/openslide-decode-gdkpixbuf.c",
    "src/openslide-decode-gdkpixbuf.h",
    "src/openslide-decode-jp2k.c",
    "src/openslide-decode-jp2k.h",
    "src/openslide-decode-jpeg.c",
    "src/openslide-decode-jpeg.h",
    "src/openslide-decode-png.c",
    "src/openslide-decode-png.h",
    "src/openslide-decode-sqlite.c",
    "src/openslide-decode-sqlite.h",
    "src/openslide-decode-tiff.c",
    "src/openslide-decode-tiff.h",
    "src/openslide-decode-tifflike.c",
    "src/openslide-decode-tifflike.h",
    "src/openslide-decode-xml.c",
    "src/openslide-decode-xml.h",
    "src/openslide-error.c",
    "src/openslide-error.h",
    "src/openslide-file.c",
    "src/openslide-grid.c",
    "src/openslide-hash.c",
    "src/openslide-hash.h",
    "src/openslide-jdatasrc.c",
    "src/openslide-tables.c",
    "src/openslide-util.c",
    "src/openslide-vendor-aperio.c",
    "src/openslide-vendor-dicom.c",
    "src/openslide-vendor-generic-tiff.c",
    "src/openslide-vendor-hamamatsu.c",
    "src/openslide-vendor-leica.c",
    "src/openslide-vendor-mirax.c",
    "src/openslide-vendor-philips-tiff.c",
    "src/openslide-vendor-sakura.c",
    "src/openslide-vendor-synthetic.c",
    "src/openslide-vendor-trestle.c",
    "src/openslide-vendor-ventana.c",
]

OPENSLIDE_HEADERS = [
    "src/openslide.h",
    "src/openslide-features.h",
    "src/openslide-private.h",
]

cc_library(
    name = "openslide_config_h",
    hdrs = ["config.h"],
    include_prefix = ".",
)

cc_library(
    name = "openslide",
    srcs = OPENSLIDE_SOURCES,
    hdrs = OPENSLIDE_HEADERS,
    copts = [
        "-Wno-deprecated-declarations",
        "-Iexternal/openslide~4.0.0",
        "-Iexternal/openslide~4.0.0/src",
        "-Iexternal/sqlite3~",
    ],
    include_prefix = "openslide",
    includes = ["src"],
    strip_include_prefix = "src",
    visibility = ["//visibility:public"],
    deps = [
        ":openslide_config_h",
        "@cairo",
        "@gdk-pixbuf//:gdk-pixbuf",
        "@glib2//:gio",
        "@glib2//:glib",
        "@glib2//:gobject",
        "@libdicom",
        "@libjpeg_turbo//:jpeg",
        "@libpng//:png",
        "@libtiff",
        "@libxml2",
        "@openjp2",
        "@sqlite3",
    ],
)

cc_binary(
    name = "openslide-shared-impl",
    srcs = OPENSLIDE_SOURCES + OPENSLIDE_HEADERS,
    linkshared = True,
    linkstatic = True,
    copts = [
        "-Wno-deprecated-declarations",
        "-Iexternal/openslide~4.0.0",
        "-Iexternal/openslide~4.0.0/src",
        "-Iexternal/sqlite3~",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":openslide",
    ],
)

genrule(
    name = "openslide-shared",
    srcs = [":openslide-shared-impl"],
    outs = ["openslide.so"],  # The desired output filename
    cmd = "cp $< $@",  # Copy and rename the library
    visibility = ["//visibility:public"],  # Expose this to other targets
)
