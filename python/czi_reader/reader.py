"""CZI reader implementation.

This is a minimal reader focused on:
- Parsing the file header and SubBlockDirectory (Schema DV)
- Indexing tiles by integer downsample ("levels")
- Reading/decoding individual tile pixels

The parsing/behavior is intentionally modeled after OpenSlide's CZI reader
implementation in `openslide/src/openslide-vendor-zeiss.c`.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import replace
from pathlib import Path
import struct
from typing import Literal
import xml.etree.ElementTree as ET
import functools
import numpy as np

from .decoders import decode_tile_pixels
from .structs import Bounds, CziHeader, LevelInfo, Subblock, Tile


_SID_ZISRAWFILE = b"ZISRAWFILE"
_SID_ZISRAWDIRECTORY = b"ZISRAWDIRECTORY"
_SID_ZISRAWSUBBLOCK = b"ZISRAWSUBBLOCK"
_SID_ZISRAWMETADATA = b"ZISRAWMETADATA"
_SID_ZISRAWATTDIR = b"ZISRAWATTDIR"
_SID_ZISRAWATTACH = b"ZISRAWATTACH"

_SCHEMA_DV = b"DV"
_SCHEMA_A1 = b"A1"

_CZI_SUBBLK_HDR_LEN = 288
_CZI_ATTACHMENT_HDR_LEN = 288

_SEG_HDR_STRUCT = struct.Struct("<16sqq")
_FILE_HDR_STRUCT = struct.Struct("<16sqqiiii16s16siqqiq")
_SUBBLK_DIR_HDR_STRUCT = struct.Struct("<16sqqi124s")
_SUBBLK_HDR_STRUCT = struct.Struct("<16sqqiiq")
_META_HDR_STRUCT = struct.Struct("<16sqqii248s")
_DIR_ENTRY_DV_STRUCT = struct.Struct("<2siqiibc4si")
_DIM_ENTRY_DV_STRUCT = struct.Struct("<4siifi")
_ATT_DIR_HDR_STRUCT = struct.Struct("<16sqqi252s")
_ATT_ENTRY_A1_STRUCT = struct.Struct("<2s10sqi16s8s80s")
_ATT_SEG_HDR_PREFIX_STRUCT = struct.Struct("<16sqqi")  # seg_hdr + data_size


def _check_magic(found: bytes, expected: bytes) -> None:
    if not found.startswith(expected):
        raise ValueError(f'Bad magic: expected "{expected.decode(errors="ignore")}", got {found!r}')


def _div_round_closest(n: int, d: int) -> int:
    if d == 0:
        raise ValueError("Division by zero while computing downsample")
    # Matches OpenSlide macro:
    # ((((n) < 0) != ((d) < 0)) ? (((n) - (d) / 2) / (d)) : (((n) + (d) / 2) / (d)))
    if (n < 0) != (d < 0):
        return (n - d // 2) // d
    return (n + d // 2) // d


def _decode_ascii_name(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def _size_from_bounds(bounds: Bounds) -> tuple[int, int]:
    """Compute full (SizeX, SizeY) from a possibly-offset content bounds.

    This matches the C++ behavior: when metadata doesn't provide the full image
    size, fall back to the maximal extent implied by the bounds (x+width, y+height),
    not just (width, height).
    """
    return max(0, bounds.x + bounds.width), max(0, bounds.y + bounds.height)


def _clip_bounds_to_size(bounds: Bounds, *, size_l0: tuple[int, int]) -> Bounds:
    """Clip bounds to the full level-0 image size.

    Args:
        bounds: Bounds in level-0 pixel coordinates.
        size_l0: Full level-0 image size as (width, height).

    Returns:
        Bounds clipped to [0, size] and with non-negative width/height.
    """
    size_x, size_y = size_l0
    size_x = max(0, int(size_x))
    size_y = max(0, int(size_y))

    x1 = min(max(int(bounds.x), 0), size_x)
    y1 = min(max(int(bounds.y), 0), size_y)
    x2 = min(max(int(bounds.x + bounds.width), 0), size_x)
    y2 = min(max(int(bounds.y + bounds.height), 0), size_y)
    if x2 < x1:
        x2 = x1
    if y2 < y1:
        y2 = y1
    return Bounds(x=x1, y=y1, width=x2 - x1, height=y2 - y1)


class CziReader:
    """Read and decode Zeiss CZI tiles."""

    def __init__(self, path: str | Path, *, base_offset: int = 0):
        self._path = Path(path)
        self._base_offset = int(base_offset)
        self._header: CziHeader | None = None
        self._subblocks: list[Subblock] | None = None
        self._levels: dict[int, list[int]] | None = None  # downsample -> subblock indices
        self._bounds_l0_raw: Bounds | None = None
        self._bounds_l0: Bounds | None = None  # clipped/cached
        self._size_l0: tuple[int, int] | None = None
        self._spatial_bins: dict[int, dict[tuple[int, int], list[int]]] = {}
        self._spatial_bin_size: dict[int, int] = {}
        self._attachments: list[dict[str, object]] | None = None

    @property
    def path(self) -> Path:
        return self._path

    def read_header(self) -> CziHeader:
        if self._header is not None:
            return self._header

        with self._path.open("rb") as f:
            f.seek(self._base_offset)
            raw = f.read(_FILE_HDR_STRUCT.size)
        (
            sid,
            _allocated_size,
            _used_size,
            major,
            minor,
            _reserved1,
            _reserved2,
            primary_file_guid,
            file_guid,
            _file_part,
            subblk_dir_pos,
            meta_pos,
            _update_pending,
            att_dir_pos,
        ) = _FILE_HDR_STRUCT.unpack(raw)
        _check_magic(sid, _SID_ZISRAWFILE)

        self._header = CziHeader(
            path=self._path,
            major=int(major),
            minor=int(minor),
            primary_file_guid=primary_file_guid,
            file_guid=file_guid,
            subblk_dir_pos=int(subblk_dir_pos),
            meta_pos=int(meta_pos),
            att_dir_pos=int(att_dir_pos),
        )
        return self._header

    def _ensure_index(self) -> None:
        if self._subblocks is not None and self._levels is not None:
            return
        hdr = self.read_header()

        with self._path.open("rb") as f:
            # Read subblock directory header
            f.seek(self._base_offset + hdr.subblk_dir_pos)
            buf = f.read(_SUBBLK_DIR_HDR_STRUCT.size)
            sid, _allocated_size, used_size, entry_count, _reserved = _SUBBLK_DIR_HDR_STRUCT.unpack(buf)
            _check_magic(sid, _SID_ZISRAWDIRECTORY)

            # OpenSlide computation:
            # seg_size = used_size - sizeof(hdr) + sizeof(hdr.seg_hdr)
            seg_size = int(used_size) - _SUBBLK_DIR_HDR_STRUCT.size + _SEG_HDR_STRUCT.size
            if seg_size < 0:
                raise ValueError(f"Invalid subblock directory used_size={used_size}")

            buf_dir = f.read(seg_size)

        nsubblk = int(entry_count)
        p = 0
        avail = len(buf_dir)

        subblocks: list[Subblock] = []
        for i in range(nsubblk):
            if avail < _DIR_ENTRY_DV_STRUCT.size:
                raise ValueError("Premature end of directory when reading directory entry")
            (
                schema,
                pixel_type,
                file_pos,
                _file_part,
                compression,
                pyramid_type,
                _reserved1,
                _reserved2,
                ndimensions,
            ) = _DIR_ENTRY_DV_STRUCT.unpack_from(buf_dir, p)
            p += _DIR_ENTRY_DV_STRUCT.size
            avail -= _DIR_ENTRY_DV_STRUCT.size
            if schema != _SCHEMA_DV:
                raise ValueError(f"Unexpected directory entry schema: {schema!r}")

            x = 0
            y = 0
            w = 0
            h = 0
            scene = 0
            channel = 0
            z_index = 0
            downsample_i = 1

            ndim = int(ndimensions)
            for _ in range(ndim):
                if avail < _DIM_ENTRY_DV_STRUCT.size:
                    raise ValueError("Premature end of directory when reading dimension")
                dim_raw, start, size0, _start_coord, stored_size = _DIM_ENTRY_DV_STRUCT.unpack_from(buf_dir, p)
                p += _DIM_ENTRY_DV_STRUCT.size
                avail -= _DIM_ENTRY_DV_STRUCT.size

                name = _decode_ascii_name(dim_raw)
                start = int(start)
                size0 = int(size0)
                stored_size = int(stored_size)

                if name == "X":
                    x = start
                    w = stored_size
                    downsample_i = _div_round_closest(size0, stored_size)
                elif name == "Y":
                    y = start
                    h = stored_size
                elif name == "S":
                    scene = start
                elif name == "C":
                    # Brightfield is typically C=0, but keep it general.
                    channel = start
                elif name == "M":
                    z_index = start
                else:
                    raise ValueError(f'Unrecognized subblock dimension "{name}"')

            if w <= 0 or h <= 0:
                raise ValueError("Missing X or Y dimension in directory entry")

            subblocks.append(
                Subblock(
                    index=i,
                    file_pos=int(file_pos),
                    pixel_type=int(pixel_type),
                    compression=int(compression),
                    pyramid_type=int(pyramid_type),
                    ndimensions=ndim,
                    x=int(x),
                    y=int(y),
                    w=int(w),
                    h=int(h),
                    scene=int(scene),
                    channel=int(channel),
                    z_index=int(z_index),
                    downsample_i=int(downsample_i),
                )
            )

        if avail != 0:
            raise ValueError(f"Found trailing bytes after subblock directory: {avail}")

        # Adjust coordinate origin (OpenSlide): subtract min X/Y so top-left is (0,0)
        min_x = min(sb.x for sb in subblocks)
        min_y = min(sb.y for sb in subblocks)
        if min_x != 0 or min_y != 0:
            subblocks = [replace(sb, x=sb.x - min_x, y=sb.y - min_y) for sb in subblocks]

        # Compute bounds from level-0 tiles (OpenSlide's scene boundary computation
        # uses only downsample==1). If there are no downsample==1 tiles, fall back
        # to all tiles.
        l0 = [sb for sb in subblocks if sb.downsample_i == 1]
        if not l0:
            l0 = subblocks
        bx = min(sb.x for sb in l0)
        by = min(sb.y for sb in l0)
        bx2 = max(sb.x + sb.w for sb in l0)
        by2 = max(sb.y + sb.h for sb in l0)
        self._bounds_l0_raw = Bounds(x=int(bx), y=int(by), width=int(bx2 - bx), height=int(by2 - by))
        self._bounds_l0 = None

        levels: dict[int, list[int]] = {}
        for sb in subblocks:
            levels.setdefault(sb.downsample_i, []).append(sb.index)

        self._subblocks = subblocks
        self._levels = levels

    def iter_subblocks(self) -> Iterator[Subblock]:
        self._ensure_index()
        assert self._subblocks is not None
        yield from self._subblocks

    def levels(self) -> dict[int, LevelInfo]:
        self._ensure_index()
        assert self._levels is not None
        base_w, base_h = self.size_l0()

        out: dict[int, LevelInfo] = {}
        for ds, idxs in self._levels.items():
            out[ds] = LevelInfo(
                downsample=int(ds),
                tile_count=len(idxs),
                width=max(1, base_w // int(ds)),
                height=max(1, base_h // int(ds)),
            )
        return out

    def level_downsamples(self) -> list[int]:
        """Return downsamples sorted as OpenSlide levels (level 0 is downsample 1)."""
        self._ensure_index()
        assert self._levels is not None
        return sorted(self._levels.keys())

    def get_level_count(self) -> int:
        return len(self.level_downsamples())

    def get_level_downsample(self, level: int) -> int:
        ds_list = self.level_downsamples()
        if level < 0 or level >= len(ds_list):
            raise IndexError(f"level out of range: {level}")
        return ds_list[level]

    def get_level_dimensions(self, level: int) -> tuple[int, int]:
        ds = self.get_level_downsample(level)
        info = self.levels()[ds]
        return info.width, info.height

    def bounds(self) -> Bounds:
        """Return integer bounds for level-0 content (downsample==1).

        The returned bounds are clipped to the full level-0 image size.
        """
        self._ensure_index()
        assert self._bounds_l0_raw is not None
        if self._bounds_l0 is None:
            self._bounds_l0 = _clip_bounds_to_size(self._bounds_l0_raw, size_l0=self.size_l0())
        return self._bounds_l0

    def metadata_image_size_l0(self) -> tuple[int, int] | None:
        """Return (SizeX, SizeY) parsed from metadata XML, if available."""
        xml = self.read_metadata_xml()
        if not xml:
            return None
        try:
            root = ET.fromstring(xml)
        except ET.ParseError:
            return None

        # Typical paths (as seen in OpenSlide docs):
        # ImageDocument/Metadata/Information/Image/SizeX, SizeY
        size_x_text = root.findtext("./Metadata/Information/Image/SizeX")
        size_y_text = root.findtext("./Metadata/Information/Image/SizeY")
        if not size_x_text or not size_y_text:
            return None
        try:
            return int(size_x_text), int(size_y_text)
        except ValueError:
            return None

    def metadata_mpp(self) -> tuple[float | None, float | None]:
        """Return (mpp_x, mpp_y) in microns-per-pixel from metadata XML.

        Mirrors OpenSlide's Zeiss handling:
        - reads zeiss.Scaling.Items.X.Value and zeiss.Scaling.Items.Y.Value (meters/pixel)
        - multiplies by 1e6 to get microns/pixel
        """
        xml = self.read_metadata_xml()
        if not xml:
            return None, None
        try:
            root = ET.fromstring(xml)
        except ET.ParseError:
            return None, None

        def find_distance_value(axis: str) -> float | None:
            # <Scaling><Items><Distance Id="X"><Value>...</Value></Distance></Items></Scaling>
            for dist in root.findall(".//Metadata/Scaling/Items/Distance"):
                if dist.get("Id") == axis:
                    val = dist.findtext("./Value")
                    if not val:
                        return None
                    try:
                        meters_per_px = float(val)
                    except ValueError:
                        return None
                    return meters_per_px * 1_000_000.0
            # Fallback: sometimes Id is an attribute on nested elements; be conservative.
            return None

        return find_distance_value("X"), find_distance_value("Y")

    def metadata_objective_power(self) -> float | None:
        """Return objective nominal magnification (objective-power) from metadata XML.

        Mirrors OpenSlide's Zeiss handling:
        - read ObjectiveSettings/ObjectiveRef/@Id to get e.g. 'Objective:1'
        - lookup Instrument/Objectives/Objective[@Id=...]/NominalMagnification
        """
        xml = self.read_metadata_xml()
        if not xml:
            return None
        try:
            root = ET.fromstring(xml)
        except ET.ParseError:
            return None

        # <ObjectiveSettings><ObjectiveRef Id="Objective:1"/></ObjectiveSettings>
        obj_ref = root.find(".//Metadata/Information/Image/ObjectiveSettings/ObjectiveRef")
        if obj_ref is None:
            return None
        obj_id = obj_ref.get("Id")
        if not obj_id:
            return None

        # <Instrument><Objectives><Objective Id="Objective:1"><NominalMagnification>40</NominalMagnification>...
        for obj in root.findall(".//Metadata/Information/Instrument/Objectives/Objective"):
            if obj.get("Id") == obj_id:
                text = obj.findtext("./NominalMagnification")
                if not text:
                    return None
                try:
                    return float(text)
                except ValueError:
                    return None
        return None

    def size_l0(self) -> tuple[int, int]:
        """Return the 'actual' level-0 (SizeX, SizeY) when available.

        If metadata doesn't contain SizeX/SizeY, fall back to computed bounds size.
        """
        if self._size_l0 is not None:
            return self._size_l0

        meta = self.metadata_image_size_l0()
        if meta is not None:
            self._size_l0 = meta
            return meta

        self._ensure_index()
        assert self._bounds_l0_raw is not None
        self._size_l0 = _size_from_bounds(self._bounds_l0_raw)
        return self._size_l0

    def iter_tiles(self, downsample: int, *, scene: int | None = None) -> Iterator[Tile]:
        self._ensure_index()
        assert self._levels is not None
        assert self._subblocks is not None

        idxs = self._levels.get(int(downsample), [])
        for i in idxs:
            sb = self._subblocks[i]
            if scene is not None and sb.scene != scene:
                continue
            yield Tile(path=self._path, subblock=sb)

    def _ensure_spatial_index(self, downsample: int) -> None:
        """Build a simple spatial hash for a given downsample level."""
        self._ensure_index()
        assert self._subblocks is not None
        assert self._levels is not None

        ds = int(downsample)
        if ds in self._spatial_bins:
            return

        idxs = self._levels.get(ds, [])
        if not idxs:
            self._spatial_bins[ds] = {}
            self._spatial_bin_size[ds] = 1
            return

        # Choose a bin size in *level coordinates* based on typical tile size.
        # (Tiles can vary, but this keeps the hash reasonably small.)
        max_dim = 1
        for i in idxs:
            sb = self._subblocks[i]
            max_dim = max(max_dim, sb.w, sb.h)
        bin_size = max_dim

        bins: dict[tuple[int, int], list[int]] = {}
        for i in idxs:
            sb = self._subblocks[i]
            tx = sb.x // ds
            ty = sb.y // ds
            bx = tx // bin_size
            by = ty // bin_size
            bins.setdefault((bx, by), []).append(i)

        self._spatial_bins[ds] = bins
        self._spatial_bin_size[ds] = bin_size

    def _iter_candidate_tile_indices_for_region(
        self, *, downsample: int, rx: int, ry: int, rw: int, rh: int
    ) -> Iterator[int]:
        """Yield candidate tile indices intersecting region in *level coordinates*."""
        self._ensure_spatial_index(downsample)
        ds = int(downsample)
        bins = self._spatial_bins[ds]
        bin_size = self._spatial_bin_size[ds]
        if not bins:
            return

        # Region bins (inclusive)
        bx0 = rx // bin_size
        by0 = ry // bin_size
        bx1 = ((rx + rw - 1) // bin_size) if rw > 0 else bx0
        by1 = ((ry + rh - 1) // bin_size) if rh > 0 else by0

        seen: set[int] = set()
        for by in range(by0, by1 + 1):
            for bx in range(bx0, bx1 + 1):
                for idx in bins.get((bx, by), []):
                    if idx in seen:
                        continue
                    seen.add(idx)
                    yield idx

    @staticmethod
    @functools.lru_cache(maxsize=1)
    def _srgb_u8_to_linear_lut() -> np.ndarray:
        v = np.arange(256, dtype=np.float32) / 255.0
        # sRGB -> linear
        lin = np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)
        return lin.astype(np.float32)

    @staticmethod
    @functools.lru_cache(maxsize=1)
    def _linear_to_srgb_u8_lut() -> np.ndarray:
        # Build LUT for linear values in [0,1] quantized to 4096 steps.
        n = 4096
        v = np.linspace(0.0, 1.0, n, dtype=np.float32)
        srgb = np.where(v <= 0.0031308, v * 12.92, 1.055 * (v ** (1.0 / 2.4)) - 0.055)
        srgb_u8 = np.clip(np.round(srgb * 255.0), 0, 255).astype(np.uint8)
        return srgb_u8

    def read_region(
        self,
        *,
        downsample: int | None = None,
        level: int | None = None,
        x: int,
        y: int,
        w: int,
        h: int,
        output: Literal["rgb", "rgba"] = "rgb",
    ) -> np.ndarray:
        """Read an RGB/RGBA region at a given downsample, averaging overlaps.

        Semantics (OpenSlide-like):
        - `x`, `y` are in **level-0 pixel coordinates**.
        - `w`, `h` are the **output pixel dimensions** at the requested `downsample`.
        - The sampled origin in level coordinates is `(x // downsample, y // downsample)`.

        Overlaps are resolved by averaging in linear RGB space:
        - Convert sRGB->linear
        - Average overlapping contributions
        - Convert linear->sRGB
        """
        self._ensure_index()
        assert self._subblocks is not None

        if (downsample is None) == (level is None):
            raise ValueError("Specify exactly one of downsample= or level=")

        ds = int(downsample) if downsample is not None else self.get_level_downsample(int(level))
        if ds <= 0:
            raise ValueError("downsample must be > 0")
        if w <= 0 or h <= 0:
            raise ValueError("w and h must be > 0")

        # NOTE: OpenSlide places tiles at *fractional* positions in level
        # coordinates: (double)tile_x / downsample (same for y). That preserves
        # non-multiple-of-downsample offsets and avoids systematic seams.
        #
        # Our implementation currently rasterizes onto an integer grid, so we
        # approximate fractional placement by rounding. If you still see seams,
        # the correct fix is to support fractional placement with resampling
        # (or a Cairo-like compositor), rather than integer slicing.
        rx = int(round(int(x) / ds))
        ry = int(round(int(y) / ds))
        rw = int(w)
        rh = int(h)

        acc = np.zeros((rh, rw, 3), dtype=np.float32)
        cnt = np.zeros((rh, rw, 1), dtype=np.float32)

        srgb_to_lin = self._srgb_u8_to_linear_lut()
        lin_to_srgb = self._linear_to_srgb_u8_lut()
        lin_lut_n = int(lin_to_srgb.size)

        for idx in self._iter_candidate_tile_indices_for_region(downsample=ds, rx=rx, ry=ry, rw=rw, rh=rh):
            sb = self._subblocks[idx]
            tx = int(round(sb.x / ds))
            ty = int(round(sb.y / ds))
            tw = sb.w
            th = sb.h

            ix0 = max(rx, tx)
            iy0 = max(ry, ty)
            ix1 = min(rx + rw, tx + tw)
            iy1 = min(ry + rh, ty + th)
            if ix1 <= ix0 or iy1 <= iy0:
                continue

            tile = Tile(path=self._path, subblock=sb)
            tile_rgb = np.frombuffer(bytes(self.read_tile(tile, output="rgb")), dtype=np.uint8).reshape(th, tw, 3)
            tile_lin = srgb_to_lin[tile_rgb]  # float32 in [0,1]

            out_x0 = ix0 - rx
            out_y0 = iy0 - ry
            out_x1 = ix1 - rx
            out_y1 = iy1 - ry

            t_x0 = ix0 - tx
            t_y0 = iy0 - ty
            t_x1 = ix1 - tx
            t_y1 = iy1 - ty

            acc[out_y0:out_y1, out_x0:out_x1, :] += tile_lin[t_y0:t_y1, t_x0:t_x1, :]
            cnt[out_y0:out_y1, out_x0:out_x1, :] += 1.0

        avg_lin = np.divide(acc, cnt, out=np.zeros_like(acc), where=(cnt > 0))
        lut_idx = np.clip(np.round(avg_lin * (lin_lut_n - 1)).astype(np.int32), 0, lin_lut_n - 1)
        rgb_u8 = lin_to_srgb[lut_idx].astype(np.uint8)

        if output == "rgb":
            return rgb_u8
        if output == "rgba":
            a = np.full((rh, rw, 1), 255, dtype=np.uint8)
            return np.concatenate([rgb_u8, a], axis=2)
        raise ValueError(f"Unsupported output: {output}")

    def read_tile(
        self,
        tile: Tile,
        *,
        output: Literal["rgba", "bgr", "rgb", "gray"] = "rgba",
    ) -> memoryview:
        """Read and decode a single tile.

        Returns a `memoryview` of bytes. For `output="rgba"`, this is
        `w * h * 4` bytes in RGBA8888.
        """
        sb = tile.subblock
        with self._path.open("rb") as f:
            f.seek(self._base_offset + sb.file_pos)
            hdr_bytes = f.read(_SUBBLK_HDR_STRUCT.size)
            sid, _allocated_size, _used_size, meta_size, _attach_size, data_size = _SUBBLK_HDR_STRUCT.unpack(hdr_bytes)
            _check_magic(sid, _SID_ZISRAWSUBBLOCK)

            data_pos = self._base_offset + sb.file_pos + _CZI_SUBBLK_HDR_LEN + int(meta_size)
            f.seek(data_pos)
            payload = f.read(int(data_size))

        decoded = decode_tile_pixels(
            payload=payload,
            compression=sb.compression,
            pixel_type=sb.pixel_type,
            w=sb.w,
            h=sb.h,
            output=output,
        )
        return memoryview(decoded)

    def read_metadata_xml(self) -> str | None:
        """Read the (large) metadata XML document, if present."""
        hdr = self.read_header()
        if hdr.meta_pos == 0:
            return None
        with self._path.open("rb") as f:
            f.seek(self._base_offset + hdr.meta_pos)
            meta_hdr = f.read(_META_HDR_STRUCT.size)
            sid, _allocated_size, _used_size, xml_size, _attach_size, _reserved = _META_HDR_STRUCT.unpack(meta_hdr)
            _check_magic(sid, _SID_ZISRAWMETADATA)
            xml = f.read(int(xml_size))
        return xml.decode("utf-8", errors="replace")

    def attachments(self) -> list[dict[str, object]]:
        """Return raw attachment directory entries.

        Each entry dict contains:
        - name: str
        - file_type: str (e.g. 'JPG', 'CZI')
        - file_pos: int (relative to this CZI's base_offset)
        - guid: bytes (16 bytes)
        """
        if self._attachments is not None:
            return self._attachments

        hdr = self.read_header()
        if hdr.att_dir_pos == 0:
            self._attachments = []
            return self._attachments

        entries: list[dict[str, object]] = []
        with self._path.open("rb") as f:
            f.seek(self._base_offset + hdr.att_dir_pos)
            buf = f.read(_ATT_DIR_HDR_STRUCT.size)
            sid, _alloc, _used, entry_count, _reserved = _ATT_DIR_HDR_STRUCT.unpack(buf)
            _check_magic(sid, _SID_ZISRAWATTDIR)

            for _ in range(int(entry_count)):
                raw = f.read(_ATT_ENTRY_A1_STRUCT.size)
                schema, _r2, file_pos, _file_part, guid, file_type, name = _ATT_ENTRY_A1_STRUCT.unpack(raw)
                if schema != _SCHEMA_A1:
                    raise ValueError(f"Unexpected attachment schema: {schema!r}")
                entries.append(
                    {
                        "name": _decode_ascii_name(name),
                        "file_type": _decode_ascii_name(file_type),
                        "file_pos": int(file_pos),
                        "guid": bytes(guid),
                    }
                )

        self._attachments = entries
        return self._attachments

    def associated_images(self) -> dict[str, dict[str, object]]:
        """Return OpenSlide-like associated images mapping.

        Keys are: 'label', 'macro', 'thumbnail' when present.
        """
        mapping = {"Label": "label", "SlidePreview": "macro", "Thumbnail": "thumbnail"}
        out: dict[str, dict[str, object]] = {}
        for e in self.attachments():
            czi_name = str(e["name"])
            os_name = mapping.get(czi_name)
            if not os_name:
                continue
            file_type = str(e["file_type"])
            if file_type not in ("JPG", "CZI"):
                continue
            # Like OpenSlide: data_offset = att.file_pos + sizeof(zisraw_seg_att_hdr)
            data_offset = self._base_offset + int(e["file_pos"]) + _CZI_ATTACHMENT_HDR_LEN
            out[os_name] = {
                "file_type": file_type,
                "data_offset": data_offset,
                "source_name": czi_name,
            }
        return out

    def read_associated_image(self, name: str, *, output: Literal["rgb", "rgba"] = "rgba") -> np.ndarray:
        """Read an associated image ('label', 'macro', 'thumbnail') into a numpy array."""
        assoc = self.associated_images().get(name)
        if not assoc:
            raise KeyError(f"Associated image not found: {name!r}")

        file_type = str(assoc["file_type"])
        data_offset = int(assoc["data_offset"])

        if file_type == "JPG":
            try:
                import imagecodecs  # type: ignore
            except Exception as e:  # pragma: no cover
                raise ImportError("Reading JPG associated images requires 'imagecodecs'.") from e

            # Need data_size: read attachment segment prefix at (data_offset - 288)
            seg_pos = data_offset - _CZI_ATTACHMENT_HDR_LEN
            with self._path.open("rb") as f:
                f.seek(seg_pos)
                prefix = f.read(_ATT_SEG_HDR_PREFIX_STRUCT.size)
                sid, _alloc, _used, data_size = _ATT_SEG_HDR_PREFIX_STRUCT.unpack(prefix)
                _check_magic(sid, _SID_ZISRAWATTACH)
                f.seek(data_offset)
                jpg = f.read(int(data_size))
            arr = imagecodecs.jpeg_decode(jpg)
            if arr.ndim == 2:
                arr = np.repeat(arr[..., None], 3, axis=2)
            if output == "rgb":
                return arr.astype(np.uint8, copy=False)
            if output == "rgba":
                a = np.full((*arr.shape[:2], 1), 255, dtype=np.uint8)
                return np.concatenate([arr.astype(np.uint8, copy=False), a], axis=2)
            raise ValueError(f"Unsupported output: {output}")

        if file_type == "CZI":
            # Embedded CZI begins at data_offset. Bio-Formats reads the embedded CZI
            # as a normal CZI and returns the pixel buffer at native bit depth
            # (e.g. UINT16 for BGR48), letting the viewer decide display scaling.
            #
            # IMPORTANT: Do NOT emulate OpenSlide's associated image conversion here.
            # OpenSlide truncates BGR48 to 8-bit by taking the high byte, which can
            # look extremely dark for 12-bit-in-16-bit data.
            sub = CziReader(self._path, base_offset=data_offset)
            sub._ensure_index()
            assert sub._subblocks is not None
            if len(sub._subblocks) != 1:
                raise ValueError(f"Embedded CZI associated image has {len(sub._subblocks)} subblocks, expected 1")
            sb = sub._subblocks[0]

            # Read/decompress raw pixel bytes (no colorspace/gamma handling).
            with self._path.open("rb") as f:
                f.seek(data_offset + sb.file_pos)
                hdr_bytes = f.read(_SUBBLK_HDR_STRUCT.size)
                sid, _alloc, _used, meta_size, _attach_size, data_size = _SUBBLK_HDR_STRUCT.unpack(hdr_bytes)
                _check_magic(sid, _SID_ZISRAWSUBBLOCK)
                f.seek(data_offset + sb.file_pos + _CZI_SUBBLK_HDR_LEN + int(meta_size))
                payload = f.read(int(data_size))

            # Decompress if needed, following OpenSlide's supported cases.
            bpp = 3 if sb.pixel_type == 3 else 6 if sb.pixel_type == 4 else None
            if bpp is None:
                raise ValueError(f"Unsupported associated image pixel type: {sb.pixel_type}")
            expected = sb.w * sb.h * bpp

            raw = payload
            if sb.compression in (5, 6):  # zstd0 / zstd1
                import zstandard as zstd

                do_hilo = False
                if sb.compression == 6:  # zstd1
                    if len(raw) < 1:
                        raise ValueError("zstd1 payload too small")
                    hdr_len = raw[0]
                    if len(raw) < hdr_len:
                        raise ValueError("zstd1 payload too small for header")
                    if hdr_len == 3:
                        chunk_type = raw[1]
                        if chunk_type != 1:
                            raise ValueError(f"Unexpected zstd1 chunk type: {chunk_type}")
                        do_hilo = bool(raw[2] & 1)
                    elif hdr_len != 1:
                        raise ValueError(f"Unexpected zstd1 header length: {hdr_len}")
                    raw = raw[hdr_len:]

                dctx = zstd.ZstdDecompressor()
                raw = dctx.decompress(raw, max_output_size=expected)
                if len(raw) != expected:
                    raise ValueError(f"zstd decompressed size mismatch: got {len(raw)}, expected {expected}")

                if do_hilo:
                    if len(raw) % 2:
                        raise ValueError("Can't HiLo-unpack odd byte count")
                    half = len(raw) // 2
                    lo = memoryview(raw)[:half]
                    hi = memoryview(raw)[half:]
                    out = bytearray(len(raw))
                    out_mv = memoryview(out)
                    out_mv[0::2] = lo
                    out_mv[1::2] = hi
                    raw = bytes(out)
            elif sb.compression != 0:
                raise ValueError(f"Unsupported associated image compression: {sb.compression}")

            # Convert BGR -> RGB, preserving native bit depth (Bio-Formats style).
            if sb.pixel_type == 3:  # BGR24
                bgr8 = np.frombuffer(raw, dtype=np.uint8).reshape(sb.h, sb.w, 3)
                rgb8 = bgr8[..., ::-1]
                if output == "rgb":
                    return rgb8
                if output == "rgba":
                    a = np.full((sb.h, sb.w, 1), 255, dtype=np.uint8)
                    return np.concatenate([rgb8, a], axis=2)
                raise ValueError(f"Unsupported output: {output}")

            if sb.pixel_type == 4:  # BGR48
                bgr16 = np.frombuffer(raw, dtype="<u2").reshape(sb.h, sb.w, 3)  # B, G, R
                rgb16 = bgr16[..., ::-1]  # R, G, B
                if output == "rgb":
                    return rgb16
                if output == "rgba":
                    a = np.full((sb.h, sb.w, 1), 65535, dtype=np.uint16)
                    return np.concatenate([rgb16, a], axis=2)
                raise ValueError(f"Unsupported output: {output}")

            raise ValueError(f"Unsupported associated image pixel type: {sb.pixel_type}")

        raise ValueError(f"Unsupported associated image type: {file_type}")
