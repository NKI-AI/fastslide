#!/usr/bin/env python3
"""Build FastSlide shared libraries for multiple platforms using Zig SDK.

This helper drives Bazel with the hermetic Zig toolchains configured in
`.bazelrc` (`--config=hermetic`) to produce platform-specific shared
libraries for Linux, macOS, and Windows on both x86_64 (amd64) and arm64.

Outputs are copied into `aifo/fastslide/artifacts/shared` and renamed to
include the target platform in the filename to make distribution simpler.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import stat
from pathlib import Path
from typing import Dict, NamedTuple, List


REPO_ROOT = Path(__file__).resolve().parents[3]
ARTIFACT_DIR = REPO_ROOT / "aifo" / "fastslide" / "artifacts" / "shared"


class PlatformInfo(NamedTuple):
    bazel_platform: str
    output_filename: str
    artifact_filename: str


# Mapping of friendly platform name -> Platform configuration
TARGETS: Dict[str, PlatformInfo] = {
    "linux_x86_64": PlatformInfo(
        "@zig_sdk//platform:linux_amd64",
        "libfastslide.so",
        "libfastslide-linux-x86_64.so",
    ),
    # This is tricky because we do not know the precise platform of the machine
    # "linux_arm64": PlatformInfo(
    #     "@zig_sdk//platform:linux_arm64",
    #     "libfastslide.so",
    #     "libfastslide-linux-arm64.so",
    # ),
    "darwin_x86_64": PlatformInfo(
        "@zig_sdk//platform:macos_amd64",
        "libfastslide.dylib",
        "libfastslide-macos-x86_64.dylib",
    ),
    "darwin_aarch64": PlatformInfo(
        "@zig_sdk//platform:macos_arm64",
        "libfastslide.dylib",
        "libfastslide-macos-arm64.dylib",
    ),
    "windows_x86_64": PlatformInfo(
        "@zig_sdk//platform:windows_amd64",
        "fastslide.dll",
        "fastslide-windows-x86_64.dll",
    ),
    "windows_arm64": PlatformInfo(
        "@zig_sdk//platform:windows_arm64",
        "fastslide.dll",
        "fastslide-windows-arm64.dll",
    ),
}


def run_command(cmd: list[str], *, env: dict[str, str]) -> str:
    """Run a command and return stdout as text."""
    result = subprocess.run(cmd, cwd=REPO_ROOT, check=True, capture_output=True, env=env)
    return result.stdout.decode().strip()


def build_shared_objects(bazel_cmd: str, platforms: list[str]) -> None:
    env = os.environ.copy()

    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)

    failures: List[str] = []

    for platform_key in platforms:
        if platform_key not in TARGETS:
            raise ValueError(f"Unsupported platform '{platform_key}'. Supported: {', '.join(TARGETS.keys())}")

        info = TARGETS[platform_key]

        # We build the :fastslide alias target, which resolves to the
        # platform-specific genrule (e.g., :fastslide_linux) that outputs
        # the correctly named shared library file (e.g., libfastslide.so).
        target = "//aifo/fastslide:fastslide"

        build_cmd = [
            bazel_cmd,
            "build",
            "--config=hermetic",
            f"--platforms={info.bazel_platform}",
            target,
        ]

        print(f"\n▶︎ Building {target} for {platform_key} ({info.bazel_platform}) with {bazel_cmd} (Zig)")

        try:
            subprocess.run(build_cmd, cwd=REPO_ROOT, check=True, env=env)

            bazel_bin = Path(
                run_command(
                    [
                        bazel_cmd,
                        "info",
                        "bazel-bin",
                        "--config=hermetic",
                        f"--platforms={info.bazel_platform}",
                    ],
                    env=env,
                )
            )

            # The output is located in the package directory within bazel-bin
            built_artifact = bazel_bin / "aifo" / "fastslide" / info.output_filename
            if not built_artifact.exists():
                raise FileNotFoundError(f"Expected build output not found: {built_artifact}")

            destination = ARTIFACT_DIR / info.artifact_filename

            # Handle permission errors if destination exists and is read-only
            if destination.exists():
                try:
                    destination.unlink()
                except PermissionError:
                    os.chmod(destination, stat.S_IWRITE | stat.S_IREAD)
                    destination.unlink()

            shutil.copy2(built_artifact, destination)

            # Ensure the new copy is writable so we can overwrite it next time
            os.chmod(destination, 0o755)

            print(f"✔ Copied {built_artifact} -> {destination}")

        except subprocess.CalledProcessError:
            print(f"❌ Build failed for {platform_key}. Skipping...")
            failures.append(platform_key)
        except Exception as e:
            print(f"❌ Error processing {platform_key}: {e}. Skipping...")
            failures.append(platform_key)

    if failures:
        print(f"\n⚠️  Completed with failures in: {', '.join(failures)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bazel",
        default="bazel",
        help="Bazel/Bazelisk command to invoke (default: bazel)",
    )
    parser.add_argument(
        "--platform",
        action="append",
        dest="platforms",
        choices=sorted(TARGETS.keys()),
        help="Platform(s) to build. Defaults to all supported platforms.",
    )
    args = parser.parse_args()

    platforms = args.platforms or list(TARGETS.keys())
    build_shared_objects(args.bazel, platforms)


if __name__ == "__main__":
    main()
