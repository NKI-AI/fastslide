"""Build the FastSlide Java native classifier JAR with Meson (no Bazel).

This is the standalone Windows path: Bazel has no Windows host support (the
aspect_rules_py / rules_uv toolchains are unavailable), and cross-compiling the
native library with Zig produces a broken arm64 PE. Instead, compile the C API
as a self-contained shared library natively on the Windows runner with MSVC via
Meson -- the same build system that produces the wheels -- and package the
resulting DLL into the classifier JAR that the Java FFM loader expects.

Only the per-platform native classifier JAR is produced here. The
platform-independent wrapper / tool JARs are still built by the Bazel path on a
Linux runner.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

from . import common
from .jars import ARTIFACT_DIR, _create_classifier_jar
from .specs import PLATFORMS, PlatformSpec

# Build directory for the shared C API. Kept distinct from any meson-python /
# wheel build dir so the two never clash.
_BUILD_DIR = common.WORKSPACE_ROOT / "builddir-java-native"

# Meson setup options. Mirrors the wheel build's dependency posture (everything
# static + dep test suites disabled) but targets the shared C API instead of the
# Python extension. b_vscrt=static_from_buildtype links the MSVC runtime
# statically so the DLL needs no VC++ redistributable on the end-user machine.
_SETUP_OPTIONS = [
    "--wrap-mode=forcefallback",
    "-Dbuildtype=release",
    "-Ddefault_library=static",
    "-Dbuild_c_api=true",
    "-Dshared_c_api=true",
    "-Dbuild_python=false",
    "-Dbuild_tests=false",
    "-Dbuild_tool=false",
    "-Db_vscrt=static_from_buildtype",
    # Trim dependency test suites that would otherwise compile for nothing
    # (matches pyproject.toml's wheel setup args).
    "-Dsimpletiff:default_library=static",
    "-Dsimpletiff:build_tests=false",
    "-Dhighway:test_standalone=true",
    "-Dpugixml:tests=false",
    "-Dlibdicom:tests=false",
]


def _strip_gnu_link_from_path(env: dict[str, str]) -> None:
    """Drop Git-for-Windows Unix-tool dirs that shadow the MSVC linker on PATH.

    Git Bash prepends ``C:\\Program Files\\Git\\usr\\bin`` (which ships a GNU
    ``link.exe``) to PATH, so Meson's linker probe can pick it over the MSVC
    ``link.exe`` and abort with "Found GNU link.exe instead of MSVC link.exe".
    Remove only directories that hold a ``link.exe`` next to Unix coreutils
    (``sh.exe`` / ``cygpath.exe``) but no MSVC ``cl.exe`` -- i.e. exactly the
    Git Bash tool dirs, never the real MSVC toolchain. No-op off Windows.

    Args:
        env: Environment mapping to mutate in place (its ``PATH`` is rewritten).
    """
    if os.name != "nt":
        return
    kept: list[str] = []
    for entry in env.get("PATH", "").split(os.pathsep):
        d = Path(entry) if entry else None
        is_gnu_link_dir = (
            d is not None
            and (d / "link.exe").exists()
            and not (d / "cl.exe").exists()
            and ((d / "sh.exe").exists() or (d / "cygpath.exe").exists())
        )
        if not is_gnu_link_dir:
            kept.append(entry)
    env["PATH"] = os.pathsep.join(kept)


def _find_shared_lib(build_dir: Path, *, env: dict[str, str]) -> Path:
    """Locate the built fastslide_c shared library via `meson introspect`."""
    out = common.run_capture(["meson", "introspect", str(build_dir), "--targets"], env=env)
    targets = json.loads(out)
    for target in targets:
        if target.get("name") != "fastslide_c" or target.get("type") != "shared library":
            continue
        for filename in target.get("filename", []):
            path = Path(filename)
            if not path.is_absolute():
                path = build_dir / path
            if path.suffix.lower() in (".dll", ".so", ".dylib"):
                return path
    raise FileNotFoundError(
        f"Could not locate the fastslide_c shared library in {build_dir} via 'meson introspect'."
    )


def build_native_jar(platform_key: str) -> int:
    """Meson-build the shared C API and package it into the classifier JAR.

    Args:
        platform_key: A key into ``PLATFORMS`` (e.g. ``windows_x86_64``). The
            build is native, so this must match the runner's own OS/arch; it
            only selects the ``<os>-<arch>`` classifier used to name and lay out
            the JAR.

    Returns:
        Process exit code (0 on success).
    """
    if platform_key not in PLATFORMS:
        raise ValueError(f"Unsupported platform '{platform_key}'. Supported: {', '.join(sorted(PLATFORMS))}")
    spec: PlatformSpec = PLATFORMS[platform_key]

    env = os.environ.copy()
    _strip_gnu_link_from_path(env)
    version = common.read_fastslide_version()
    common.ensure_dir(ARTIFACT_DIR)

    # Reconfigure if the build dir already exists (e.g. a warm CI cache).
    setup_cmd = ["meson", "setup", str(_BUILD_DIR), str(common.WORKSPACE_ROOT), *_SETUP_OPTIONS]
    if (_BUILD_DIR / "meson-info").is_dir():
        setup_cmd.append("--reconfigure")

    print(f"\n\u25b6\ufe0e Configuring Meson for the {platform_key} native C API")
    common.run(setup_cmd, env=env)

    print(f"\u25b6\ufe0e Compiling the shared C API for {platform_key}")
    common.run(["meson", "compile", "-C", str(_BUILD_DIR), "fastslide_c"], env=env)

    native_lib = _find_shared_lib(_BUILD_DIR, env=env)
    classifier_jar = _create_classifier_jar(native_lib, spec, version, ARTIFACT_DIR)
    print(f"\u2714 {classifier_jar.name}")
    print(f"Artifact collected in: {ARTIFACT_DIR}")
    return 0
