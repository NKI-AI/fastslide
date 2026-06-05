#!/usr/bin/env python3
"""On-target smoke test for the shipped FastSlide Java artifacts.

This runs the *exact* JARs that get published, loaded the same way a downstream
consumer (e.g. the QuPath extension) loads them: the platform-independent
wrapper JAR plus the per-platform classifier JAR on the classpath, with the
native library resolved through ``META-INF/native/<os>-<arch>/`` (the
``NativeLoader.tryLoadFromClasspath`` code path). It then runs
``FastSlideTool info`` against a small bundled sample slide, which forces the
native library to actually load and decode on the real OS/arch of the runner.

Because it deliberately avoids ``-Dfastslide.native.path`` (the Bazel-internal
shortcut), a green run proves the classifier JAR is wired up correctly for
production use.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from artifacts import common
from artifacts.jars import host_platform_key
from artifacts.specs import PLATFORMS

ARTIFACT_DIR = common.WORKSPACE_ROOT / "artifacts" / "jars"
DEFAULT_SAMPLE = common.WORKSPACE_ROOT / "tests" / "test_data" / "CMU-1-Small-Region.svs"
MAIN_CLASS = "dev.aifo.fastslide.tools.FastSlideTool"
SUCCESS_MARKER = "Successfully read slide information!"


def _java_executable() -> str:
    java_home = os.environ.get("JAVA_HOME", "").strip()
    if java_home:
        candidate = Path(java_home) / "bin" / ("java.exe" if os.name == "nt" else "java")
        if candidate.is_file():
            return str(candidate)
    return "java"


def _resolve_jars(jar_dir: Path, version: str, platform_key: str) -> tuple[Path, Path]:
    spec = PLATFORMS[platform_key]
    wrapper = jar_dir / f"fastslide-java-{version}.jar"
    classifier = jar_dir / f"fastslide-native-{version}-{spec.os_name}-{spec.arch}.jar"

    missing = [str(p) for p in (wrapper, classifier) if not p.is_file()]
    if missing:
        joined = "\n  ".join(missing)
        raise FileNotFoundError(
            "Missing artifact(s) required for the smoke test. Build them first with\n"
            f"  python3 tools/build_java_artifacts.py --platform {platform_key}\n"
            f"Missing:\n  {joined}"
        )
    return wrapper, classifier


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--platform",
        choices=sorted(PLATFORMS.keys()),
        default=host_platform_key(),
        help="Platform key whose classifier JAR to test (default: the host platform).",
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=DEFAULT_SAMPLE,
        help=f"Sample slide to open (default: {DEFAULT_SAMPLE}).",
    )
    parser.add_argument(
        "--jar-dir",
        type=Path,
        default=ARTIFACT_DIR,
        help=f"Directory containing the built JARs (default: {ARTIFACT_DIR}).",
    )
    parser.add_argument(
        "--java",
        default=_java_executable(),
        help="Java launcher to use (default: $JAVA_HOME/bin/java or 'java').",
    )
    args = parser.parse_args()

    if args.platform is None:
        parser.error(
            "Could not determine the host platform; pass --platform explicitly "
            f"(one of: {', '.join(sorted(PLATFORMS))})."
        )

    if not args.input.is_file():
        parser.error(f"Sample slide not found: {args.input}")

    version = common.read_fastslide_version()
    wrapper, classifier = _resolve_jars(args.jar_dir, version, args.platform)

    classpath = os.pathsep.join([str(wrapper), str(classifier)])
    cmd = [
        args.java,
        "--enable-native-access=ALL-UNNAMED",
        "-cp",
        classpath,
        MAIN_CLASS,
        "info",
        "--input",
        str(args.input),
    ]

    print(f"\u25b6\ufe0e Smoke testing {args.platform} artifacts (FastSlide {version})")
    print(f"  java:       {args.java}")
    print(f"  wrapper:    {wrapper.name}")
    print(f"  classifier: {classifier.name}")
    print(f"  sample:     {args.input}")
    print("  command:    " + " ".join(cmd))

    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)

    if result.returncode != 0:
        print(f"\n\u274c Smoke test failed (exit code {result.returncode}).", file=sys.stderr)
        raise SystemExit(1)

    if SUCCESS_MARKER not in result.stdout:
        print(
            f"\n\u274c Smoke test ran but did not report success (missing marker: {SUCCESS_MARKER!r}).",
            file=sys.stderr,
        )
        raise SystemExit(1)

    print(f"\n\u2714 Smoke test passed for {args.platform}.")


if __name__ == "__main__":
    main()
