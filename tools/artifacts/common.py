"""Shared helpers for artifact build scripts."""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
from pathlib import Path


def find_fastslide_workspace() -> Path:
    """Return the Bazel workspace root for the FastSlide module."""
    path = Path(__file__).resolve()
    for parent in path.parents:
        module_file = parent / "MODULE.bazel"
        if not module_file.is_file():
            continue
        if 'name = "fastslide"' in module_file.read_text(encoding="utf-8"):
            return parent
    raise RuntimeError('Could not locate the FastSlide Bazel workspace (MODULE.bazel with name = "fastslide").')


WORKSPACE_ROOT = find_fastslide_workspace()
VERSIONS_JSON = WORKSPACE_ROOT / "package" / "versions.json"


def run(cmd: list[str], *, env: dict[str, str]) -> None:
    subprocess.run(cmd, cwd=WORKSPACE_ROOT, check=True, env=env)


def run_capture(cmd: list[str], *, env: dict[str, str]) -> str:
    result = subprocess.run(
        cmd,
        cwd=WORKSPACE_ROOT,
        check=True,
        capture_output=True,
        env=env,
        text=True,
    )
    return result.stdout


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def safe_unlink(path: Path) -> None:
    if not path.exists():
        return
    try:
        path.unlink()
    except PermissionError:
        os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
        path.unlink()


def copy_to_dir(src: Path, dst_dir: Path, *, mode: int = 0o644) -> Path:
    """Copy src into dst_dir, overwriting if present, and chmod to mode."""
    if not src.exists():
        raise FileNotFoundError(f"Build output not found: {src}")
    ensure_dir(dst_dir)
    dst = dst_dir / src.name
    safe_unlink(dst)
    shutil.copy2(src, dst)
    os.chmod(dst, mode)
    return dst


def cquery_target_files(*, bazel_cmd: str, target: str, bazel_flags: list[str], env: dict[str, str]) -> list[Path]:
    """Return repo-root-relative file paths for a built target using `bazel cquery`."""
    out = run_capture(
        [
            bazel_cmd,
            "cquery",
            *bazel_flags,
            target,
            "--output=starlark",
            "--starlark:expr=" + '"\\n".join([f.path for f in target.files.to_list()])',
        ],
        env=env,
    )
    files: list[Path] = []
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        files.append(WORKSPACE_ROOT / line)
    return files


def read_fastslide_version() -> str:
    """Read the FastSlide version from versions.json."""
    data = json.loads(VERSIONS_JSON.read_text(encoding="utf-8"))
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
