"""Build FastSlide Java artifacts (JAR + native shared library) for one or more platforms.

Produces:
- Platform-independent wrapper JAR and tool deploy JAR (built once).
- Per-platform classifier JARs containing the native shared library at
  META-INF/native/<os>-<arch>/<libname>, ready for classpath-based auto-loading.
"""

from __future__ import annotations

import os
import platform as host_platform
import shutil
import subprocess
import zipfile
from pathlib import Path

from . import common
from .specs import PLATFORMS, PlatformSpec

ARTIFACT_DIR = common.REPO_ROOT / "aifo" / "fastslide" / "artifacts" / "jars"

_JAR_TARGETS = [
    "//aifo/fastslide/java:fastslide_java",
    "//aifo/fastslide/java:fastslidetool_java_deploy.jar",
]

_NATIVE_TARGETS: dict[str, str] = {
    "darwin_aarch64": "//aifo/fastslide/java:fastslide_native",
    "darwin_x86_64": "//aifo/fastslide/java:fastslide_native",
    "linux_x86_64": "//aifo/fastslide/java:fastslide_native",
    "linux_arm64": "//aifo/fastslide/java:fastslide_native",
    "windows_x86_64": "//aifo/fastslide/java:fastslide_native",
    "windows_arm64": "//aifo/fastslide/java:fastslide_native",
}


def _is_macos_host() -> bool:
    return host_platform.system().lower() == "darwin"


def _platform_bazel_flags(
    spec: PlatformSpec,
    platform_key: str,
    *,
    is_macos: bool,
    extra_bazel_args: list[str],
) -> list[str]:
    flags = [f"--platforms={spec.bazel_platform}"]
    should_use_hermetic = spec.use_hermetic and not (is_macos and platform_key.startswith("darwin_"))
    if should_use_hermetic:
        flags.append("--config=hermetic")
    flags.extend(extra_bazel_args)
    return flags


def _pick_single(paths: list[Path], *, what: str) -> Path:
    if not paths:
        raise FileNotFoundError(f"Expected to find {what}, but none were found.")
    if len(paths) != 1:
        joined = "\n".join(str(p) for p in paths)
        raise RuntimeError(f"Expected exactly one {what}, but found {len(paths)}:\n{joined}")
    return paths[0]


def _native_lib_filename(spec: PlatformSpec) -> str:
    """Expected native library filename for a given platform."""
    match spec.os_name:
        case "windows":
            return "fastslide.dll"
        case "darwin":
            return "libfastslide.dylib"
        case _:
            return "libfastslide.so"


def _create_classifier_jar(
    native_lib: Path,
    spec: PlatformSpec,
    version: str,
    out_dir: Path,
) -> Path:
    """Package the native library into a classifier JAR at META-INF/native/<os>-<arch>/."""
    jar_name = f"fastslide-native-{version}-{spec.os_name}-{spec.arch}.jar"
    jar_path = out_dir / jar_name
    entry_path = f"META-INF/native/{spec.os_name}-{spec.arch}/{_native_lib_filename(spec)}"

    common.safe_unlink(jar_path)
    common.ensure_dir(out_dir)

    with zipfile.ZipFile(jar_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.write(native_lib, entry_path)

    os.chmod(jar_path, 0o644)
    return jar_path


def _create_platform_tool_jar(
    tool_jar: Path,
    native_lib: Path,
    spec: PlatformSpec,
    version: str,
    out_dir: Path,
) -> Path:
    """Copy the deploy JAR and inject the native library so `java -jar` just works."""
    jar_name = f"fastslidetool-java-{version}-{spec.os_name}-{spec.arch}.jar"
    jar_path = out_dir / jar_name
    entry_path = f"META-INF/native/{spec.os_name}-{spec.arch}/{_native_lib_filename(spec)}"

    common.safe_unlink(jar_path)
    common.ensure_dir(out_dir)
    shutil.copy2(tool_jar, jar_path)

    with zipfile.ZipFile(jar_path, "a", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.write(native_lib, entry_path)

    os.chmod(jar_path, 0o644)
    return jar_path


def build_jars(
    *,
    bazel_cmd: str,
    platforms: list[str],
    keep_going: bool,
    extra_bazel_args: list[str],
) -> int:
    """Build Java JARs and per-platform classifier JARs, copying them to ARTIFACT_DIR."""
    env = os.environ.copy()
    common.ensure_dir(ARTIFACT_DIR)
    is_macos = _is_macos_host()
    failures: list[str] = []

    try:
        version = common.read_fastslide_version()
    except Exception as e:
        print(f"\u274c Error reading FastSlide version: {e}")
        return 1

    print(f"\n\u25b6\ufe0e Building platform-independent JARs with {bazel_cmd}")
    try:
        common.run([bazel_cmd, "build", *_JAR_TARGETS], env=env)
    except subprocess.CalledProcessError:
        print("\u274c JAR build failed.")
        return 1

    bazel_bin_str = common.run_capture([bazel_cmd, "info", "bazel-bin"], env=env).strip()
    java_out = Path(bazel_bin_str) / "aifo" / "fastslide" / "java"
    wrapper_jar = _pick_single(list(java_out.glob("libfastslide_java.jar")), what="wrapper jar")
    tool_jar = _pick_single(list(java_out.glob("fastslidetool_java_deploy.jar")), what="tool deploy jar")

    versioned_wrapper = ARTIFACT_DIR / f"fastslide-java-{version}.jar"
    versioned_tool = ARTIFACT_DIR / f"fastslidetool-java-{version}-deploy.jar"
    common.safe_unlink(versioned_wrapper)
    common.safe_unlink(versioned_tool)
    shutil.copy2(wrapper_jar, versioned_wrapper)
    os.chmod(versioned_wrapper, 0o644)
    shutil.copy2(tool_jar, versioned_tool)
    os.chmod(versioned_tool, 0o644)

    print(f"\u2714 {versioned_wrapper.name}")
    print(f"\u2714 {versioned_tool.name}")

    for platform_key in platforms:
        if platform_key not in PLATFORMS:
            raise ValueError(f"Unsupported platform '{platform_key}'. Supported: {', '.join(sorted(PLATFORMS))}")

        spec = PLATFORMS[platform_key]
        native_target = _NATIVE_TARGETS[platform_key]
        bazel_flags = _platform_bazel_flags(spec, platform_key, is_macos=is_macos, extra_bazel_args=extra_bazel_args)

        print(f"\n\u25b6\ufe0e Building native library for {platform_key} with {bazel_cmd}")
        try:
            common.run([bazel_cmd, "build", *bazel_flags, native_target], env=env)

            files = common.cquery_target_files(
                bazel_cmd=bazel_cmd,
                target=native_target,
                bazel_flags=bazel_flags,
                env=env,
            )
            native_candidates = [p for p in files if p.suffix.lower() in (".so", ".dylib", ".dll")]
            native_lib = _pick_single(native_candidates, what=f"native shared library for {platform_key}")

            classifier_jar = _create_classifier_jar(native_lib, spec, version, ARTIFACT_DIR)
            print(f"\u2714 {classifier_jar.name}")

        except subprocess.CalledProcessError:
            print(f"\u274c Build failed for {platform_key}.")
            failures.append(platform_key)
            if not keep_going:
                return 1
        except Exception as e:
            print(f"\u274c Error processing {platform_key}: {e}")
            failures.append(platform_key)
            if not keep_going:
                return 1

    if failures:
        print(f"\n\u26a0 Completed with failures in: {', '.join(failures)}")
        return 1

    print("\nAll requested Java artifacts built successfully.")
    print(f"Artifacts are collected in: {ARTIFACT_DIR}")
    return 0
