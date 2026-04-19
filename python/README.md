# czi-reader

Small Python 3.11+ reader for Zeiss CZI:

- Parses the file header and subblock directory
- Builds a tile index grouped by integer downsample (“levels”)
- Decodes tiles for COMP_NONE, zstd0, zstd1, and JPEG XR (optional via `imagecodecs`)

## Install (editable)

```bash
source ~/.venv/bin/activate
pip install -e libczi/python
pip install 'czi-reader[jxr]'
```

## Minimal usage

```python
from pathlib import Path

from czi_reader import CziReader

r = CziReader(Path("~/data/SR1482_40X_HE_T052_02.czi").expanduser())
hdr = r.read_header()
print(hdr)
print(sorted(r.levels().keys()))

tile = next(r.iter_tiles(downsample=1))
rgba = r.read_tile(tile, output="rgba")
print(tile, len(rgba))
```
