#!/usr/bin/python3
"""Generates test images covering the pixel layouts enim must handle.

Usage: uv run tests/images/generate.py [--force] [--large] [output_dir]   (default: tests/images/out)

Existing files are kept unless --force is given (the pure Python QOI encoder is slow on large images).
--large also writes images too large for the default fast mode budget (~1.2 GB on disk), which load in normal mode.

Every image shows the same pattern, so a wrong channel order, row order or stride is easy to spot:
- red grows left to right, green grows top to bottom;
- a blue square fills the top-left corner;
- alpha (when present) grows left to right, so the left edge is transparent.
"""

import io
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


def photo_like(width: int, height: int) -> np.ndarray:
    """RGB uint8 image with smooth gradients, flat areas and noise, so that every QOI op is used."""
    rng = np.random.default_rng(0)
    rgba = pattern(width, height)
    rgb = rgba[..., :3].astype(np.int16)
    noisy = rng.integers(-6, 7, size=rgb.shape, dtype=np.int16)
    rgb[:, width // 2 :] += noisy[:, width // 2 :]
    rgb[height // 2 :, : width // 4] = (40, 90, 160)  # flat block: runs
    return np.clip(rgb, 0, 255).astype(np.uint8)


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


def encode_qoi(pixels: np.ndarray) -> bytes:
    """Encodes an RGB or RGBA uint8 image (height, width, 3 or 4) as QOI (qoiformat.org), top row first."""
    height, width, channels = pixels.shape
    out = bytearray(b"qoif" + struct.pack(">IIBB", width, height, channels, 0))
    flat = pixels.reshape(-1, channels)
    if channels == 3:
        flat = np.hstack([flat, np.full((flat.shape[0], 1), 255, dtype=np.uint8)])
    index = [(0, 0, 0, 0)] * 64
    prev = (0, 0, 0, 255)
    run = 0
    for r, g, b, a in map(tuple, flat.tolist()):
        px = (r, g, b, a)
        if px == prev:
            run += 1
            if run == 62:
                out.append(0xC0 | (run - 1))
                run = 0
            continue
        if run:
            out.append(0xC0 | (run - 1))
            run = 0
        h = (r * 3 + g * 5 + b * 7 + a * 11) % 64
        if index[h] == px:
            out.append(h)
        else:
            index[h] = px
            if a != prev[3]:
                out += bytes((0xFF, r, g, b, a))
            else:
                dr = (r - prev[0] + 128) % 256 - 128
                dg = (g - prev[1] + 128) % 256 - 128
                db = (b - prev[2] + 128) % 256 - 128
                if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                    out.append(0x40 | (dr + 2) << 4 | (dg + 2) << 2 | (db + 2))
                elif -32 <= dg <= 31 and -8 <= dr - dg <= 7 and -8 <= db - dg <= 7:
                    out += bytes((0x80 | (dg + 32), (dr - dg + 8) << 4 | (db - dg + 8)))
                else:
                    out += bytes((0xFE, r, g, b))
        prev = px
    if run:
        out.append(0xC0 | (run - 1))
    return bytes(out + bytes(7) + b"")


def jpeg_bytes(pixels: np.ndarray, progressive: bool) -> bytes:
    buffer = io.BytesIO()
    Image.fromarray(pixels).save(buffer, format="JPEG", quality=90, progressive=progressive)
    return buffer.getvalue()


def main() -> None:
    args = sys.argv[1:]
    force = "--force" in args
    large = "--large" in args
    args = [a for a in args if a not in ("--force", "--large")]
    out = Path(args[0]) if args else Path(__file__).parent / "out"
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
        # large ARGB (~144 MB): GpuConvert needs several bands of the 64 MB staging buffer
        "bmp_argb32_bitfields_large.bmp": lambda p: write_bmp(
            p, pattern(6000, 6000)[..., [3, 0, 1, 2]], masks=(0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF)
        ),
        # large DirectUpload images: mipmaps take several steps (RGB8 and RGBA8 textures)
        "bmp_bgr24_large.bmp": lambda p: write_bmp(p, bgr(pattern(6000, 6000))),
        "bmp_bgra32_bitfields_large.bmp": lambda p: write_bmp(
            p, pattern(6000, 6000)[..., [2, 1, 0, 3]], masks=(0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
        ),
        # QOI (CPU decoded, StreamUpload): same patterns as bmp_bgr24_topdown and bmp_rgba32_bitfields_topdown
        "qoi_rgb_odd.qoi": lambda p: p.write_bytes(encode_qoi(odd[..., :3])),
        "qoi_rgba.qoi": lambda p: p.write_bytes(encode_qoi(even)),
        # a photo-like image, several bands of rows
        "qoi_rgb_large.qoi": lambda p: p.write_bytes(encode_qoi(photo_like(3000, 2000))),
        # cut in half: the decoder must report an error and keep the rows it decoded
        "qoi_rgb_truncated.qoi": lambda p: p.write_bytes((lambda q: q[: len(q) // 2])(encode_qoi(odd[..., :3]))),
        # JPEG (libjpeg-turbo, StreamUpload): odd sizes are not a multiple of the MCU size
        "jpeg_rgb_420_odd.jpg": lambda p: Image.fromarray(odd[..., :3]).save(p, quality=90, subsampling="4:2:0"),
        "jpeg_rgb_444.jpg": lambda p: Image.fromarray(even[..., :3]).save(p, quality=90, subsampling="4:4:4"),
        "jpeg_gray.jpg": lambda p: Image.fromarray(odd[..., :3]).convert("L").save(p, quality=90),
        "jpeg_rgb_progressive.jpg": lambda p: Image.fromarray(odd[..., :3]).save(p, quality=90, progressive=True),
        "jpeg_rgb_large.jpg": lambda p: Image.fromarray(photo_like(6000, 4000)).save(p, quality=90),
        # cut in half: baseline keeps the rows decoded before the error, progressive shows the whole image with
        # less detail; both report an error
        "jpeg_rgb_truncated.jpg": lambda p: p.write_bytes(
            (lambda q: q[: len(q) // 2])(jpeg_bytes(photo_like(3000, 2000), progressive=False))
        ),
        "jpeg_rgb_progressive_truncated.jpg": lambda p: p.write_bytes(
            (lambda q: q[: len(q) // 2])(jpeg_bytes(photo_like(3000, 2000), progressive=True))
        ),
        # not supported yet: must fail in open() with a clear message
        "jpeg_cmyk.jpg": lambda p: Image.fromarray(even[..., :3]).convert("CMYK").save(p, quality=90),
    }

    # --large: fast mode would need ~1.5 GiB, over the default budget, so they load in normal mode (odd sizes: pyramid edges)
    if large:
        cases |= {
            # ~1.1 GB, bottom-up: the pyramid builder gets the rows bottom row first
            "bmp_bgr24_huge.bmp": lambda p: write_bmp(p, bgr(pattern(24001, 16001)[..., :3])),
            # decoded to disk (DecodeToDisk)
            "jpeg_rgb_huge.jpg": lambda p: Image.fromarray(pattern(24001, 16001)[..., :3]).save(p, quality=90),
        }

    for name, write in cases.items():
        path = out / name
        if force or not path.exists():
            write(path)
        print(path)


if __name__ == "__main__":
    main()
