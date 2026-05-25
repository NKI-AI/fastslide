"""Custom pytest entrypoint used by `tests/python/*_test` targets.

Replaces `aspect_rules_py`'s `pytest_main = True` machinery, which silently
drops external-repo test sources in its `_pytest_paths_impl` (anything with
a `short_path` starting with `../` is excluded). That filter breaks
`bazelisk test @fastslide//tests/python:*` when fastslide is consumed as an
external bzlmod dep -- the generated `.pytest_paths` file ends up empty,
pytest falls back to autodiscovery from the consumer's `_main` runfiles
root, and collects zero items.

This runner is self-contained: it locates tests by resolving its own
`__file__` and asks pytest to collect from that directory. The same code
path works whether fastslide is the root module (standalone build) or
loaded via `local_path_override` in a consuming repo.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest


def _build_args() -> list[str]:
    """Assemble pytest's argv, honouring Bazel's test protocol env vars."""
    test_dir = Path(__file__).resolve().parent
    args: list[str] = [
        "--verbose",
        # Skip the cacheprovider plugin: it tries to write under CWD which is
        # a read-only Bazel sandbox.
        "-p",
        "no:cacheprovider",
        str(test_dir),
    ]

    junit_xml = os.environ.get("XML_OUTPUT_FILE")
    if junit_xml:
        args.append(f"--junitxml={junit_xml}")
        suite_name = os.environ.get("BAZEL_TARGET")
        if suite_name:
            args.extend(["-o", f"junit_suite_name={suite_name}"])

    test_filter = os.environ.get("TESTBRIDGE_TEST_ONLY")
    if test_filter is not None:
        args.append(f"-k={test_filter}")

    # Forward any user-supplied positional args from the py_test `args` attr.
    args.extend(sys.argv[1:])
    return args


if __name__ == "__main__":
    exit_code = pytest.main(_build_args())
    if exit_code != 0:
        print(f"Pytest exit code: {exit_code}", file=sys.stderr)
    sys.exit(exit_code)
