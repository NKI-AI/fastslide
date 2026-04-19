"""Build FastSlide wheels for multiple platforms and Python versions."""

from __future__ import annotations

import os
import platform as host_platform
import subprocess

from . import common
from .specs import PLATFORMS, PY_TAG_TO_VERSION, PlatformSpec

ARTIFACT_DIR = common.REPO_ROOT / "aifo" / "fastslide" / "artifacts" / "wheels"


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
    python_tags: list[str],
    keep_going: bool,
    extra_bazel_args: list[str],
) -> int:
    env = os.environ.copy()
    failures: list[str] = []
    common.ensure_dir(ARTIFACT_DIR)
    is_macos = _is_macos_host()

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

        for py_tag in python_tags:
            if py_tag not in PY_TAG_TO_VERSION:
                raise ValueError(
                    f"Unsupported python tag '{py_tag}'. Supported: {', '.join(sorted(PY_TAG_TO_VERSION))}"
                )

        # Build all Python versions for this platform in a single Bazel
        # invocation so Bazel can parallelise the four transitioned configs.
        targets = [f"//aifo/fastslide/python:fastslide_wheel_{t}" for t in python_tags]
        tags_label = ", ".join(python_tags)
        print(f"\n▶︎ Building {tags_label} wheels for {platform_key} with {bazel_cmd}")

        try:
            common.run([bazel_cmd, "build", *bazel_flags, *targets], env=env)
        except subprocess.CalledProcessError:
            for t in python_tags:
                failures.append(f"{platform_key}:{t}")
            print(f"❌ Build failed for {platform_key}")
            if not keep_going:
                return 1
            continue

        for py_tag in python_tags:
            target = f"//aifo/fastslide/python:fastslide_wheel_{py_tag}"
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
                    print(f"  ✔ {py_tag} -> {dst}")
                    copied_any = True
                if not copied_any:
                    raise FileNotFoundError(f"No .whl outputs found for {target} (got {len(files)} files)")
            except Exception as e:
                failures.append(f"{platform_key}:{py_tag}")
                print(f"  ❌ Error collecting {py_tag} wheel for {platform_key}: {e}")
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
