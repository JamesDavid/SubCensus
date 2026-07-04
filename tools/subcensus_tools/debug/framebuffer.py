"""Flipper framebuffer decode + render (Debug §2.2).

The Gui screen-stream RPC pushes the 128x64 monochrome framebuffer as a 1024-byte 1-bit
buffer. Layout is u8g2/Flipper page-major: 8 vertical pages of `width` bytes; byte at
(page, x) holds 8 stacked pixels, LSB = topmost. Pixel (x, y):
    page = y // 8;  bit = y % 8;  index = page * width + x
    set  = (buf[index] >> bit) & 1

Decode -> PNG (agent can view natively) and/or ASCII (cheap "is the cursor on row 3?" text
asserts). Pure functions, unit-tested with no device (Debug §2.2). PNG uses stdlib zlib only
(no third-party deps) so the harness stays light.
"""

from __future__ import annotations

import struct
import zlib

WIDTH = 128
HEIGHT = 64
FRAME_BYTES = WIDTH * HEIGHT // 8  # 1024


def decode_framebuffer(buf: bytes, width: int = WIDTH, height: int = HEIGHT) -> list[list[int]]:
    """Decode a page-major 1-bit buffer into `height` rows of `width` ints (0/1)."""
    expected = width * height // 8
    if len(buf) < expected:
        raise ValueError(f"framebuffer too small: {len(buf)} < {expected}")
    rows = [[0] * width for _ in range(height)]
    for y in range(height):
        page = y // 8
        bit = y % 8
        base = page * width
        for x in range(width):
            rows[y][x] = (buf[base + x] >> bit) & 1
    return rows


def to_ascii(pixels: list[list[int]], on: str = "#", off: str = " ") -> str:
    """Render decoded pixels as ASCII (one char per pixel, one line per row)."""
    return "\n".join("".join(on if px else off for px in row) for row in pixels)


def to_ascii_halfblock(pixels: list[list[int]]) -> str:
    """Compact render: two vertical pixels per character using half-block glyphs.
    Halves the line count so a 64-row screen fits in 32 text lines."""
    glyph = {(0, 0): " ", (1, 0): "▀", (0, 1): "▄", (1, 1): "█"}
    height = len(pixels)
    width = len(pixels[0]) if pixels else 0
    lines = []
    for y in range(0, height, 2):
        top = pixels[y]
        bot = pixels[y + 1] if y + 1 < height else [0] * width
        lines.append("".join(glyph[(top[x], bot[x])] for x in range(width)))
    return "\n".join(lines)


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def to_png(pixels: list[list[int]], scale: int = 1) -> bytes:
    """Encode decoded pixels to an 8-bit grayscale PNG (0=black pixel-set, 255=white).
    `scale` nearest-neighbour upscales so the 128x64 screen is legible. stdlib only."""
    if scale < 1:
        scale = 1
    height = len(pixels)
    width = len(pixels[0]) if pixels else 0
    out_w, out_h = width * scale, height * scale

    raw = bytearray()
    for y in range(height):
        for _ in range(scale):
            raw.append(0)  # filter type 0 (None) per scanline
            for x in range(width):
                val = 0 if pixels[y][x] else 255  # set pixel -> black
                raw.extend(bytes([val]) * scale)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", out_w, out_h, 8, 0, 0, 0, 0)  # 8-bit grayscale
    idat = zlib.compress(bytes(raw), 9)
    return sig + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", idat) + _png_chunk(b"IEND", b"")


def cursor_row(pixels: list[list[int]], threshold: float = 0.5) -> int:
    """Heuristic for text asserts: index of the most-filled row (e.g. an inverted
    selection bar). Returns -1 if no row exceeds `threshold` fill."""
    best, best_fill = -1, threshold
    width = len(pixels[0]) if pixels else 0
    for y, row in enumerate(pixels):
        fill = sum(row) / width if width else 0
        if fill > best_fill:
            best, best_fill = y, fill
    return best
