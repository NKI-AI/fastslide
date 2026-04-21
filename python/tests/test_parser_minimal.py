from __future__ import annotations

from pathlib import Path
import struct

import pytest
import zstandard as zstd

from czi_reader import CziReader
from czi_reader.structs import Compression, PixelType


_FILE_HDR_STRUCT = struct.Struct("<16sqqiiii16s16siqqiq")
_SUBBLK_DIR_HDR_STRUCT = struct.Struct("<16sqqi124s")
_DIR_ENTRY_DV_STRUCT = struct.Struct("<2siqiibc4si")
_DIM_ENTRY_DV_STRUCT = struct.Struct("<4siifi")
_SUBBLK_HDR_STRUCT = struct.Struct("<16sqqiiq")


def _write_minimal_czi(
    path: Path,
    *,
    pixel_type: int,
    compression: int,
    tile_w: int,
    tile_h: int,
    tile_x: int = 0,
    tile_y: int = 0,
    downsample_size_x: int | None = None,
    downsample_size_y: int | None = None,
    subblock_payload: bytes,
) -> None:
    subblk_dir_pos = 128
    subblock_pos = 4096

    if downsample_size_x is None:
        downsample_size_x = tile_w
    if downsample_size_y is None:
        downsample_size_y = tile_h

    # File header (ZISRAWFILE)
    file_hdr = _FILE_HDR_STRUCT.pack(
        b"ZISRAWFILE\x00\x00\x00\x00\x00\x00\x00",
        0,
        0,
        1,
        0,
        0,
        0,
        b"\x01" * 16,
        b"\x02" * 16,
        0,
        subblk_dir_pos,
        0,
        0,
        0,
    )

    # One directory entry (DV) + dimensions: X, Y, S, C, M
    dims = b"".join(
        [
            _DIM_ENTRY_DV_STRUCT.pack(b"X\x00\x00\x00", tile_x, downsample_size_x, 0.0, tile_w),
            _DIM_ENTRY_DV_STRUCT.pack(b"Y\x00\x00\x00", tile_y, downsample_size_y, 0.0, tile_h),
            _DIM_ENTRY_DV_STRUCT.pack(b"S\x00\x00\x00", 0, 1, 0.0, 1),
            _DIM_ENTRY_DV_STRUCT.pack(b"C\x00\x00\x00", 0, 1, 0.0, 1),
            _DIM_ENTRY_DV_STRUCT.pack(b"M\x00\x00\x00", 0, 1, 0.0, 1),
        ]
    )
    entry = _DIR_ENTRY_DV_STRUCT.pack(
        b"DV",
        int(pixel_type),
        subblock_pos,
        0,
        int(compression),
        0,
        b"\x00",
        b"\x00" * 4,
        5,
    )
    dir_payload = entry + dims

    # Subblock directory header (ZISRAWDIRECTORY)
    used_size = (_SUBBLK_DIR_HDR_STRUCT.size - 32) + len(dir_payload)
    subblk_dir_hdr = _SUBBLK_DIR_HDR_STRUCT.pack(
        b"ZISRAWDIRECTORY\x00",
        used_size,
        used_size,
        1,
        b"\x00" * 124,
    )

    # Subblock header (ZISRAWSUBBLOCK) is 288 bytes total; our reader reads first 48 bytes,
    # then seeks to pos + 288 + meta_size.
    meta_size = 0
    attach_size = 0
    data_size = len(subblock_payload)
    subblk_hdr_48 = _SUBBLK_HDR_STRUCT.pack(
        b"ZISRAWSUBBLOCK\x00\x00\x00\x00",
        0,
        0,
        meta_size,
        attach_size,
        data_size,
    )
    subblk_hdr_288 = subblk_hdr_48 + (b"\x00" * (288 - len(subblk_hdr_48)))

    blob = bytearray()
    blob += file_hdr
    if len(blob) < subblk_dir_pos:
        blob += b"\x00" * (subblk_dir_pos - len(blob))
    blob += subblk_dir_hdr
    blob += dir_payload

    if len(blob) < subblock_pos:
        blob += b"\x00" * (subblock_pos - len(blob))
    blob += subblk_hdr_288
    blob += subblock_payload

    path.write_bytes(bytes(blob))


def test_parse_header_and_levels_and_read_uncompressed_bgr24(tmp_path: Path) -> None:
    p = tmp_path / "t.czi"
    # 2x1 tile, BGR24, uncompressed
    bgr = bytes([10, 20, 30, 40, 50, 60])
    _write_minimal_czi(
        p,
        pixel_type=PixelType.BGR24,
        compression=Compression.NONE,
        tile_w=2,
        tile_h=1,
        subblock_payload=bgr,
    )
    r = CziReader(p)
    hdr = r.read_header()
    assert hdr.major == 1
    assert hdr.subblk_dir_pos == 128

    levels = r.levels()
    assert sorted(levels.keys()) == [1]
    assert levels[1].tile_count == 1
    assert r.bounds().x == 0
    assert r.bounds().y == 0
    assert r.bounds().width == 2
    assert r.bounds().height == 1

    tile = next(r.iter_tiles(1))
    rgba = bytes(r.read_tile(tile, output="rgba"))
    assert rgba == bytes([30, 20, 10, 255, 60, 50, 40, 255])


def test_zstd1_hilo_gray16(tmp_path: Path) -> None:
    p = tmp_path / "hilo.czi"

    # Two uint16 samples: 0x1122, 0x3344 -> little endian bytes: 22 11 44 33
    raw = bytes([0x22, 0x11, 0x44, 0x33])
    # HiLo packed form: low bytes first (22 44), then high bytes (11 33)
    hilo = bytes([0x22, 0x44, 0x11, 0x33])
    comp = zstd.ZstdCompressor(level=1).compress(hilo)
    # zstd1 header: size=3, chunk_type=1, is_hi_low_pack=1
    payload = bytes([3, 1, 1]) + comp

    _write_minimal_czi(
        p,
        pixel_type=PixelType.GRAY16,
        compression=Compression.ZSTD1,
        tile_w=2,
        tile_h=1,
        subblock_payload=payload,
    )

    r = CziReader(p)
    tile = next(r.iter_tiles(1))
    out = bytes(r.read_tile(tile, output="gray"))
    # raw == 22 11 44 33 -> gray output uses high byte => 11, 33
    assert out == bytes([0x11, 0x33])


def test_coordinate_origin_adjustment(tmp_path: Path) -> None:
    # Two tiles: one at X=-5, another at X=5. After adjustment, min X becomes 0.
    # We'll encode them as separate files to keep the helper simple, but still validate
    # the behavior on a multi-tile directory by writing a custom buffer here.
    p = tmp_path / "origin.czi"

    subblk_dir_pos = 128
    sb0_pos = 4096
    sb1_pos = 8192

    file_hdr = _FILE_HDR_STRUCT.pack(
        b"ZISRAWFILE\x00\x00\x00\x00\x00\x00\x00",
        0,
        0,
        1,
        0,
        0,
        0,
        b"\x01" * 16,
        b"\x02" * 16,
        0,
        subblk_dir_pos,
        0,
        0,
        0,
    )

    def entry(file_pos: int, x: int) -> bytes:
        dims = b"".join(
            [
                _DIM_ENTRY_DV_STRUCT.pack(b"X\x00\x00\x00", x, 2, 0.0, 2),
                _DIM_ENTRY_DV_STRUCT.pack(b"Y\x00\x00\x00", 0, 1, 0.0, 1),
                _DIM_ENTRY_DV_STRUCT.pack(b"S\x00\x00\x00", 0, 1, 0.0, 1),
                _DIM_ENTRY_DV_STRUCT.pack(b"C\x00\x00\x00", 0, 1, 0.0, 1),
                _DIM_ENTRY_DV_STRUCT.pack(b"M\x00\x00\x00", 0, 1, 0.0, 1),
            ]
        )
        e = _DIR_ENTRY_DV_STRUCT.pack(
            b"DV",
            int(PixelType.BGR24),
            file_pos,
            0,
            int(Compression.NONE),
            0,
            b"\x00",
            b"\x00" * 4,
            5,
        )
        return e + dims

    dir_payload = entry(sb0_pos, -5) + entry(sb1_pos, 5)
    used_size = (_SUBBLK_DIR_HDR_STRUCT.size - 32) + len(dir_payload)
    subblk_dir_hdr = _SUBBLK_DIR_HDR_STRUCT.pack(
        b"ZISRAWDIRECTORY\x00",
        used_size,
        used_size,
        2,
        b"\x00" * 124,
    )

    bgr = bytes([0, 0, 0, 0, 0, 0])
    subblk_hdr_48 = _SUBBLK_HDR_STRUCT.pack(
        b"ZISRAWSUBBLOCK\x00\x00\x00\x00",
        0,
        0,
        0,
        0,
        len(bgr),
    )
    subblk_hdr_288 = subblk_hdr_48 + (b"\x00" * (288 - len(subblk_hdr_48)))

    blob = bytearray()
    blob += file_hdr
    blob += b"\x00" * (subblk_dir_pos - len(blob))
    blob += subblk_dir_hdr
    blob += dir_payload
    blob += b"\x00" * (sb0_pos - len(blob))
    blob += subblk_hdr_288
    blob += bgr
    blob += b"\x00" * (sb1_pos - len(blob))
    blob += subblk_hdr_288
    blob += bgr
    p.write_bytes(bytes(blob))

    r = CziReader(p)
    sbs = list(r.iter_subblocks())
    assert sbs[0].x == 0
    assert sbs[1].x == 10


def test_reject_unknown_dimension(tmp_path: Path) -> None:
    p = tmp_path / "bad_dim.czi"
    # Put a bogus dimension "Q"
    subblk_dir_pos = 128
    subblock_pos = 4096

    file_hdr = _FILE_HDR_STRUCT.pack(
        b"ZISRAWFILE\x00\x00\x00\x00\x00\x00\x00",
        0,
        0,
        1,
        0,
        0,
        0,
        b"\x01" * 16,
        b"\x02" * 16,
        0,
        subblk_dir_pos,
        0,
        0,
        0,
    )

    dims = b"".join(
        [
            _DIM_ENTRY_DV_STRUCT.pack(b"X\x00\x00\x00", 0, 1, 0.0, 1),
            _DIM_ENTRY_DV_STRUCT.pack(b"Y\x00\x00\x00", 0, 1, 0.0, 1),
            _DIM_ENTRY_DV_STRUCT.pack(b"Q\x00\x00\x00", 0, 1, 0.0, 1),
        ]
    )
    entry = _DIR_ENTRY_DV_STRUCT.pack(
        b"DV",
        int(PixelType.BGR24),
        subblock_pos,
        0,
        int(Compression.NONE),
        0,
        b"\x00",
        b"\x00" * 4,
        3,
    )
    dir_payload = entry + dims
    used_size = (_SUBBLK_DIR_HDR_STRUCT.size - 32) + len(dir_payload)
    subblk_dir_hdr = _SUBBLK_DIR_HDR_STRUCT.pack(
        b"ZISRAWDIRECTORY\x00",
        used_size,
        used_size,
        1,
        b"\x00" * 124,
    )

    bgr = bytes([0, 0, 0])
    subblk_hdr_48 = _SUBBLK_HDR_STRUCT.pack(
        b"ZISRAWSUBBLOCK\x00\x00\x00\x00",
        0,
        0,
        0,
        0,
        len(bgr),
    )
    subblk_hdr_288 = subblk_hdr_48 + (b"\x00" * (288 - len(subblk_hdr_48)))

    blob = bytearray()
    blob += file_hdr
    blob += b"\x00" * (subblk_dir_pos - len(blob))
    blob += subblk_dir_hdr
    blob += dir_payload
    blob += b"\x00" * (subblock_pos - len(blob))
    blob += subblk_hdr_288
    blob += bgr
    p.write_bytes(bytes(blob))

    r = CziReader(p)
    with pytest.raises(ValueError, match="Unrecognized subblock dimension"):
        _ = list(r.iter_subblocks())


def _srgb_u8_to_linear(v: int) -> float:
    x = v / 255.0
    if x <= 0.04045:
        return x / 12.92
    return ((x + 0.055) / 1.055) ** 2.4


def _linear_to_srgb_u8(v: float) -> int:
    v = max(0.0, min(1.0, v))
    if v <= 0.0031308:
        x = v * 12.92
    else:
        x = 1.055 * (v ** (1.0 / 2.4)) - 0.055
    return int(round(max(0.0, min(1.0, x)) * 255.0))


def test_read_region_averages_overlap_in_linear_rgb(tmp_path: Path) -> None:
    # Build a minimal CZI with two fully overlapping 2x1 tiles at ds=1:
    # tile0 = red, tile1 = green. read_region() should average them in linear space.
    p = tmp_path / "overlap.czi"

    subblk_dir_pos = 128
    sb0_pos = 4096
    sb1_pos = 8192

    file_hdr = _FILE_HDR_STRUCT.pack(
        b"ZISRAWFILE\x00\x00\x00\x00\x00\x00\x00",
        0,
        0,
        1,
        0,
        0,
        0,
        b"\x01" * 16,
        b"\x02" * 16,
        0,
        subblk_dir_pos,
        0,
        0,
        0,
    )

    def entry(file_pos: int) -> bytes:
        dims = b"".join(
            [
                _DIM_ENTRY_DV_STRUCT.pack(b"X\x00\x00\x00", 0, 2, 0.0, 2),
                _DIM_ENTRY_DV_STRUCT.pack(b"Y\x00\x00\x00", 0, 1, 0.0, 1),
                _DIM_ENTRY_DV_STRUCT.pack(b"S\x00\x00\x00", 0, 1, 0.0, 1),
                _DIM_ENTRY_DV_STRUCT.pack(b"C\x00\x00\x00", 0, 1, 0.0, 1),
                _DIM_ENTRY_DV_STRUCT.pack(b"M\x00\x00\x00", 0, 1, 0.0, 1),
            ]
        )
        e = _DIR_ENTRY_DV_STRUCT.pack(
            b"DV",
            int(PixelType.BGR24),
            file_pos,
            0,
            int(Compression.NONE),
            0,
            b"\x00",
            b"\x00" * 4,
            5,
        )
        return e + dims

    dir_payload = entry(sb0_pos) + entry(sb1_pos)
    used_size = (_SUBBLK_DIR_HDR_STRUCT.size - 32) + len(dir_payload)
    subblk_dir_hdr = _SUBBLK_DIR_HDR_STRUCT.pack(
        b"ZISRAWDIRECTORY\x00",
        used_size,
        used_size,
        2,
        b"\x00" * 124,
    )

    # 2 pixels: red then red (BGR = 0,0,255), and green then green (BGR = 0,255,0)
    bgr_red = bytes([0, 0, 255, 0, 0, 255])
    bgr_green = bytes([0, 255, 0, 0, 255, 0])

    def subblk(hdr_pos: int, payload: bytes) -> bytes:
        subblk_hdr_48 = _SUBBLK_HDR_STRUCT.pack(
            b"ZISRAWSUBBLOCK\x00\x00\x00\x00",
            0,
            0,
            0,
            0,
            len(payload),
        )
        subblk_hdr_288 = subblk_hdr_48 + (b"\x00" * (288 - len(subblk_hdr_48)))
        return subblk_hdr_288 + payload

    blob = bytearray()
    blob += file_hdr
    blob += b"\x00" * (subblk_dir_pos - len(blob))
    blob += subblk_dir_hdr
    blob += dir_payload
    blob += b"\x00" * (sb0_pos - len(blob))
    blob += subblk(sb0_pos, bgr_red)
    blob += b"\x00" * (sb1_pos - len(blob))
    blob += subblk(sb1_pos, bgr_green)
    p.write_bytes(bytes(blob))

    r = CziReader(p)
    out = r.read_region(downsample=1, x=0, y=0, w=2, h=1, output="rgb")
    assert out.shape == (1, 2, 3)

    # Expected per channel for the averaged pixel:
    lin_r = (_srgb_u8_to_linear(255) + _srgb_u8_to_linear(0)) / 2.0
    lin_g = (_srgb_u8_to_linear(0) + _srgb_u8_to_linear(255)) / 2.0
    lin_b = (_srgb_u8_to_linear(0) + _srgb_u8_to_linear(0)) / 2.0
    exp = [_linear_to_srgb_u8(lin_r), _linear_to_srgb_u8(lin_g), _linear_to_srgb_u8(lin_b)]
    assert out[0, 0, :].tolist() == exp
    assert out[0, 1, :].tolist() == exp
