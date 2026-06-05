#!/usr/bin/env python3
"""On-target smoke test for an installed FastSlide Python wheel.

Run this with the interpreter of a venv that has the wheel installed (NOT from
the repo's ``python/`` source tree), e.g. in CI after
``uv pip install --find-links dist fastslide``. It imports ``fastslide`` (which
loads the native extension via ``fastslide._fastslide``), opens the bundled
sample slide, and reads its dimensions -- proving the wheel's native library
loads and decodes on the real OS/arch/Python of the runner.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

DEFAULT_SAMPLE = Path(__file__).resolve().parents[1] / "tests" / "test_data" / "CMU-1-Small-Region.svs"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=DEFAULT_SAMPLE,
        help=f"Sample slide to open (default: {DEFAULT_SAMPLE}).",
    )
    args = parser.parse_args()

    if not args.input.is_file():
        parser.error(f"Sample slide not found: {args.input}")

    # Imported from the installed wheel in the CI venv, not the repo source tree.
    import fastslide  # type: ignore  # pylint: disable=import-error,import-outside-toplevel

    version = getattr(fastslide, "__version__", "<unknown>")
    print(f"\u25b6\ufe0e Imported fastslide {version}")
    print(f"  interpreter: {sys.executable}")
    print(f"  python:      {sys.version.split()[0]} ({sys.implementation.name})")
    print(f"  sample:      {args.input}")

    slide = fastslide.FastSlide.from_file_path(str(args.input))  # type: ignore[attr-defined]
    try:
        dimensions = slide.dimensions
    finally:
        close = getattr(slide, "close", None)
        if callable(close):
            close()

    print(f"\u2714 Opened slide; level-0 dimensions = {dimensions}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
