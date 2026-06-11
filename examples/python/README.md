# FastSlide XYZ pyramid viewer

A minimal example that serves a whole-slide image as a standard
XYZ (slippy-map) tile pyramid over HTTP and views it in the browser with
[OpenLayers](https://openlayers.org/).

It is the FastSlide counterpart to the OpenSlide `deepzoom_server.py` example,
with two differences:

- it serves an **XYZ pyramid** (`/tiles/{z}/{x}/{y}.jpg`) instead of Deep Zoom
  / DZI, so any XYZ-capable web map library can consume it directly, and
- it uses **FastAPI + uvicorn** instead of Flask.

The tiling itself is produced by `fastslide.XYZPyramid`, which ships with the
`fastslide` package, so you can reuse it outside this example.

Two servers are provided:

- `server.py` -- serves a **single slide** passed on the command line.
- `server_multiimage.py` -- scans a **folder** for supported slides and shows a
  file browser to pick one; clicking a slide opens the same viewer.

## Run with Bazel (from the fastslide module)

The examples are wired into the build, so from the `aifo/fastslide` module root
you can run them directly without installing anything.

Single slide:

```bash
bazelisk run //examples/python:viewer -- /abs/path/to/slide.svs
```

A whole folder (with a file browser at `/`):

```bash
bazelisk run //examples/python:viewer_multiimage -- /abs/path/to/slides
```

Extra flags are forwarded after the path, for example:

```bash
bazelisk run //examples/python:viewer -- /abs/path/to/slide.svs --port 8080 --tile-size 256
```

Then open <http://127.0.0.1:8000> (or the port you chose).

## Run with a plain Python environment

```bash
pip install -r requirements.txt
```

`fastslide` is listed in `requirements.txt`. If you are working from a source
checkout, install your locally built wheel instead, for example:

```bash
pip install fastapi "uvicorn[standard]" pillow numpy fastslide
```

Then run a server:

```bash
# single slide
python server.py /path/to/slide.svs

# a folder of slides (file browser at http://127.0.0.1:8000)
python server_multiimage.py /path/to/slides
```

Then open <http://127.0.0.1:8000>.

Options (both servers accept `--host`, `--port`, `--tile-size`, `--jpeg-quality`):

```bash
python server.py /path/to/slide.svs --host 0.0.0.0 --port 8000 --tile-size 256 --jpeg-quality 85
```

## Endpoints

`server.py` (single slide):

| Route                                  | Description                                                                                                                                            |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `GET /`                                | The OpenLayers viewer (`index.html`).                                                                                                                  |
| `GET /info`                            | Slide metadata as JSON: file name/format, `primary_index`, and per-image entries (dimensions, zoom range, tile size, native levels, MPP, resolutions). |
| `GET /tiles/{image}/{z}/{x}/{y}.{ext}` | A single tile from image `image`; `ext` is `jpg`, `jpeg`, or `png`.                                                                                    |

`server_multiimage.py` (folder): the slide is selected with a `?slide=<relpath>`
query parameter, where `<relpath>` is the path relative to the served folder.

| Route                                                  | Description                                              |
| ------------------------------------------------------ | -------------------------------------------------------- |
| `GET /`                                                | The file browser (`browser.html`).                       |
| `GET /api/slides`                                      | JSON: served root, supported extensions, and slide list. |
| `GET /viewer?slide=<relpath>`                          | The OpenLayers viewer for one slide.                     |
| `GET /info?slide=<relpath>`                            | Same payload as the single-slide `/info`.                |
| `GET /tiles/{image}/{z}/{x}/{y}.{ext}?slide=<relpath>` | A single tile from a slide in the folder.                |

Slides with multiple navigable images (e.g. an Olympus VSI navigator plus
region images) expose one entry per image under `/info`, and the viewer shows
an image switcher in the sidebar.

## Tile convention

XYZ layout mapped onto the slide's native pyramid levels:

- Each zoom level `z` is one native pyramid level: `z = 0` is the coarsest
  native level and `z = max_zoom` is full resolution. The viewer's `TileGrid`
  uses the slide's `level_downsamples` as resolutions, so tiles are direct
  native reads (no server-side resampling) and OpenLayers scales between levels.
- `x` is the column (increasing rightwards), `y` is the row (increasing
  downwards), with the tile origin at the top-left of the slide.
