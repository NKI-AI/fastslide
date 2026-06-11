#!/usr/bin/env python3
"""Build the FastSlide Java native classifier JAR with Meson (standalone, no Bazel).

Used for native Windows builds, where Bazel has no host support. Produces only
the per-platform native classifier JAR; the wrapper JAR comes from the Bazel
path on Linux.
"""

from __future__ import annotations

import argparse

from artifacts import common, jars_meson
from artifacts.specs import PLATFORMS


def main() -> None:
    common.force_utf8_stdio()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--platform",
        required=True,
        choices=sorted(PLATFORMS.keys()),
        help="Platform to build the native classifier JAR for (must match the host OS/arch).",
    )
    args = parser.parse_args()

    raise SystemExit(jars_meson.build_native_jar(args.platform))


if __name__ == "__main__":
    main()
