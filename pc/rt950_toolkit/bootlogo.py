"""
bootlogo.py - Boot picture helpers (240 x 320)

* to_rgb() loads PNG/GIF/PPM with Tk only, any format with Pillow, and
  fits/crops it to 240 x 320.
* write_bmp() writes the 24-bit BMP that RT-950/950Pro Editor and the OEM
  CPS accept for "Boot Picture" (stock firmware path).
* rgb565() gives the raw big-endian RGB565 image stored at SPI 0x090000,
  uploaded by upload_custom() on radios with the custom firmware.
"""

from __future__ import annotations

import struct
from typing import List, Tuple

W, H = 240, 320
SPLASH_ADDR = 0x090000

RGB = List[Tuple[int, int, int]]


def to_rgb(path: str) -> RGB:
    try:
        from PIL import Image, ImageOps
        im = Image.open(path).convert("RGB")
        im = ImageOps.fit(im, (W, H))
        return list(im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata())
    except ImportError:
        import tkinter as tk
        root = tk._default_root or tk.Tk()
        img = tk.PhotoImage(file=path)
        sw, sh = img.width(), img.height()
        out = []
        for y in range(H):
            for x in range(W):
                r, g, b = img.get(min(sw - 1, x * sw // W), min(sh - 1, y * sh // H))
                out.append((r, g, b))
        return out


def rgb565(px: RGB) -> bytes:
    out = bytearray()
    for r, g, b in px:
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += struct.pack(">H", v)
    return bytes(out)


def write_bmp(path: str, px: RGB):
    row = W * 3
    pad = (4 - row % 4) % 4
    size = 54 + (row + pad) * H
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", size, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, (row + pad) * H, 2835, 2835, 0, 0))
        for y in range(H - 1, -1, -1):
            for x in range(W):
                r, g, b = px[y * W + x]
                f.write(bytes((b, g, r)))
            f.write(b"\0" * pad)


def upload_custom(link, px: RGB, progress=None):
    """Custom firmware only: erase + write the 153,600-byte image."""
    data = rgb565(px)
    for sec in range(0, len(data), 4096):
        link.erase_sector(SPLASH_ADDR + sec)
        for o in range(sec, min(sec + 4096, len(data)), 128):
            link.write_block(SPLASH_ADDR + o, data[o:o + 128])
        if progress:
            progress(min(sec + 4096, len(data)), len(data), "logo")
