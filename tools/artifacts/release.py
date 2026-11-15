"""Collect built FastSlide Java JARs and release them.

Two destinations are supported:

- ``local``: stage the JARs into ``<out_dir>/<version>/`` in the exact layout a
  Gradle ``ivy``/url repository expects, so the whole publish -> consume loop can
  be validated offline (point the consumer at ``file://<out_dir>``).
- ``github``: create/refresh a GitHub Release (tag == version) and attach the
  JARs via the ``gh`` CLI.

The set of JARs is the platform-independent wrapper plus one native classifier
JAR per platform; coordinates are ``dev.aifo:fastslide-java:<version>`` and
``dev.aifo:fastslide-native:<version>:<os>-<arch>``.
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from . import common
from .specs import PLATFORMS

ARTIFACT_DIR = common.WORKSPACE_ROOT / "artifacts" / "jars"
CHECKSUMS_NAME = "SHA256SUMS"

# Classifiers excluded from the Java release. Empty: windows-aarch64 is now
# built natively on a Windows arm64 runner via Meson + MSVC (see
# tools/artifacts/jars_meson.py)
_UNSUPPORTED_CLASSIFIERS: frozenset[str] = frozenset()

# Distinct ``<os>-<arch>`` classifiers, deduplicated while preserving order.
NATIVE_CLASSIFIERS: list[str] = [
    classifier
    for classifier in dict.fromkeys(f"{spec.os_name}-{spec.arch}" for spec in PLATFORMS.values())
    if classifier not in _UNSUPPORTED_CLASSIFIERS
]


@dataclass(frozen=True)
class CollectedJars:
    wrapper: Path | None
    natives: list[Path]
    missing: list[str]

    @property
    def present(self) -> list[Path]:
        jars: list[Path] = []
        if self.wrapper is not None:
            jars.append(self.wrapper)
        jars.extend(self.natives)
        return jars


def wrapper_jar_name(version: str) -> str:
    return f"fastslide-java-{version}.jar"


def native_jar_name(version: str, classifier: str) -> str:
    return f"fastslide-native-{version}-{classifier}.jar"


def collect_jars(jar_dir: Path, version: str) -> CollectedJars:
    """Find the wrapper + per-platform native JARs in ``jar_dir``."""
    wrapper_path = jar_dir / wrapper_jar_name(version)
    wrapper = wrapper_path if wrapper_path.is_file() else None

    natives: list[Path] = []
    missing: list[str] = []
    if wrapper is None:
        missing.append(wrapper_path.name)

    for classifier in NATIVE_CLASSIFIERS:
        candidate = jar_dir / native_jar_name(version, classifier)
        if candidate.is_file():
            natives.append(candidate)
        else:
            missing.append(candidate.name)

    return CollectedJars(wrapper=wrapper, natives=natives, missing=missing)


def require_complete(collected: CollectedJars) -> None:
    if collected.missing:
        joined = "\n  ".join(collected.missing)
        raise FileNotFoundError("Refusing to publish an incomplete artifact set. Missing:\n  " + joined)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_checksums(jars: list[Path], out_path: Path) -> Path:
    """Write a ``sha256sum``-compatible file listing the given JARs."""
    lines = [f"{_sha256(jar)}  {jar.name}\n" for jar in sorted(jars, key=lambda p: p.name)]
    out_path.write_text("".join(lines), encoding="utf-8")
    return out_path


def dedup_by_content(paths: list[Path], *, prefer_substring: str) -> list[Path]:
    """Collapse byte-identical files down to one canonical path per digest.

    Bazel's ``pkg_deb`` emits both the canonical versioned file
    (e.g. ``libfastslide_0.8.0_amd64.deb``) and a symlink named after the Bazel
    target (e.g. ``libfastslide_amd64.deb``) pointing at the same bytes. If both
    reach the release directory they are byte-identical duplicates; attach only
    one, preferring the path whose name contains ``prefer_substring`` (the
    version) so releases carry the canonical versioned filename.

    Args:
        paths: Candidate files to deduplicate.
        prefer_substring: When two paths share content, keep the one whose name
            contains this substring (falling back to the lexicographically
            smaller name for a stable, deterministic choice).

    Returns:
        The kept paths, sorted by filename.
    """
    chosen: dict[str, Path] = {}
    for path in sorted(paths, key=lambda p: p.name):
        digest = _sha256(path)
        current = chosen.get(digest)
        if current is None:
            chosen[digest] = path
            continue
        if prefer_substring in path.name and prefer_substring not in current.name:
            chosen[digest] = path
    return sorted(chosen.values(), key=lambda p: p.name)


def stage_local(jars: list[Path], out_dir: Path, version: str) -> Path:
    """Copy JARs into ``<out_dir>/<version>/`` (Gradle ivy/url layout)."""
    dest = out_dir / version
    common.ensure_dir(dest)
    staged: list[Path] = []
    for jar in jars:
        target = dest / jar.name
        common.safe_unlink(target)
        shutil.copy2(jar, target)
        staged.append(target)
        print(f"\u2714 {target}")
    write_checksums(staged, dest / CHECKSUMS_NAME)
    print(f"\u2714 {dest / CHECKSUMS_NAME}")
    return dest


def _gh(args: list[str], *, repo: str | None) -> subprocess.CompletedProcess[str]:
    cmd = ["gh", *args]
    if repo:
        cmd += ["--repo", repo]
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def _release_exists(tag: str, *, repo: str | None) -> bool:
    return _gh(["release", "view", tag], repo=repo).returncode == 0


def publish_github(
    jars: list[Path],
    *,
    tag: str,
    title: str,
    notes: str,
    prerelease: bool,
    repo: str | None,
    checksums: Path,
    extra_assets: list[Path] | None = None,
    generate_notes: bool = False,
) -> None:
    """Create the release if needed, then (re)upload all assets via ``gh``.

    Args:
        jars: Java JARs to attach.
        extra_assets: Additional files to attach (e.g. Python wheels) so a
            single GitHub Release hubs every artifact for the version.
        generate_notes: Let GitHub auto-generate the "What's Changed" notes from
            merged PRs since the previous tag instead of using ``notes``.
    """
    extra_assets = extra_assets or []
    assets = [str(jar) for jar in jars] + [str(checksums)] + [str(a) for a in extra_assets]

    if _release_exists(tag, repo=repo):
        print(f"\u25b6\ufe0e Release {tag} exists; uploading assets (--clobber)")
        result = _gh(["release", "upload", tag, "--clobber", *assets], repo=repo)
    else:
        print(f"\u25b6\ufe0e Creating release {tag}")
        create_args = [
            "release",
            "create",
            tag,
            "--title",
            title,
        ]
        if generate_notes:
            create_args.append("--generate-notes")
        else:
            create_args.extend(["--notes", notes])
        create_args.append("--prerelease" if prerelease else "--latest")
        result = _gh([*create_args, *assets], repo=repo)

    if result.stdout:
        print(result.stdout, end="")
    if result.returncode != 0:
        if result.stderr:
            print(result.stderr, end="")
        raise RuntimeError(f"gh release publishing failed for tag {tag} (exit {result.returncode}).")
    print(f"\u2714 Published {len(jars)} JAR(s) + {checksums.name} to release {tag}.")
