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


# The Python bindings ship a single stable-ABI (abi3) wheel floored at CPython
# 3.12 (tag cp312-abi3), which runs on every CPython >= 3.12.
ABI3_PY_TAG: str = "cp312"
