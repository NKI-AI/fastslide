"""Platform/Python matrix for building FastSlide artifacts."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class PlatformSpec:
    name: str
    bazel_platform: str
    use_hermetic: bool
    os_name: str
    arch: str


PLATFORMS: dict[str, PlatformSpec] = {
    "linux_x86_64": PlatformSpec(
        name="linux_x86_64",
        bazel_platform="//platforms:linux_x86_64",
        use_hermetic=True,
        os_name="linux",
        arch="x86_64",
    ),
    "linux_arm64": PlatformSpec(
        name="linux_arm64",
        bazel_platform="//platforms:linux_arm64",
        use_hermetic=True,
        os_name="linux",
        arch="aarch64",
    ),
    "darwin_x86_64": PlatformSpec(
        name="darwin_x86_64",
        bazel_platform="//platforms:darwin_x86_64",
        use_hermetic=True,
        os_name="darwin",
        arch="x86_64",
    ),
    "darwin_aarch64": PlatformSpec(
        name="darwin_aarch64",
        bazel_platform="//platforms:darwin_aarch64",
        use_hermetic=True,
        os_name="darwin",
        arch="aarch64",
    ),
    "windows_x86_64": PlatformSpec(
        name="windows_x86_64",
        bazel_platform="//platforms:windows_x86_64",
        use_hermetic=True,
        os_name="windows",
        arch="x86_64",
    ),
    "windows_arm64": PlatformSpec(
        name="windows_arm64",
        bazel_platform="//platforms:windows_arm64",
        use_hermetic=True,
        os_name="windows",
        arch="aarch64",
    ),
}


@dataclass(frozen=True)
class WheelSpec:
    """One wheel flavour: a Bazel target and the PEP 425 tag it stamps."""

    target: str
    tag: str


# Two wheels are built per platform: a stable-ABI (abi3) one floored at CPython
# 3.12, installable on every CPython >= 3.12, and a version-specific one for
# 3.11, which predates the stable-ABI floor.
WHEELS: tuple[WheelSpec, ...] = (
    WheelSpec(target="//python:fastslide_wheel", tag="cp312-abi3"),
    WheelSpec(target="//python:fastslide_wheel_cp311", tag="cp311"),
)
