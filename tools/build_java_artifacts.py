#!/usr/bin/env python3
"""Build FastSlide Java artifacts (JAR + JNI) for multiple platforms."""

from __future__ import annotations

import argparse

from artifacts import jars
from artifacts.specs import PLATFORMS


def main() -> None:
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
    args = parser.parse_args()

    platforms = args.platforms or list(PLATFORMS.keys())

    raise SystemExit(
        jars.build_jars(
            bazel_cmd=args.bazel,
            platforms=platforms,
            keep_going=args.keep_going,
            extra_bazel_args=args.bazel_args,
        )
    )


if __name__ == "__main__":
    main()
