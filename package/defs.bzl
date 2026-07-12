"""Packaging macros for FastSlide Debian packages.

Two Debian packages are produced:

- ``libfastslide`` (runtime, per-arch): the ``libfastslide.so`` shared library
  and the ``fastslidetool`` CLI. All third-party dependencies (simpletiff,
  aifocore, lcms2, highway, ...) are statically linked into ``libfastslide.so``,
  so it is self-contained and consumers only need ``-lfastslide``.
- ``libfastslide-dev`` (arch-independent ``all``): the C/C++ headers for the
  FastSlide SDK plus its ``aifocore`` and ``simpletiff`` transitive headers
  (the public headers ``#include`` them) and a ``fastslide.pc`` pkg-config file.

The debs are built with the NATIVE host toolchain (system gcc/libstdc++) so the
shared object matches the distribution it is installed on, and are wired into
the release workflow's ``build-deb`` / ``smoke-deb`` jobs. Because they use the
native toolchain (not the Zig hermetic cross-toolchain), they carry no platform
transition and no ``fastslide_cross_packages`` gating.
"""

load("@rules_pkg//pkg:deb.bzl", "pkg_deb")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

_MAINTAINER = "Jonas Teuwen <j.teuwen@nki.nl>"
_HOMEPAGE = "https://github.com/NKI-AI/fastslide"

def fastslide_runtime_deb(name, *, version, deb_arch, tool, lib):
    """Build the per-arch ``libfastslide`` runtime Debian package.

    Args:
      name: Base name for the generated targets.
      version: Debian package version (e.g. ``0.8.0``).
      deb_arch: Debian architecture string (``amd64`` or ``arm64``).
      tool: Label of the ``fastslidetool`` CLI binary.
      lib: Label producing ``libfastslide.so`` (self-contained; simpletiff and
        the other third-party deps are statically linked in).
    """

    # ``strip_prefix.files_only()`` flattens each source to its basename,
    # regardless of whether the label lives in the main repo or an external
    # repo. This keeps the targets buildable both in the monorepo (where
    # fastslide is ``@fastslide``) and in the synced standalone repo (where
    # fastslide is the main repo).
    pkg_files(
        name = name + "_lib_files",
        srcs = [lib],
        attributes = pkg_attributes(mode = "0644"),
        prefix = "usr/lib",
        strip_prefix = strip_prefix.files_only(),
    )

    pkg_files(
        name = name + "_bin_files",
        srcs = [tool],
        attributes = pkg_attributes(mode = "0755"),
        prefix = "usr/bin",
        strip_prefix = strip_prefix.files_only(),
    )

    pkg_tar(
        name = name + "_data",
        srcs = [
            ":" + name + "_lib_files",
            ":" + name + "_bin_files",
        ],
    )

    pkg_deb(
        name = name,
        data = ":" + name + "_data",
        architecture = deb_arch,
        description = "FastSlide digital pathology slide reader (shared library + CLI).",
        homepage = _HOMEPAGE,
        maintainer = _MAINTAINER,
        package = "libfastslide",
        package_file_name = "libfastslide_" + version + "_" + deb_arch + ".deb",
        postinst = "//package:ldconfig_postinst.sh",
        priority = "optional",
        section = "libs",
        version = version,
    )

def fastslide_dev_deb(name, *, version, headers, pc_file):
    """Build the arch-independent ``libfastslide-dev`` headers Debian package.

    Args:
      name: Base name for the generated targets.
      version: Debian package version; the dev package depends on the exact
        matching runtime version.
      headers: List of (label, strip) tuples. ``label`` is a header filegroup
        and ``strip`` is the package-relative prefix to strip (usually
        ``include``) so files land under ``usr/include/<tree>``.
      pc_file: Label of the generated ``fastslide.pc`` pkg-config file, shipped
        under ``usr/lib/pkgconfig``.
    """

    header_files = []
    for index, (label, strip) in enumerate(headers):
        files_name = "{}_hdr_{}".format(name, index)
        pkg_files(
            name = files_name,
            srcs = [label],
            attributes = pkg_attributes(mode = "0644"),
            prefix = "usr/include",
            strip_prefix = strip_prefix.from_pkg(strip),
        )
        header_files.append(":" + files_name)

    pkg_files(
        name = name + "_pc",
        srcs = [pc_file],
        attributes = pkg_attributes(mode = "0644"),
        prefix = "usr/lib/pkgconfig",
        strip_prefix = strip_prefix.files_only(),
    )

    pkg_tar(
        name = name + "_data",
        srcs = header_files + [":" + name + "_pc"],
    )

    pkg_deb(
        name = name,
        data = ":" + name + "_data",
        architecture = "all",
        # libfmt-dev supplies <fmt/core.h>, which the public C++ SDK headers
        # pull in transitively (aifocore/concepts/numeric.h). It is a system
        # package rather than vendored so consumers use their distro's fmt.
        depends = [
            "libfastslide (= " + version + ")",
            "libfmt-dev",
        ],
        description = "FastSlide SDK headers (development files for libfastslide).",
        homepage = _HOMEPAGE,
        maintainer = _MAINTAINER,
        package = "libfastslide-dev",
        package_file_name = "libfastslide-dev_" + version + "_all.deb",
        priority = "optional",
        section = "libdevel",
        version = version,
    )
