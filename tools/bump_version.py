#!/usr/bin/env python3
"""Interactively bump the FastSlide version across the repository.

This tool finds and updates FastSlide's version in a small, curated set of files
that are expected to stay in sync (Bazel packaging, Python metadata, docs).

Workflow:
  1) Reads the current version.
  2) Prompts for a new version (unless --new-version is provided).
  3) Prints exactly which files/fields will be updated.
  4) Asks for confirmation before writing.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
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


@dataclass(frozen=True)
class PlannedEdit:
    path: Path
    description: str
    old: str
    new: str


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _write_text(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def _validate_version(version: str) -> None:
    # Keep it strict/simple: semantic versions like 1.2.3 (optionally with -rc.1 / +local).
    # Note: place '-' at the end of the character class to avoid unintended ranges.
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.+-]+)?", version):
        raise ValueError(f"Invalid version '{version}'. Expected semver-like 'X.Y.Z' (optionally with -suffix/+meta).")


def _read_current_fastslide_version() -> str:
    versions_json = WORKSPACE_ROOT / "package" / "versions.json"
    data = json.loads(_read_text(versions_json))
    entries = data.get("versions")
    if isinstance(entries, list):
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            if entry.get("id") != "fastslide":
                continue
            v = entry.get("version")
            if isinstance(v, str) and v:
                return v
    raise ValueError(f"Could not determine current FastSlide version from {versions_json}")


def _plan_regex_sub(
    *,
    path: Path,
    description: str,
    pattern: str,
    replacement: str,
    flags: int = 0,
    expected_matches: int | None = 1,
) -> PlannedEdit:
    content = _read_text(path)
    new_content, n = re.subn(pattern, replacement, content, flags=flags)
    if expected_matches is None:
        if n < 1:
            raise ValueError(f"{path}: expected at least 1 match for {description} (got {n})")
    elif n != expected_matches:
        raise ValueError(f"{path}: expected exactly {expected_matches} match(es) for {description} (got {n})")
    return PlannedEdit(path=path, description=description, old=content, new=new_content)


def _plan_versions_json_update(*, new_version: str) -> PlannedEdit:
    path = WORKSPACE_ROOT / "package" / "versions.json"
    data = json.loads(_read_text(path))
    entries = data.get("versions")
    if not isinstance(entries, list):
        raise ValueError(f"{path}: expected 'versions' to be a list")
    changed = False
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        if entry.get("id") == "fastslide":
            entry["version"] = new_version
            changed = True
    if not changed:
        raise ValueError(f"{path}: no entry with id='fastslide' found")
    # Preserve a stable, readable formatting.
    new_text = json.dumps(data, indent=4, sort_keys=False) + "\n"
    return PlannedEdit(
        path=path, description="Update fastslide version in versions.json", old=_read_text(path), new=new_text
    )


def _collect_plans(*, current_version: str, new_version: str) -> list[PlannedEdit]:
    plans: list[PlannedEdit] = []

    # Primary source of truth for bundle naming.
    plans.append(_plan_versions_json_update(new_version=new_version))

    # Bazel constants.
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "package" / "BUILD.bazel",
            description="Update FASTSLIDE_VERSION in package/BUILD.bazel",
            pattern=r'^(FASTSLIDE_VERSION\s*=\s*)"[^"]*"\s*$',
            replacement=f'\\g<1>"{new_version}"',
            flags=re.MULTILINE,
        )
    )
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "python" / "BUILD.bazel",
            description="Update FASTSLIDE_VERSION in python/BUILD.bazel",
            pattern=r'^(FASTSLIDE_VERSION\s*=\s*)"[^"]*"\s*$',
            replacement=f'\\g<1>"{new_version}"',
            flags=re.MULTILINE,
        )
    )
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "MODULE.bazel",
            description="Update version in MODULE.bazel",
            pattern=r'(^module\(\s*\n\s*name\s*=\s*"fastslide",\s*\n\s*version\s*=\s*")[^"]*(")',
            replacement=f"\\g<1>{new_version}\\g<2>",
            flags=re.MULTILINE,
        )
    )

    # Python package version markers.
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "pyproject.toml",
            description="Update version in pyproject.toml",
            pattern=r'^(version\s*=\s*)"[^"]*"\s*$',
            replacement=f'\\g<1>"{new_version}"',
            flags=re.MULTILINE,
        )
    )
    # plans.append(
    #     _plan_regex_sub(
    #         path=WORKSPACE_ROOT / "python" / "pyproject.toml",
    #         description="Update version in python/pyproject.toml",
    #         pattern=r'^(version\s*=\s*)"[^"]*"\s*$',
    #         replacement=f'\\g<1>"{new_version}"',
    #         flags=re.MULTILINE,
    #     )
    # )
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "python" / "fastslide" / "__init__.py",
            description="Update __version__ in python/fastslide/__init__.py",
            pattern=r'^(__version__\s*=\s*)"[^"]*"\s*$',
            replacement=f'\\g<1>"{new_version}"',
            flags=re.MULTILINE,
        )
    )

    # Docs.
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "docs" / "source" / "conf.py",
            description="Update release in docs/source/conf.py",
            pattern=r'^(release\s*=\s*)"[^"]*"\s*$',
            replacement=f'\\g<1>"{new_version}"',
            flags=re.MULTILINE,
        )
    )
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "docs" / "Doxyfile",
            description="Update PROJECT_NUMBER in docs/Doxyfile",
            pattern=r"^(PROJECT_NUMBER\s*=\s*)[0-9A-Za-z.+-]+\s*$",
            replacement=f"\\g<1>{new_version}",
            flags=re.MULTILINE,
        )
    )

    # C/C++ version markers.
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "src" / "c" / "registry.cpp",
            description="Update fastslide_get_version() in src/c/registry.cpp",
            pattern=r'^(const char\*\s+fastslide_get_version\s*\(\s*void\s*\)\s*\{\s*\n\s*return\s+)"[^"]*"(;\s*\n\})',
            replacement=f'\\g<1>"{new_version}"\\g<2>',
            flags=re.MULTILINE,
        )
    )
    plans.append(
        _plan_regex_sub(
            path=WORKSPACE_ROOT / "src" / "python" / "fastslide.cpp",
            description='Update m.attr("__version__") in src/python/fastslide.cpp',
            pattern=r'^(\s*m\.attr\("__version__"\)\s*=\s*)"[^"]*"(;\s*)$',
            replacement=f'\\g<1>"{new_version}"\\g<2>',
            flags=re.MULTILINE,
        )
    )

    # Sanity: ensure all plans actually change something.
    for p in plans:
        if p.old == p.new:
            raise ValueError(f"{p.path}: planned edit made no changes ({p.description})")

    return plans


def _print_plan(plans: list[PlannedEdit]) -> None:
    print("\nPlanned version updates:\n")
    for p in plans:
        print(f"- {p.path.relative_to(WORKSPACE_ROOT)}: {p.description}")
    print()


def _prompt(prompt: str) -> str:
    return input(prompt).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--new-version",
        help="New version to set. If omitted, you'll be prompted.",
    )
    args = parser.parse_args()

    current_version = _read_current_fastslide_version()
    print(f"Current FastSlide version: {current_version}")

    new_version = args.new_version
    if not new_version:
        new_version = _prompt("Enter new version (e.g. 0.2.0): ")
    if not new_version:
        raise SystemExit("No version provided.")
    _validate_version(new_version)
    if new_version == current_version:
        raise SystemExit("New version matches current version; nothing to do.")

    plans = _collect_plans(current_version=current_version, new_version=new_version)
    _print_plan(plans)

    confirm = _prompt(f"Apply these {len(plans)} updates? [y/N]: ").lower()
    if confirm not in ("y", "yes"):
        print("Aborted; no files were changed.")
        return

    for p in plans:
        _write_text(p.path, p.new)

    print("\nDone. Updated files:")
    for p in plans:
        print(f"- {p.path.relative_to(WORKSPACE_ROOT)}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nAborted.")
        sys.exit(1)
