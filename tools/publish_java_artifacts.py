#!/usr/bin/env python3
"""Publish the built FastSlide Java JARs locally or to a GitHub Release.

Examples:
    # Stage a local file:// repo to test the consume loop offline:
    python3 tools/publish_java_artifacts.py --dest local --out-dir /tmp/fastslide-release

    # Create a full GitHub Release (tag == version):
    python3 tools/publish_java_artifacts.py --dest github --release

    # Create a prerelease/snapshot (tag == <version>-dev.<shortsha>):
    python3 tools/publish_java_artifacts.py --dest github --snapshot
"""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path

from artifacts import common, release


def _short_sha() -> str:
    sha = os.environ.get("GITHUB_SHA", "").strip()
    if not sha:
        try:
            sha = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=common.WORKSPACE_ROOT,
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            sha = ""
    return sha[:7] if sha else "unknown"


def main() -> None:
    common.force_utf8_stdio()

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--dest",
        choices=("local", "github"),
        required=True,
        help="Where to publish: a local file:// repo layout, or a GitHub Release.",
    )
    parser.add_argument(
        "--jar-dir",
        type=Path,
        default=release.ARTIFACT_DIR,
        help=f"Directory containing the built JARs (default: {release.ARTIFACT_DIR}).",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=common.WORKSPACE_ROOT / "artifacts" / "release",
        help="Output directory for --dest local (default: artifacts/release).",
    )

    channel = parser.add_mutually_exclusive_group()
    channel.add_argument(
        "--release",
        dest="release_channel",
        action="store_const",
        const="release",
        help="Publish as a full release (tag == version, marked latest).",
    )
    channel.add_argument(
        "--snapshot",
        dest="release_channel",
        action="store_const",
        const="snapshot",
        help="Publish as a prerelease/snapshot (tag == <version>-dev.<shortsha>). [default]",
    )
    parser.set_defaults(release_channel="snapshot")

    parser.add_argument(
        "--repo",
        default=None,
        help="GitHub repo (owner/name) for --dest github (default: gh's current repo).",
    )
    parser.add_argument(
        "--require-all",
        action="store_true",
        help="Fail unless the wrapper + all per-platform native JARs are present.",
    )
    args = parser.parse_args()

    version = common.read_fastslide_version()
    collected = release.collect_jars(args.jar_dir, version)

    if args.require_all:
        release.require_complete(collected)

    jars = collected.present
    if not jars:
        missing = "\n  ".join(collected.missing)
        raise SystemExit(
            "No JARs found to publish. Build them first with\n"
            "  python3 tools/build_java_artifacts.py --platform <key>\n"
            f"Looked in: {args.jar_dir}\nExpected (none found):\n  {missing}"
        )

    if collected.missing:
        print("\u26a0 Publishing a partial artifact set; missing:")
        for name in collected.missing:
            print(f"    {name}")

    if args.dest == "local":
        dest = release.stage_local(jars, args.out_dir, version)
        print(f"\nStaged a local repository. Point the consumer at it with:\n  -PfastslideRepoUrl=file://{dest.parent}")
        return

    is_release = args.release_channel == "release"
    if is_release:
        tag = version
    else:
        tag = f"{version}-dev.{_short_sha()}"

    checksums = release.write_checksums(jars, args.jar_dir / release.CHECKSUMS_NAME)
    title = f"FastSlide Java {tag}"
    notes = (
        f"FastSlide Java artifacts for `{version}`.\n\n"
        "Consumed as Gradle ivy/url dependencies "
        "`dev.aifo:fastslide-java` and `dev.aifo:fastslide-native:<os>-<arch>`."
    )
    release.publish_github(
        jars,
        tag=tag,
        title=title,
        notes=notes,
        prerelease=not is_release,
        repo=args.repo,
        checksums=checksums,
    )


if __name__ == "__main__":
    main()
