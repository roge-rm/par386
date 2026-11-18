#!/usr/bin/env python3
"""Writes the app icon bitmaps (golf flag on a hill) to res/ as BMP files.
Usage: mkicon.py RESDIR"""
import struct
import sys
from pathlib import Path

W, H = 32, 22
SW, SH = 15, 9


def big():
    """0 = white, 1 = light, 2 = dark, 3 = black."""
    g = [[0] * W for _ in range(H)]
    for y in range(3, 18):                      # pole
        g[y][15] = 3
    for y in range(3, 9):                       # flag, a triangle
        for x in range(16, 16 + (9 - y) * 2 + 1 if y > 5 else 16 + (y - 2) * 2):
            g[y][x] = 3 if y in (3, 8) else 2
    for x in range(4, 28):                      # hill
        top = 18 + (abs(x - 15) // 6)
        for y in range(top, 21):
            g[y][x] = 1 if y > top else 3
    for y in range(19, 21):                     # ball
        g[y][20] = 3
    return g


def small():
    g = [[0] * SW for _ in range(SH)]
    for y in range(1, 7):
        g[y][7] = 3
    for y in range(1, 4):
        for x in range(8, 11 - (y - 1)):
            g[y][x] = 3
    for x in range(2, 13):
        g[7][x] = 3
    return g


def bmp24(g, path):
    """pilrc wants 2-bit art as 24-bit grey BMP."""
    h, w = len(g), len(g[0])
    rowbytes = (w * 3 + 3) // 4 * 4
    data = bytearray()
    for row in reversed(g):
        out = bytearray(rowbytes)
        for x, v in enumerate(row):
            out[x * 3:x * 3 + 3] = bytes([255 - 85 * v] * 3)
        data += out
    hdr = b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    Path(path).write_bytes(hdr + info + bytes(data))


def bmp(g, bits, path):
    if bits == 2:
        return bmp24(g, path)
    h, w = len(g), len(g[0])
    maxv = (1 << bits) - 1
    rowbytes = ((w * bits + 31) // 32) * 4
    pal = []
    for i in range(maxv + 1):
        v = 255 - 255 * i // maxv
        pal.append(bytes([v, v, v, 0]))
    # Greys map: levels 0..3 white, light, dark, black. For 1 bit, 1 = black.
    data = bytearray()
    for row in reversed(g):
        out = bytearray(rowbytes)
        for x, v in enumerate(row):
            if bits == 1:
                v = 1 if v >= 2 else 0
            pos = x * bits
            out[pos // 8] |= v << (8 - bits - pos % 8)
        data += out
    off = 14 + 40 + 4 * len(pal)
    hdr = b'BM' + struct.pack('<IHHI', off + len(data), 0, 0, off)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, bits, 0, len(data), 2835, 2835, len(pal), 0)
    Path(path).write_bytes(hdr + info + b''.join(pal) + bytes(data))


res = Path(sys.argv[1])
res.mkdir(parents=True, exist_ok=True)
bmp(big(), 1, res / 'icon-1.bmp')
bmp(big(), 2, res / 'icon-2.bmp')
bmp(small(), 1, res / 'small-1.bmp')
bmp(small(), 2, res / 'small-2.bmp')
