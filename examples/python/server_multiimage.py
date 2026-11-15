# Copyright 2026 Jonas Teuwen. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""FastAPI XYZ tile server for a *folder* of whole-slide images.

This is the multi-slide counterpart to ``server.py`` and the FastSlide
analogue of OpenSlide's ``deepzoom_multiserver.py``. It scans a directory for
slides whose format FastSlide supports, presents a file browser to pick one,
and serves the same OpenLayers XYZ viewer (``index.html``) per slide.

Usage:
    python server_multiimage.py /path/to/slides
    python server_multiimage.py /path/to/slides --host 0.0.0.0 --port 8000

Then open http://127.0.0.1:8000 to browse the folder.

Endpoints:
    GET /                                  -> the file browser (browser.html)
    GET /api/slides                        -> JSON list of discovered slides
    GET /viewer?slide=<relpath>            -> the OpenLayers viewer (index.html)
    GET /info?slide=<relpath>              -> slide + per-image metadata as JSON
    GET /tiles/{image}/{z}/{x}/{y}.{ext}?slide=<relpath>  -> a single tile
"""

from __future__ import annotations

import argparse
import functools
import os
from dataclasses import dataclass
from pathlib import Path

import uvicorn
from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse, JSONResponse, Response

import fastslide
from fastslide.xyz_pyramid import XYZPyramid


@dataclass(frozen=True)
class _OpenSlide:
    """A cached, opened slide: its reader and one XYZPyramid per image.

    Holding an instance keeps the underlying ``FastSlide`` reader alive, so the
    weak reader handle inside each pyramid's ``SlideImageView`` stays valid for
    as long as any caller references this object. Eviction from the cache is
    therefore safe even while a tile request is still in flight: the entry is
    only torn down once the last in-flight request drops its reference.
    """

    reader: fastslide.FastSlide
    pyramids: tuple[XYZPyramid, ...]


_HERE = Path(__file__).resolve().parent
_INDEX_HTML = _HERE / "index.html"
_BROWSER_HTML = _HERE / "browser.html"

_MEDIA_TYPES = {
    "jpg": "image/jpeg",
    "jpeg": "image/jpeg",
    "png": "image/png",
}


def _supported_extensions() -> tuple[str, ...]:
    """Returns supported extensions, longest first (so ``.ome.tif`` wins)."""
    exts = {e.lower() for e in fastslide.get_supported_extensions()}
    return tuple(sorted(exts, key=len, reverse=True))


def _matches_extension(name: str, extensions: tuple[str, ...]) -> bool:
    return name.lower().endswith(extensions)


def _entry(path: Path, root: Path) -> dict[str, object]:
    # Directory-based formats (e.g. OME-Zarr) report no single file size.
    size = path.stat().st_size if path.is_file() else None
    return {
        "path": path.relative_to(root).as_posix(),
        "name": path.name,
        "size_bytes": size,
        "is_dir": path.is_dir(),
    }


def _list_slides(root: Path, extensions: tuple[str, ...]) -> list[dict[str, object]]:
    """Returns supported slides under ``root`` as relative-path entries.

    Matching is purely by extension against ``get_supported_extensions()``;
    this is cheap and faithful to the advertised formats. Directory-based
    formats (``.zarr``/``.ome.zarr``) are matched as a unit and not descended
    into, which also avoids walking their many chunk files.
    """
    slides: list[dict[str, object]] = []
    for dirpath, dirnames, filenames in os.walk(root):
        here = Path(dirpath)
        keep: list[str] = []
        for name in sorted(dirnames):
            if _matches_extension(name, extensions):
                slides.append(_entry(here / name, root))
            else:
                keep.append(name)
        dirnames[:] = keep  # prune matched directories from the walk
        for name in sorted(filenames):
            if _matches_extension(name, extensions):
                slides.append(_entry(here / name, root))
    slides.sort(key=lambda s: str(s["path"]))
    return slides


def create_app(root: str | Path, tile_size: int = 256, jpeg_quality: int = 85) -> FastAPI:
    """Builds the FastAPI app serving XYZ tiles for every slide under ``root``.

    Args:
        root: Directory to scan for supported slides (searched recursively).
        tile_size: Tile edge length in pixels.
        jpeg_quality: Quality used when encoding JPEG tiles.

    Returns:
        A configured :class:`fastapi.FastAPI` instance.
    """
    root_dir = Path(root).resolve()
    if not root_dir.is_dir():
        raise NotADirectoryError(f"not a directory: {root_dir}")

    extensions = _supported_extensions()

    def resolve_slide(rel: str) -> Path:
        """Resolves a slide relative path, guarding against traversal."""
        candidate = (root_dir / rel).resolve()
        try:
            candidate.relative_to(root_dir)
        except ValueError as exc:
            raise HTTPException(status_code=404, detail="slide not found") from exc
        if not candidate.exists() or not _matches_extension(candidate.name, extensions):
            raise HTTPException(status_code=404, detail="slide not found")
        return candidate

    # Cache the opened slide per relative path so repeated tile requests reuse
    # the same reader. ``functools.lru_cache`` is thread-safe, which matters
    # because FastAPI runs these sync endpoints in a threadpool. Each endpoint
    # holds onto the returned _OpenSlide for the whole request, so an entry
    # evicted by a concurrent open of a different slide stays alive until the
    # in-flight request that still references it returns. ``maxsize`` only
    # bounds how many readers are kept warm, not correctness.
    @functools.lru_cache(maxsize=16)
    def open_slide(rel: str) -> _OpenSlide:
        path = resolve_slide(rel)
        reader = fastslide.FastSlide.from_file_path(str(path))
        images = reader.images
        pyramids = tuple(XYZPyramid(images[i], tile_size=tile_size) for i in range(len(images)))
        return _OpenSlide(reader=reader, pyramids=pyramids)

    app = FastAPI(title="FastSlide XYZ Multi-Slide Viewer")

    @app.get("/")
    def index() -> FileResponse:
        return FileResponse(_BROWSER_HTML, media_type="text/html")

    @app.get("/viewer")
    def viewer() -> FileResponse:
        return FileResponse(_INDEX_HTML, media_type="text/html")

    @app.get("/api/slides")
    def api_slides() -> JSONResponse:
        return JSONResponse(
            {
                "root": str(root_dir),
                "extensions": sorted(extensions),
                "slides": _list_slides(root_dir, extensions),
            }
        )

    @app.get("/info")
    def info(slide: str = Query(...)) -> JSONResponse:
        opened = open_slide(slide)
        image_infos = []
        for i, pyramid in enumerate(opened.pyramids):
            entry = pyramid.info()
            entry["index"] = i
            entry["name"] = pyramid.slide.name
            image_infos.append(entry)
        return JSONResponse(
            {
                "name": Path(slide).name,
                "format": opened.reader.format,
                "primary_index": opened.reader.images.primary_index,
                "images": image_infos,
            }
        )

    @app.get("/tiles/{image}/{z}/{x}/{y}.{ext}")
    def tile(image: int, z: int, x: int, y: int, ext: str, slide: str = Query(...)) -> Response:
        media_type = _MEDIA_TYPES.get(ext.lower())
        if media_type is None:
            raise HTTPException(status_code=404, detail=f"unsupported extension: {ext}")
        opened = open_slide(slide)
        if not 0 <= image < len(opened.pyramids):
            raise HTTPException(status_code=404, detail=f"no image {image}")
        try:
            data = opened.pyramids[image].get_tile_bytes(z, x, y, fmt=ext, quality=jpeg_quality)
        except ValueError as exc:
            raise HTTPException(status_code=404, detail=str(exc)) from exc
        return Response(content=data, media_type=media_type)

    return app


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve a folder of slides as XYZ pyramids.")
    parser.add_argument("root", help="Directory to scan for supported slides.")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host (default: 127.0.0.1).")
    parser.add_argument("--port", type=int, default=8000, help="Bind port (default: 8000).")
    parser.add_argument("--tile-size", type=int, default=256, help="Tile size in px (default: 256).")
    parser.add_argument("--jpeg-quality", type=int, default=85, help="JPEG quality 1-100 (default: 85).")
    return parser.parse_args()


def main() -> None:
    """CLI entry point."""
    args = _parse_args()
    app = create_app(args.root, tile_size=args.tile_size, jpeg_quality=args.jpeg_quality)
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
