"""Build FastSlide wheels for multiple platforms and Python versions."""

from __future__ import annotations

import platform as host_platform
import os
import subprocess
from pathlib import Path

from . import common
from .specs import PLATFORMS, PY_TAG_TO_VERSION

ARTIFACT_DIR = common.REPO_ROOT / "aifo" / "fastslide" / "artifacts" / "wheels"


def _is_macos_host() -> bool:
    return host_platform.system().lower() == "darwin"


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

        # rules_python does not currently provide a Python C/C++ toolchain for
        # Windows on AArch64, so Bazel cannot resolve
        # @rules_python//python/cc:toolchain_type for that target platform.
        # We skip these wheels rather than failing the entire build.
        if platform_key == "windows_arm64":
            print("⚠ Skipping windows_arm64 wheels: rules_python has no py_cc toolchain for Windows AArch64")
            continue

        for py_tag in python_tags:
            if py_tag not in PY_TAG_TO_VERSION:
                raise ValueError(
                    f"Unsupported python tag '{py_tag}'. Supported: {', '.join(sorted(PY_TAG_TO_VERSION))}"
                )
            py_version = PY_TAG_TO_VERSION[py_tag]
            target = f"//aifo/fastslide/python:fastslide_wheel_{py_tag}"

            bazel_flags = [
                f"--@rules_python//python/config_settings:python_version={py_version}",
                f"--platforms={spec.bazel_platform}",
            ]
            # Only disable hermetic for Darwin wheels *when running on macOS*.
            # On macOS, Zig's hermetic toolchain currently fails when linking
            # Python extension modules (e.g. unsupported -exported_symbol).
            should_use_hermetic = spec.use_hermetic and not (is_macos and platform_key.startswith("darwin_"))
            if should_use_hermetic:
                bazel_flags.append("--config=hermetic")
            bazel_flags.extend(extra_bazel_args)

            print(f"\n▶︎ Building {target} for {platform_key} ({py_tag}, Python {py_version}) with {bazel_cmd}")

            try:
                common.run([bazel_cmd, "build", *bazel_flags, target], env=env)
                files = common.cquery_target_files(
                    bazel_cmd=bazel_cmd,
                    target=target,
                    bazel_flags=bazel_flags,
                    env=env,
                )
                # Copy immediately when the target completes successfully.
                copied_any = False
                for f in files:
                    if f.suffix != ".whl":
                        continue
                    dst = common.copy_to_dir(f, ARTIFACT_DIR, mode=0o644)
                    print(f"✔ Copied wheel -> {dst}")
                    copied_any = True
                if not copied_any:
                    raise FileNotFoundError(f"No .whl outputs found for {target} (got {len(files)} files)")
            except subprocess.CalledProcessError:
                failures.append(f"{platform_key}:{py_tag}")
                print(f"❌ Failed: {platform_key} + {py_tag}")
                if not keep_going:
                    return 1
            except Exception as e:
                failures.append(f"{platform_key}:{py_tag}")
                print(f"❌ Error while collecting wheels for {platform_key} + {py_tag}: {e}")
                if not keep_going:
                    return 1
            else:
                print(f"✔ Success: {platform_key} + {py_tag}")

    if failures:
        print("\nCompleted with failures:")
        for item in failures:
            print(f" - {item}")
        return 1

    print("\nAll requested wheels built successfully.")
    print(f"Wheels are collected in: {ARTIFACT_DIR}")
    return 0
