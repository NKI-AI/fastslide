"""Tile decoders for CZI subblocks."""

from __future__ import annotations

import struct
from typing import Literal

import numpy as np
import zstandard as zstd

from .structs import Compression, PixelType


_ZSTD1_HDR_STRUCT = struct.Struct("<BBB")  # size, chunk_type, is_hi_low_pack


def _bgr24_to_rgba(src: bytes) -> bytes:
    # src is BGRBGR..., output RGBA with alpha=255
    b = np.frombuffer(src, dtype=np.uint8)
    rgb = b.reshape(-1, 3)[:, ::-1]  # to RGB
    a = np.full((rgb.shape[0], 1), 255, dtype=np.uint8)
    rgba = np.concatenate([rgb, a], axis=1)
    return rgba.tobytes()


def _bgr24_to_rgb(src: bytes) -> bytes:
    b = np.frombuffer(src, dtype=np.uint8)
    rgb = b.reshape(-1, 3)[:, ::-1]
    return rgb.tobytes()


def _bgr48_to_rgba(src: bytes) -> bytes:
    rgb = _bgr48_to_rgb(src)
    b = np.frombuffer(rgb, dtype=np.uint8).reshape(-1, 3)
    a = np.full((b.shape[0], 1), 255, dtype=np.uint8)
    rgba = np.concatenate([b, a], axis=1)
    return rgba.tobytes()


def _bgr48_to_rgb(src: bytes) -> bytes:
    # BGR48 is 16-bit per channel (little endian). Many CZIs store 12-bit data
    # packed into 16-bit (max around 4095/4096). Simply taking the high byte
    # makes such images appear too dark. We dynamically scale to 8-bit based on
    # observed max value (per tile).
    u16 = np.frombuffer(src, dtype="<u2")
    if u16.size % 3 != 0:
        raise ValueError("BGR48 buffer length is not a multiple of 6 bytes")
    pix = u16.reshape(-1, 3)  # B, G, R
    max_val = int(pix.max())
    if max_val <= 0:
        rgb8 = np.zeros((pix.shape[0], 3), dtype=np.uint8)
        return rgb8.tobytes()

    # Choose a denominator matching the apparent bit depth.
    # If max is an exact power of two (e.g. 4096), assume (2^n - 1) range (4095).
    pow2 = 1 << (max_val.bit_length() - 1)
    if max_val == pow2:
        denom = pow2 - 1
    else:
        denom = (1 << max_val.bit_length()) - 1
    denom = max(denom, 255)

    scaled = (pix.astype(np.uint32) * 255 + (denom // 2)) // denom  # uint32
    bgr8 = scaled.astype(np.uint8)
    rgb8 = bgr8[:, ::-1]
    return rgb8.tobytes()


def _unhilo_16bit(buf: bytes) -> bytes:
    # Undo OpenSlide HiLo packing: low bytes first half, high bytes second half.
    if len(buf) % 2:
        raise ValueError("Can't perform HiLo unpacking with odd byte count")
    half = len(buf) // 2
    lo = memoryview(buf)[:half]
    hi = memoryview(buf)[half:]
    out = bytearray(len(buf))
    out_mv = memoryview(out)
    out_mv[0::2] = lo
    out_mv[1::2] = hi
    return bytes(out)


def _zstd_decompress(payload: bytes, expected_size: int) -> bytes:
    dctx = zstd.ZstdDecompressor()
    out = dctx.decompress(payload, max_output_size=expected_size)
    if len(out) != expected_size:
        raise ValueError(f"Zstd decompressed size mismatch: got {len(out)}, expected {expected_size}")
    return out


def _decode_zstd(payload: bytes, *, expected_size: int, is_zstd1: bool) -> bytes:
    do_hilo = False
    if is_zstd1:
        if len(payload) < 1:
            raise ValueError("Image data length too small for zstd1 header")
        hdr_size = payload[0]
        if len(payload) < hdr_size:
            raise ValueError("Image data length too small for zstd1 header size")
        if hdr_size == 1:
            pass
        elif hdr_size == 3:
            _size, chunk_type, is_hi_low_pack = _ZSTD1_HDR_STRUCT.unpack_from(payload, 0)
            if chunk_type != 1:
                raise ValueError(f"Unexpected zstd chunk type: {chunk_type}")
            do_hilo = bool(is_hi_low_pack & 1)
        else:
            raise ValueError(f"Unexpected zstd header length: {hdr_size}")
        payload = payload[hdr_size:]

    out = _zstd_decompress(payload, expected_size)
    if do_hilo:
        out = _unhilo_16bit(out)
    return out


def decode_tile_pixels(
    *,
    payload: bytes,
    compression: int,
    pixel_type: int,
    w: int,
    h: int,
    output: Literal["rgba", "bgr", "rgb", "gray"] = "rgba",
) -> bytes:
    """Decode tile payload to requested output bytes.

    For output:
    - rgba: RGBA8888 (w*h*4)
    - rgb:  RGB888   (w*h*3)
    - bgr:  BGR888   (w*h*3)
    - gray: GRAY8    (w*h)
    """
    comp = Compression(compression)
    pt = PixelType(pixel_type)

    if w <= 0 or h <= 0:
        raise ValueError(f"Invalid tile size {w}x{h}")

    if pt == PixelType.BGR24:
        bytes_per_pixel = 3
    elif pt == PixelType.BGR48:
        bytes_per_pixel = 6
    elif pt == PixelType.GRAY8:
        bytes_per_pixel = 1
    elif pt == PixelType.GRAY16:
        bytes_per_pixel = 2
    else:
        raise NotImplementedError(f"Unsupported pixel type: {pt}")

    expected = w * h * bytes_per_pixel

    if comp == Compression.NONE:
        raw = payload
        if len(raw) != expected:
            raise ValueError(f"COMP_NONE payload size mismatch: got {len(raw)}, expected {expected}")
    elif comp == Compression.ZSTD0:
        raw = _decode_zstd(payload, expected_size=expected, is_zstd1=False)
    elif comp == Compression.ZSTD1:
        raw = _decode_zstd(payload, expected_size=expected, is_zstd1=True)
    elif comp == Compression.JXR:
        try:
            import imagecodecs  # type: ignore
        except Exception as e:  # pragma: no cover
            raise ImportError(
                "JPEG XR tile encountered but 'imagecodecs' is not installed. "
                "Install with: pip install 'czi-reader[jxr]'"
            ) from e
        arr = imagecodecs.jpegxr_decode(payload)
        if not isinstance(arr, np.ndarray):
            raise ValueError("imagecodecs.jpegxr_decode did not return a numpy array")
        # Normalize to expected shape/dtype. Many CZIs are 8-bit or 16-bit.
        if arr.ndim == 2:
            # grayscale
            if output != "gray":
                # promote to RGBA
                g = arr.astype(np.uint8, copy=False)
                if g.dtype == np.uint16:
                    g = (g >> 8).astype(np.uint8)
                rgb = np.repeat(g[..., None], 3, axis=2)
                a = np.full((*rgb.shape[:2], 1), 255, dtype=np.uint8)
                rgba = np.concatenate([rgb, a], axis=2)
                return rgba.tobytes()
            g = arr
            if g.dtype == np.uint16:
                g = (g >> 8).astype(np.uint8)
            return g.astype(np.uint8, copy=False).tobytes()
        if arr.ndim != 3:
            raise ValueError(f"Unexpected jpegxr decoded array ndim={arr.ndim}")
        # color
        if arr.shape[0] != h or arr.shape[1] != w:
            # imagecodecs should decode actual tile size; if not, still allow but validate
            raise ValueError(f"JPEG XR decoded size mismatch: got {arr.shape[1]}x{arr.shape[0]}, expected {w}x{h}")
        if arr.dtype == np.uint16:
            arr8 = (arr >> 8).astype(np.uint8)
        else:
            arr8 = arr.astype(np.uint8, copy=False)
        # NOTE: imagecodecs returns a numpy array but does not document channel
        # order here. Empirically, treating jpegxr_decode() output as RGB gives
        # results that match typical viewers (e.g., QuPath) better than assuming
        # BGR. If you need BGR, request output="bgr".
        rgb = arr8
        bgr = arr8[..., ::-1]

        if output == "rgb":
            return rgb.tobytes()
        if output == "bgr":
            return bgr.tobytes()
        if output == "rgba":
            a = np.full((h, w, 1), 255, dtype=np.uint8)
            rgba = np.concatenate([rgb, a], axis=2)
            return rgba.tobytes()
        if output == "gray":
            # luminosity
            r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
            gray = (0.299 * r + 0.587 * g + 0.114 * b).round().astype(np.uint8)
            return gray.tobytes()
        raise ValueError(f"Unsupported output: {output}")
    else:
        raise NotImplementedError(f"Unsupported compression: {comp}")

    # Convert raw pixel data to requested output
    if pt == PixelType.GRAY8:
        if output == "gray":
            return raw
        g = np.frombuffer(raw, dtype=np.uint8).reshape(h, w)
        rgb = np.repeat(g[..., None], 3, axis=2)
        if output == "rgb":
            return rgb.tobytes()
        if output == "bgr":
            return rgb[..., ::-1].tobytes()
        if output == "rgba":
            a = np.full((h, w, 1), 255, dtype=np.uint8)
            rgba = np.concatenate([rgb, a], axis=2)
            return rgba.tobytes()
        raise ValueError(f"Unsupported output: {output}")

    if pt == PixelType.GRAY16:
        g16 = np.frombuffer(raw, dtype="<u2").reshape(h, w)
        max_val = int(g16.max())
        if max_val <= 0:
            g8 = np.zeros((h, w), dtype=np.uint8)
        else:
            pow2 = 1 << (max_val.bit_length() - 1)
            if max_val == pow2:
                denom = pow2 - 1
            else:
                denom = (1 << max_val.bit_length()) - 1
            denom = max(denom, 255)
            g8 = ((g16.astype(np.uint32) * 255 + (denom // 2)) // denom).astype(np.uint8)
        if output == "gray":
            return g8.tobytes()
        rgb = np.repeat(g8[..., None], 3, axis=2)
        if output == "rgb":
            return rgb.tobytes()
        if output == "bgr":
            return rgb[..., ::-1].tobytes()
        if output == "rgba":
            a = np.full((h, w, 1), 255, dtype=np.uint8)
            rgba = np.concatenate([rgb, a], axis=2)
            return rgba.tobytes()
        raise ValueError(f"Unsupported output: {output}")

    if pt == PixelType.BGR24:
        if output == "bgr":
            return raw
        if output == "rgb":
            return _bgr24_to_rgb(raw)
        if output == "rgba":
            return _bgr24_to_rgba(raw)
        if output == "gray":
            rgb = np.frombuffer(_bgr24_to_rgb(raw), dtype=np.uint8).reshape(h, w, 3)
            r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
            gray = (0.299 * r + 0.587 * g + 0.114 * b).round().astype(np.uint8)
            return gray.tobytes()
        raise ValueError(f"Unsupported output: {output}")

    if pt == PixelType.BGR48:
        if output == "rgb":
            return _bgr48_to_rgb(raw)
        if output == "rgba":
            return _bgr48_to_rgba(raw)
        if output == "bgr":
            rgb8 = np.frombuffer(_bgr48_to_rgb(raw), dtype=np.uint8).reshape(-1, 3)
            bgr8 = rgb8[:, ::-1]
            return bgr8.tobytes()
        if output == "gray":
            rgb = np.frombuffer(_bgr48_to_rgb(raw), dtype=np.uint8).reshape(h, w, 3)
            r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
            gray = (0.299 * r + 0.587 * g + 0.114 * b).round().astype(np.uint8)
            return gray.tobytes()
        raise ValueError(f"Unsupported output: {output}")

    raise NotImplementedError(f"Unsupported pixel type conversion: {pt}")
