"""Tests for the artifact release helpers."""

from __future__ import annotations

from pathlib import Path

import pytest

from artifacts import release


def _write(directory: Path, name: str, content: bytes) -> Path:
    path = directory / name
    path.write_bytes(content)
    return path


def test_dedup_by_content_keeps_versioned_over_symlink(tmp_path: Path) -> None:
    """Byte-identical debs collapse to the versioned name (the pkg_deb symlink case)."""
    _write(tmp_path, "libfastslide_0.8.0_amd64.deb", b"amd64")
    _write(tmp_path, "libfastslide_amd64.deb", b"amd64")
    _write(tmp_path, "libfastslide_0.8.0_arm64.deb", b"arm64")
    _write(tmp_path, "libfastslide_arm64.deb", b"arm64")
    _write(tmp_path, "libfastslide-dev_0.8.0_all.deb", b"dev")
    _write(tmp_path, "libfastslide_dev.deb", b"dev")

    kept = release.dedup_by_content(sorted(tmp_path.glob("*.deb")), prefer_substring="0.8.0")

    assert [p.name for p in kept] == [
        "libfastslide-dev_0.8.0_all.deb",
        "libfastslide_0.8.0_amd64.deb",
        "libfastslide_0.8.0_arm64.deb",
    ]


def test_dedup_by_content_keeps_distinct_content(tmp_path: Path) -> None:
    """Different architectures have different bytes and must all survive."""
    amd = _write(tmp_path, "libfastslide_0.8.0_amd64.deb", b"amd64")
    arm = _write(tmp_path, "libfastslide_0.8.0_arm64.deb", b"arm64")

    kept = release.dedup_by_content([amd, arm], prefer_substring="0.8.0")

    assert {p.name for p in kept} == {amd.name, arm.name}


def test_dedup_by_content_stable_when_no_preference_matches(tmp_path: Path) -> None:
    """With no versioned candidate, fall back to the lexicographically smaller name."""
    first = _write(tmp_path, "a.deb", b"same")
    _write(tmp_path, "b.deb", b"same")

    kept = release.dedup_by_content(sorted(tmp_path.glob("*.deb")), prefer_substring="9.9.9")

    assert [p.name for p in kept] == [first.name]


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
