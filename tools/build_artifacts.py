#!/usr/bin/env python3
"""Build FastSlide distributable artifacts (wheels + packages) via Zig toolchains.

This script is a thin wrapper around `aifo/fastslide/tools/artifacts/`.

Both are built using the hermetic cross-compilation toolchain configuration
(`--config=hermetic`), enabling cross-platform builds from a single host.

Outputs are collected under:
  - `aifo/fastslide/artifacts/wheels/`
  - `aifo/fastslide/artifacts/packages/`
"""

from __future__ import annotations

from artifacts.cli import main


if __name__ == "__main__":
    main()


