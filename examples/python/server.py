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
"""FastAPI XYZ tile server for a single FastSlide whole-slide image.

This is the FastSlide analogue of the OpenSlide ``deepzoom_server.py`` example,
but it serves a standard XYZ (slippy-map) tile pyramid that an OpenLayers
viewer can consume directly, and uses FastAPI/uvicorn instead of Flask.

Usage:
    python server.py /path/to/slide.svs
    python server.py /path/to/slide.svs --host 0.0.0.0 --port 8000 --tile-size 256

Then open http://127.0.0.1:8000 in a browser.

Endpoints:
    GET /                                  -> the OpenLayers viewer (index.html)
    GET /info                              -> slide + per-image metadata as JSON
    GET /tiles/{image}/{z}/{x}/{y}.{ext}   -> a single tile (ext: jpg | jpeg | png)
"""

from __future__ import annotations

import argparse
from pathlib import Path

import uvicorn
from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse, JSONResponse, Response

import fastslide
from fastslide.xyz_pyramid import XYZPyramid

_HERE = Path(__file__).resolve().parent
_INDEX_HTML = _HERE / "index.html"

_MEDIA_TYPES = {
    "jpg": "image/jpeg",
    "jpeg": "image/jpeg",
    "png": "image/png",
}


def create_app(slide_path: str | Path, tile_size: int = 256, jpeg_quality: int = 85) -> FastAPI:
    """Builds the FastAPI app serving XYZ tiles for a single slide.

    Args:
        slide_path: Path to the whole-slide image to serve.
        tile_size: Tile edge length in pixels.
        jpeg_quality: Quality used when encoding JPEG tiles.

    Returns:
        A configured :class:`fastapi.FastAPI` instance.
    """
    slide = fastslide.FastSlide.from_file_path(str(slide_path))

    # One XYZ pyramid per navigable image in the file. Most slides expose a
    # single image; some (e.g. Olympus VSI) expose a navigator plus one or more
    # region images.
    images = slide.images
    pyramids = [XYZPyramid(images[i], tile_size=tile_size) for i in range(len(images))]

    app = FastAPI(title="FastSlide XYZ Viewer")

    @app.get("/")
    def index() -> FileResponse:
        return FileResponse(_INDEX_HTML, media_type="text/html")

    @app.get("/info")
    def info() -> JSONResponse:
        image_infos = []
        for i, pyramid in enumerate(pyramids):
            entry = pyramid.info()
            entry["index"] = i
            entry["name"] = images[i].name
            image_infos.append(entry)
        return JSONResponse(
            {
                "name": Path(slide_path).name,
                "format": slide.format,
                "primary_index": images.primary_index,
                "images": image_infos,
            }
        )

    @app.get("/tiles/{image}/{z}/{x}/{y}.{ext}")
    def tile(image: int, z: int, x: int, y: int, ext: str) -> Response:
        media_type = _MEDIA_TYPES.get(ext.lower())
        if media_type is None:
            raise HTTPException(status_code=404, detail=f"unsupported extension: {ext}")
        if not 0 <= image < len(pyramids):
            raise HTTPException(status_code=404, detail=f"no image {image}")
        try:
            data = pyramids[image].get_tile_bytes(z, x, y, fmt=ext, quality=jpeg_quality)
        except ValueError as exc:
            raise HTTPException(status_code=404, detail=str(exc)) from exc
        return Response(content=data, media_type=media_type)

    return app


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve a slide as an XYZ tile pyramid.")
    parser.add_argument("slide", help="Path to the whole-slide image file.")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host (default: 127.0.0.1).")
    parser.add_argument("--port", type=int, default=8000, help="Bind port (default: 8000).")
    parser.add_argument("--tile-size", type=int, default=256, help="Tile size in px (default: 256).")
    parser.add_argument("--jpeg-quality", type=int, default=85, help="JPEG quality 1-100 (default: 85).")
    return parser.parse_args()


def main() -> None:
    """CLI entry point."""
    args = _parse_args()
    app = create_app(args.slide, tile_size=args.tile_size, jpeg_quality=args.jpeg_quality)
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
