#!/usr/bin/python3
"""Generates test images covering the pixel layouts enim must handle.

Usage: uv run tests/images/generate.py [output_dir]   (default: tests/images/out)

Every image shows the same pattern, so a wrong channel order, row order or stride is easy to spot:
- red grows left to right, green grows top to bottom;
- a blue square fills the top-left corner;
- alpha (when present) grows left to right, so the left edge is transparent.
"""

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def pattern(width: int, height: int) -> np.ndarray:
    """RGBA uint8 image, shape (height, width, 4), row 0 at the top."""
    x = np.linspace(0, 255, width, dtype=np.float32)[None, :]
    y = np.linspace(0, 255, height, dtype=np.float32)[:, None]
    rgba = np.zeros((height, width, 4), dtype=np.uint8)
    rgba[..., 0] = np.broadcast_to(x, (height, width))
    rgba[..., 1] = np.broadcast_to(y, (height, width))
    rgba[: height // 4, : width // 4, 2] = 255
    rgba[..., 3] = np.broadcast_to(x, (height, width))
    return rgba


def write_bmp(path: Path, pixels: np.ndarray, top_down: bool = False, masks: tuple[int, ...] | None = None) -> None:
    """Writes an uncompressed BMP from stored pixels (height, width, bytes per pixel), already in file channel order.

    masks: None for BI_RGB, else (R, G, B, A) masks written in a BITMAPV4HEADER with BI_BITFIELDS.
    """
    height, width, channels = pixels.shape
    bpp = channels * 8
    stride = (width * bpp + 31) // 32 * 4

    rows = np.zeros((height, stride), dtype=np.uint8)
    rows[:, : width * channels] = pixels.reshape(height, width * channels)
    if not top_down:
        rows = rows[::-1]
    data = rows.tobytes()

    signed_height = -height if top_down else height
    if masks is None:
        dib = struct.pack("<IiiHHIIiiII", 40, width, signed_height, 1, bpp, 0, len(data), 2835, 2835, 0, 0)
    else:
        dib = struct.pack("<IiiHHIIiiII", 108, width, signed_height, 1, bpp, 3, len(data), 2835, 2835, 0, 0)
        dib += struct.pack("<IIII", *masks) + b"BGRs" + bytes(48)  # color space, endpoints, gamma

    offset = 14 + len(dib)
    header = b"BM" + struct.pack("<IHHI", offset + len(data), 0, 0, offset)
    path.write_bytes(header + dib + data)


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "out"
    out.mkdir(parents=True, exist_ok=True)

    # odd width: 24-bit rows need padding to 4 bytes
    odd = pattern(301, 199)
    even = pattern(300, 200)
    bgr = lambda img: img[..., [2, 1, 0]]

    cases = {
        # 24-bit BGR, bottom-up, written by Pillow (a real encoder)
        "bmp_bgr24_bottomup_pillow.bmp": lambda p: Image.fromarray(odd[..., :3]).save(p),
        "bmp_bgr24_topdown.bmp": lambda p: write_bmp(p, bgr(odd), top_down=True),
        # 32-bit BI_RGB: BGRX, the 4th byte is unused
        "bmp_bgrx32_bottomup.bmp": lambda p: write_bmp(p, np.dstack([bgr(even), np.zeros_like(even[..., :1])])),
        # 32-bit BI_BITFIELDS: RGBA byte order with an alpha mask
        "bmp_rgba32_bitfields_topdown.bmp": lambda p: write_bmp(
            p, even, top_down=True, masks=(0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
        ),
        # 32-bit BI_BITFIELDS: BGRA byte order with an alpha mask (what most tools write)
        "bmp_bgra32_bitfields_bottomup.bmp": lambda p: write_bmp(
            p, even[..., [2, 1, 0, 3]], masks=(0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
        ),
        # 32-bit BI_BITFIELDS: ARGB byte order, alpha first: not GL-uploadable as is (needs GpuConvert)
        "bmp_argb32_bitfields_bottomup.bmp": lambda p: write_bmp(
            p, even[..., [3, 0, 1, 2]], masks=(0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF)
        ),
    }

    for name, write in cases.items():
        write(out / name)
        print(out / name)


if __name__ == "__main__":
    main()
