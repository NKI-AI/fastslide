#!/usr/bin/env python3
"""Generate a synthetic two-part DICOM WSI concatenation for tests.

Produces two minimal VL Whole Slide Microscopy Image files that together
describe a single pyramid level split via DICOM Part 10 concatenation
semantics (PS3.3 Section C.7.6.16.2.2.4):

    part_1.dcm    InConcatenationNumber=1, frames 0..3
    part_2.dcm    InConcatenationNumber=2, frames 4..7

The pyramid level is 4 tiles wide x 2 tiles tall (8 tiles total) at a
tile size of 16x16 px, giving a 64x32 total pixel matrix using
TILED_FULL layout (raster order). Each frame is a uniform colour so a
test can assert the right tile is delivered for the right (col, row).

The script is committed for reproducibility; the resulting .dcm files
are committed alongside it so tests do not require running Python.

Usage:
    python3 generate_concatenation.py [--output-dir DIR]
"""

from __future__ import annotations

import argparse
import pathlib

import numpy as np
import pydicom
from pydicom.dataset import Dataset, FileDataset
from pydicom.uid import ExplicitVRLittleEndian, generate_uid

# VL Whole Slide Microscopy Image Storage SOP Class UID (PS3.4 B.5.1.21).
WSI_SOP_CLASS_UID = "1.2.840.10008.5.1.4.1.1.77.1.6"

# Geometry of the synthetic pyramid level.
TILE_W = 16
TILE_H = 16
TILES_ACROSS = 4
TILES_DOWN = 2
TOTAL_FRAMES = TILES_ACROSS * TILES_DOWN  # 8

# Concatenation split: 4 frames in part 1, 4 frames in part 2.
PART_FRAME_COUNTS = (4, 4)


def _make_pixel_payload(start_frame: int, num_frames: int) -> bytes:
    """Return uncompressed RGB pixel data for `num_frames` consecutive frames.

    Each frame is a uniform colour derived from its global frame index so
    tests can assert the correct part returns the correct tile.
    """
    pixels = np.empty((num_frames, TILE_H, TILE_W, 3), dtype=np.uint8)
    for local_idx in range(num_frames):
        global_idx = start_frame + local_idx
        # Encode the frame index in three channels with a wide spread so
        # adjacent frames are visually distinct.
        red = (global_idx * 31) & 0xFF
        green = (global_idx * 61) & 0xFF
        blue = (global_idx * 97) & 0xFF
        pixels[local_idx, ...] = (red, green, blue)
    return pixels.tobytes()


def _build_part(
    *,
    output_path: pathlib.Path,
    series_uid: str,
    study_uid: str,
    concatenation_uid: str,
    in_concatenation_number: int,
    frame_offset: int,
    num_frames: int,
) -> None:
    """Write one concatenation part as a DICOM Part 10 file."""
    file_meta = Dataset()
    file_meta.MediaStorageSOPClassUID = WSI_SOP_CLASS_UID
    file_meta.MediaStorageSOPInstanceUID = generate_uid()
    file_meta.TransferSyntaxUID = ExplicitVRLittleEndian
    file_meta.ImplementationClassUID = generate_uid()

    ds = FileDataset(str(output_path), {}, file_meta=file_meta, preamble=b"\0" * 128)

    # SOP / patient / study identifiers (minimal but valid).
    ds.SOPClassUID = WSI_SOP_CLASS_UID
    ds.SOPInstanceUID = file_meta.MediaStorageSOPInstanceUID
    ds.StudyInstanceUID = study_uid
    ds.SeriesInstanceUID = series_uid
    ds.PatientID = "FASTSLIDE-TEST"
    ds.PatientName = "Synthetic^Concatenation"
    ds.Modality = "SM"  # Slide Microscopy.

    # Image classification: ORIGINAL/PRIMARY/VOLUME marks this as a
    # pyramid level (PS3.3 C.7.6.1).
    ds.ImageType = ["ORIGINAL", "PRIMARY", "VOLUME", "NONE"]

    # Pixel format: 8-bit unsigned RGB, interleaved.
    ds.SamplesPerPixel = 3
    ds.PhotometricInterpretation = "RGB"
    ds.BitsAllocated = 8
    ds.BitsStored = 8
    ds.HighBit = 7
    ds.PixelRepresentation = 0
    ds.PlanarConfiguration = 0

    # Frame and tile geometry.
    ds.NumberOfFrames = num_frames
    ds.Rows = TILE_H
    ds.Columns = TILE_W
    ds.TotalPixelMatrixColumns = TILES_ACROSS * TILE_W
    ds.TotalPixelMatrixRows = TILES_DOWN * TILE_H
    ds.TotalPixelMatrixFocalPlanes = 1
    ds.DimensionOrganizationType = "TILED_FULL"

    # Concatenation tags (PS3.3 C.7.6.16.2.2.4).
    ds.ConcatenationUID = concatenation_uid
    ds.InConcatenationNumber = in_concatenation_number
    ds.ConcatenationFrameOffsetNumber = frame_offset

    # Pixel data.
    ds.PixelData = _make_pixel_payload(frame_offset, num_frames)

    ds.is_little_endian = True
    ds.is_implicit_VR = False
    ds.save_as(output_path, enforce_file_format=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        default=pathlib.Path(__file__).parent,
        help="Directory to write part_1.dcm and part_2.dcm into.",
    )
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    # Stable UIDs would be friendlier for diffing but DICOM requires UIDs to
    # be globally unique; tests must instead query the files at runtime.
    series_uid = generate_uid()
    study_uid = generate_uid()
    concatenation_uid = generate_uid()

    frame_cursor = 0
    for idx, frame_count in enumerate(PART_FRAME_COUNTS, start=1):
        out = args.output_dir / f"part_{idx}.dcm"
        _build_part(
            output_path=out,
            series_uid=series_uid,
            study_uid=study_uid,
            concatenation_uid=concatenation_uid,
            in_concatenation_number=idx,
            frame_offset=frame_cursor,
            num_frames=frame_count,
        )
        print(f"wrote {out} (frames {frame_cursor}..{frame_cursor + frame_count - 1})")
        frame_cursor += frame_count


if __name__ == "__main__":
    main()
