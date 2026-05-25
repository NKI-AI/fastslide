#!/usr/bin/env python3
"""Tiny HTTP server for the FastSlide WASM viewer.

The WASM build is compiled with `-pthread`, so the runtime needs
`SharedArrayBuffer`, which browsers only expose when the page is served
cross-origin-isolated. This means every response must carry:

    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Embedder-Policy: credentialless

We use `credentialless` instead of the stricter `require-corp` so the
example can still pull Tailwind/OpenLayers from public CDNs (which don't
send `Cross-Origin-Resource-Policy: cross-origin`). Credentialless still
gives the page a cross-origin-isolated context — the only difference is
that cross-origin sub-resources are loaded without cookies. Supported in
Chrome/Edge 96+ and Firefox 119+ (and not Safari, which also doesn't
support our nested-worker pthread setup, so we're not regressing anything).

`python -m http.server` does not send these headers, so this script wraps
the stdlib server and injects them. Run it from the staged Bazel output:

    bazelisk build @fastslide//wasm/example:site
    python3 bazel-bin/external/fastslide+/wasm/example/site/serve.py

By default it serves the directory the script lives in on
http://localhost:8000.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import pathlib
import sys


class CrossOriginIsolatedHandler(http.server.SimpleHTTPRequestHandler):
    """`SimpleHTTPRequestHandler` that sets COOP/COEP on every response."""

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        # `credentialless` keeps the page cross-origin-isolated (so
        # SharedArrayBuffer + pthreads work) while still allowing the
        # example to load Tailwind/OpenLayers from public CDNs.
        self.send_header("Cross-Origin-Embedder-Policy", "credentialless")
        # Avoid stale wasm/js during development.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="Bind address.")
    parser.add_argument("--port", type=int, default=8000, help="Bind port.")
    parser.add_argument(
        "--directory",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent,
        help="Directory to serve (defaults to the directory of this script).",
    )
    args = parser.parse_args(argv)

    directory = args.directory.resolve()
    if not directory.is_dir():
        print(f"error: not a directory: {directory}", file=sys.stderr)
        return 1

    handler_factory = functools.partial(CrossOriginIsolatedHandler, directory=str(directory))

    with http.server.ThreadingHTTPServer((args.host, args.port), handler_factory) as httpd:
        print(
            f"FastSlide WASM viewer ready at http://{args.host}:{args.port}/ (serving {directory})",
            flush=True,
        )
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nshutting down", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
