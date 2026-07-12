"""CLI for building FastSlide artifacts (wheels + JARs)."""

from __future__ import annotations

import argparse

from . import jars, wheels
from .specs import PLATFORMS, PY_TAG_TO_VERSION


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
        help="Platform(s) to build. Defaults to all supported platforms.",
    )
    parser.add_argument(
        "--python",
        action="append",
        dest="python_tags",
        choices=sorted(PY_TAG_TO_VERSION.keys()),
        help="Python tag(s) to build (e.g. cp311). Defaults to all supported tags.",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="Continue building other artifacts after a failure.",
    )
    parser.add_argument(
        "--skip-wheels",
        action="store_true",
        help="Skip building wheels.",
    )
    parser.add_argument(
        "--skip-jars",
        action="store_true",
        help="Skip building Java JARs + JNI.",
    )
    parser.add_argument(
        "--bazel-arg",
        action="append",
        default=[],
        dest="bazel_args",
        help="Extra arg to pass through to Bazel (repeatable), e.g. --bazel-arg=--verbose_failures",
    )
    args = parser.parse_args()

    platforms = args.platforms or list(PLATFORMS.keys())
    python_tags = args.python_tags or list(PY_TAG_TO_VERSION.keys())

    exit_code = 0

    if not args.skip_wheels:
        wheels_rc = wheels.build_wheels(
            bazel_cmd=args.bazel,
            platforms=platforms,
            python_tags=python_tags,
            keep_going=args.keep_going,
            extra_bazel_args=args.bazel_args,
        )
        if wheels_rc != 0:
            exit_code = 1
            if not args.keep_going:
                raise SystemExit(exit_code)

    if not args.skip_jars:
        jars_rc = jars.build_jars(
            bazel_cmd=args.bazel,
            platforms=platforms,
            keep_going=args.keep_going,
            extra_bazel_args=args.bazel_args,
        )
        if jars_rc != 0:
            exit_code = 1

    raise SystemExit(exit_code)
