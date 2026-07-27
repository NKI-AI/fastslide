"""Build FastSlide wheels for multiple platforms and Python versions."""

from __future__ import annotations

import os
import platform as host_platform
import subprocess

from . import common
from .specs import PLATFORMS, PlatformSpec

ARTIFACT_DIR = common.WORKSPACE_ROOT / "artifacts" / "wheels"


def _is_macos_host() -> bool:
    return host_platform.system().lower() == "darwin"


def _platform_bazel_flags(
    spec: PlatformSpec,
    platform_key: str,
    *,
    is_macos: bool,
    extra_bazel_args: list[str],
) -> list[str]:
    """Build the Bazel flags shared by every Python version on a given platform."""
    flags = [f"--platforms={spec.bazel_platform}"]

    should_use_hermetic = spec.use_hermetic and not (is_macos and platform_key.startswith("darwin_"))
    if should_use_hermetic:
        flags.append("--config=hermetic")
    flags.extend(extra_bazel_args)
    return flags


def build_wheels(
    *,
    bazel_cmd: str,
    platforms: list[str],
    keep_going: bool,
    extra_bazel_args: list[str],
) -> int:
    env = os.environ.copy()
    failures: list[str] = []
    common.ensure_dir(ARTIFACT_DIR)
    is_macos = _is_macos_host()

    target = "//python:fastslide_wheel"

    for platform_key in platforms:
        if platform_key not in PLATFORMS:
            raise ValueError(f"Unsupported platform '{platform_key}'. Supported: {', '.join(sorted(PLATFORMS))}")
        spec = PLATFORMS[platform_key]

        if platform_key == "windows_arm64":
            print("⚠ Skipping windows_arm64 wheels: rules_python has no py_cc toolchain for Windows AArch64")
            continue

        bazel_flags = _platform_bazel_flags(
            spec,
            platform_key,
            is_macos=is_macos,
            extra_bazel_args=extra_bazel_args,
        )

        # A single stable-ABI (cp312-abi3) wheel per platform.
        print(f"\n▶︎ Building cp312-abi3 wheel for {platform_key} with {bazel_cmd}")

        try:
            common.run([bazel_cmd, "build", *bazel_flags, target], env=env)
        except subprocess.CalledProcessError:
            failures.append(platform_key)
            print(f"❌ Build failed for {platform_key}")
            if not keep_going:
                return 1
            continue

        try:
            files = common.cquery_target_files(
                bazel_cmd=bazel_cmd,
                target=target,
                bazel_flags=bazel_flags,
                env=env,
            )
            copied_any = False
            for f in files:
                if f.suffix != ".whl":
                    continue
                dst = common.copy_to_dir(f, ARTIFACT_DIR, mode=0o644)
                print(f"  ✔ {platform_key} -> {dst}")
                copied_any = True
            if not copied_any:
                raise FileNotFoundError(f"No .whl outputs found for {target} (got {len(files)} files)")
        except Exception as e:
            failures.append(platform_key)
            print(f"  ❌ Error collecting wheel for {platform_key}: {e}")
            if not keep_going:
                return 1

    if failures:
        print("\nCompleted with failures:")
        for item in failures:
            print(f" - {item}")
        return 1

    print("\nAll requested wheels built successfully.")
    print(f"Wheels are collected in: {ARTIFACT_DIR}")
    return 0
