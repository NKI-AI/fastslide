#!/usr/bin/env python3
"""Build FastSlide Java artifacts (JAR + JNI) for multiple platforms."""

from __future__ import annotations

import argparse

from artifacts import common, jars
from artifacts.specs import PLATFORMS


def main() -> None:
    common.force_utf8_stdio()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bazel",
        default="bazelisk",
        help="Bazel/Bazelisk command to invoke (default: bazelisk)",
    )
    parser.add_argument(
        "--platform",
        action="append",
        dest="platforms",
        choices=sorted(PLATFORMS.keys()),
        help="Platform(s) to build JNI for. Defaults to all supported platforms.",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="Continue building other platforms after a failure.",
    )
    parser.add_argument(
        "--bazel-arg",
        action="append",
        default=[],
        dest="bazel_args",
        help="Extra arg to pass through to Bazel (repeatable).",
    )
    parser.add_argument(
        "--native-host",
        action="store_true",
        help=(
            "Build the host platform with its own native toolchain instead of the "
            "hermetic Zig toolchain (no cross-compilation for the host platform)."
        ),
    )
    parser.add_argument(
        "--with-tool-jar",
        action="store_true",
        help="Also emit a self-contained fastslidetool JAR per platform (runnable via 'java -jar').",
    )
    args = parser.parse_args()

    platforms = args.platforms or list(PLATFORMS.keys())

    raise SystemExit(
        jars.build_jars(
            bazel_cmd=args.bazel,
            platforms=platforms,
            keep_going=args.keep_going,
            extra_bazel_args=args.bazel_args,
            native_host=args.native_host,
            with_tool_jar=args.with_tool_jar,
        )
    )


if __name__ == "__main__":
    main()
