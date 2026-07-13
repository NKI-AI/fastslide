"""Packaging macros for FastSlide binary bundles."""

load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")

CROSS_PACKAGES_COMPAT = select({
    "//package:cross_packages_enabled": [],
    "//conditions:default": ["@platforms//:incompatible"],
})

def fastslide_bundle_tar_xz(name, *, tool, lib, deps = []):
    """Create a per-platform tar.xz bundle.

    Args:
      name: Name of the final tar target (and archive basename).
      tool: Label of platform-specific fastslidetool binary.
      lib: Label of platform-specific fastslide shared library.
    """
    pkg_tar(
        name = name + "_bin_tar",
        srcs = [tool],
        mode = "0755",
        package_dir = "bin",
        strip_prefix = "/external/fastslide+",
        target_compatible_with = CROSS_PACKAGES_COMPAT,
    )

    pkg_tar(
        name = name + "_lib_tar",
        srcs = [lib],
        mode = "0755",
        package_dir = "lib",
        strip_prefix = "/external/fastslide+",
        target_compatible_with = CROSS_PACKAGES_COMPAT,
    )

    pkg_tar(
        name = name,
        deps = [
            ":include_tar",
            ":aifocore_include_tar",
            ":simpletiff_include_tar",
            ":meta_root_tar",
            ":versions_tar",
            ":" + name + "_bin_tar",
            ":" + name + "_lib_tar",
        ] + deps,
        extension = "tar.xz",
        package_dir = "",
        strip_prefix = "",
        target_compatible_with = CROSS_PACKAGES_COMPAT,
    )
