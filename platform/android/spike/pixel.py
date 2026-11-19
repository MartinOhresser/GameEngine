#!/usr/bin/env python3
"""Reads one pixel of a PNG (8 bits, RGB or RGBA, not interlaced: what `adb screencap` writes).

    pixel.py shot.png x y     prints "r g b"   (x, y as fractions of the size: 0.5 0.5 is the centre)
    pixel.py shot.png size    prints "width height"
    pixel.py shot.png colours prints how many distinct colours a grid of 40x40 points shows
"""
import struct
import sys
import zlib


def decode(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, header = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    width, height, depth, color, _, _, interlace = header
    assert depth == 8 and interlace == 0 and color in (2, 6), f"unsupported PNG {header}"
    step = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * step
    rows, previous = [], bytearray(stride)
    for y in range(height):
        base = y * (stride + 1)
        kind, line = raw[base], bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            a = line[i - step] if i >= step else 0
            b = previous[i]
            c = previous[i - step] if i >= step else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        previous = line
    return width, height, step, rows


if __name__ == "__main__":
    w, h, step, rows = decode(sys.argv[1])
    if sys.argv[2] == "size":
        print(w, h)
    elif sys.argv[2] == "colours":
        seen = {bytes(rows[y * h // 40][x * w // 40 * step:x * w // 40 * step + 3])
                for y in range(40) for x in range(40)}
        print(len(seen))
    else:
        x = min(w - 1, int(float(sys.argv[2]) * w))
        y = min(h - 1, int(float(sys.argv[3]) * h))
        print(*rows[y][x * step:x * step + 3])
