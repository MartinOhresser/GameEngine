#!/usr/bin/env python3
"""Writes small PNG atlases from a text description, with only the standard library.

Used to produce the committed test and example images, so they can be rebuilt
and reviewed: `scripts/make_png.py OUT.png CELL_W CELL_H COLUMNS ROWS RRGGBB[,RRGGBB...]`
paints each cell with one solid colour (row by row; a cell may be `-` for fully
transparent). The first pixel column of each cell is darkened so flips are visible.
"""
import struct
import sys
import zlib


def chunk(kind: bytes, data: bytes) -> bytes:
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path: str, width: int, height: int, rows: list) -> None:
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as out:
        out.write(data)


def main(argv: list) -> int:
    if len(argv) != 7:
        print(__doc__)
        return 2
    out, cw, ch, cols, rows_n, colors = argv[1], *map(int, argv[2:6]), argv[6].split(",")
    if len(colors) != cols * rows_n:
        print("need %d colours, got %d" % (cols * rows_n, len(colors)))
        return 2
    rows = []
    for y in range(rows_n * ch):
        row = []
        for x in range(cols * cw):
            color = colors[(y // ch) * cols + (x // cw)]
            if color == "-":
                row += [0, 0, 0, 0]
                continue
            r, g, b = (int(color[i : i + 2], 16) for i in (0, 2, 4))
            if x % cw == 0:  # darker left edge, to see mirroring
                r, g, b = r // 2, g // 2, b // 2
            row += [r, g, b, 255]
        rows.append(row)
    write_png(out, cols * cw, rows_n * ch, rows)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
