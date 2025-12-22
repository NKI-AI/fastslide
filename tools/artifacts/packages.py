"""Build FastSlide per-platform .tar.xz bundles and collect them under artifacts/."""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

from . import common
from .specs import PLATFORMS

ARTIFACT_DIR = common.REPO_ROOT / "aifo" / "fastslide" / "artifacts" / "packages"
VERSIONS_JSON = common.REPO_ROOT / "aifo" / "fastslide" / "package" / "versions.json"


@dataclass(frozen=True)
class BundleInfo:
    bazel_platform: str
    bazel_target: str


def _read_fastslide_version() -> str:
    data = json.loads(VERSIONS_JSON.read_text(encoding="utf-8"))
    # Supported schemas:
    #  - {"version": "0.1.0"}  (legacy/simple)
    #  - {"versions": [{"id": "fastslide", "version": "0.1.0", ...}, ...]}  (current)
    version = data.get("version")
    if isinstance(version, str) and version:
        return version

    entries = data.get("versions")
    if isinstance(entries, list):
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            if entry.get("id") != "fastslide":
                continue
            entry_version = entry.get("version")
            if isinstance(entry_version, str) and entry_version:
                return entry_version

    raise ValueError(
        "Invalid versions.json: expected either a top-level string field 'version' "
        "or a list field 'versions' containing an entry with id='fastslide' and a string 'version'. "
        f"File: {VERSIONS_JSON}"
    )


def _targets_for_version(version: str) -> dict[str, BundleInfo]:
    return {
        "linux_x86_64": BundleInfo(
            bazel_platform=PLATFORMS["linux_x86_64"].bazel_platform,
            bazel_target=f"//aifo/fastslide/package:fastslide-bin-{version}-linux-x86_64",
        ),
        "linux_arm64": BundleInfo(
            bazel_platform=PLATFORMS["linux_arm64"].bazel_platform,
            bazel_target=f"//aifo/fastslide/package:fastslide-bin-{version}-linux-aarch64",
        ),
        "darwin_x86_64": BundleInfo(
            bazel_platform=PLATFORMS["darwin_x86_64"].bazel_platform,
            bazel_target=f"//aifo/fastslide/package:fastslide-bin-{version}-macos-x86_64",
        ),
        "darwin_aarch64": BundleInfo(
            bazel_platform=PLATFORMS["darwin_aarch64"].bazel_platform,
            bazel_target=f"//aifo/fastslide/package:fastslide-bin-{version}-macos-arm64",
        ),
        "windows_x86_64": BundleInfo(
            bazel_platform=PLATFORMS["windows_x86_64"].bazel_platform,
            bazel_target=f"//aifo/fastslide/package:fastslide-bin-{version}-windows-x86_64",
        ),
        "windows_arm64": BundleInfo(
            bazel_platform=PLATFORMS["windows_arm64"].bazel_platform,
            bazel_target=f"//aifo/fastslide/package:fastslide-bin-{version}-windows-arm64",
        ),
    }


def build_packages_keep_going(bazel_cmd: str, platforms: list[str], *, keep_going: bool) -> int:
    """Build .tar.xz bundles and copy each one immediately after it is built."""
    env = os.environ.copy()
    common.ensure_dir(ARTIFACT_DIR)

    try:
        version = _read_fastslide_version()
    except Exception as e:
        print(f"❌ Error reading FastSlide version: {e}")
        return 1
    targets = _targets_for_version(version)

    failures: list[str] = []
    for platform_key in platforms:
        if platform_key not in targets:
            raise ValueError(f"Unsupported platform '{platform_key}'. Supported: {', '.join(sorted(targets))}")

        info = targets[platform_key]
        bazel_flags = [
            "--config=hermetic",
            f"--platforms={info.bazel_platform}",
        ]

        print(f"\n▶︎ Building {info.bazel_target} for {platform_key} ({info.bazel_platform}) with {bazel_cmd}")

        try:
            common.run([bazel_cmd, "build", *bazel_flags, info.bazel_target], env=env)

            files = common.cquery_target_files(
                bazel_cmd=bazel_cmd,
                target=info.bazel_target,
                bazel_flags=bazel_flags,
                env=env,
            )

            # Copy immediately when the target completes successfully.
            copied_any = False
            for f in files:
                if f.suffixes[-2:] != [".tar", ".xz"]:
                    continue
                dst = common.copy_to_dir(f, ARTIFACT_DIR, mode=0o644)
                print(f"✔ Copied package -> {dst}")
                copied_any = True
            if not copied_any:
                raise FileNotFoundError(f"No .tar.xz outputs found for {info.bazel_target} (got {len(files)} files)")

        except subprocess.CalledProcessError:
            print(f"❌ Build failed for {platform_key}.")
            failures.append(platform_key)
            if not keep_going:
                return 1
        except Exception as e:
            print(f"❌ Error processing {platform_key}: {e}")
            failures.append(platform_key)
            if not keep_going:
                return 1

    if failures:
        print(f"\n⚠ Completed with failures in: {', '.join(failures)}")
        return 1
    return 0


