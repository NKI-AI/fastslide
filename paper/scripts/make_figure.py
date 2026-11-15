"""Figure for the paper: (a) H&E region from CMU-1.svs, (b) three channels of LuCa-7color_Scan1.qptiff."""
import sys
import numpy as np
from PIL import Image
from fastslide import FastSlide

svs, qptiff, out = sys.argv[1:4]
N = 800
with FastSlide.from_file_path(svs) as s:
    he = s.read_region((2600, 3300), 1, (N, N)).numpy()
    print("HE", s.format, "level 1", he.shape, he.dtype)
with FastSlide.from_file_path(qptiff) as s:
    names = [c["name"] for c in s.channel_metadata]
    lvl = 1
    W, H = s.level_dimensions[lvl]
    img = s.read_region((int(W * 0.45), int(H * 0.45)), lvl, (N, N)).to_separate().numpy().astype(np.float32)
    print("QPTIFF", names, "level", lvl, img.shape)
    pick = {"DAPI": 2, "FITC": 1, "CY5": 0}  # channel name -> RGB index
    rgb = np.zeros((N, N, 3), np.float32)
    for name, k in pick.items():
        ch = img[names.index(name)]
        rgb[..., k] = np.clip(ch / max(np.percentile(ch, 99.8), 1.0), 0, 1)
    mif = (rgb * 255).astype(np.uint8)
gap = np.full((N, 24, 3), 255, np.uint8)
Image.fromarray(np.concatenate([he, gap, mif], axis=1)).save(out, optimize=True)
