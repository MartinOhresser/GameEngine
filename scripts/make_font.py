#!/usr/bin/env python3
"""Draws a bitmap font: a PNG atlas with one letter per cell, and the atlas file for it.

    python3 scripts/make_font.py OUT_DIR NAME [PIXEL_SIZE]

writes OUT_DIR/NAME.png and OUT_DIR/NAME.json. The JSON lists the letters in
`glyphs`, in cell order (row by row), which is all the engine needs to draw text
with it. Letters are white on transparent, so a Text's red/green/blue tint them.

The letters come from DejaVu Sans Mono (free licence, see
https://dejavu-fonts.github.io/License.html), rendered by Pillow. Any monospace
TrueType font works: set FONT below. TrueType files are not read by the engine
itself yet; this script is how a font becomes an atlas.
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
COLUMNS = 16
GLYPHS = (
    "".join(chr(code) for code in range(32, 127))
    + "àâäçéèêëîïôöùûüÿœÀÉÈÊÇ«»€°…’"
)


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__)
        return 2
    out_dir, name = argv[1], argv[2]
    size = int(argv[3]) if len(argv) == 4 else 16
    font = ImageFont.truetype(FONT, size)
    ascent, descent = font.getmetrics()
    cell_w = int(round(font.getlength("M"))) + 1
    cell_h = ascent + descent + 1
    rows = (len(GLYPHS) + COLUMNS - 1) // COLUMNS
    image = Image.new("RGBA", (cell_w * COLUMNS, cell_h * rows), (0, 0, 0, 0))
    for index, glyph in enumerate(GLYPHS):
        layer = Image.new("L", (cell_w, cell_h), 0)
        ImageDraw.Draw(layer).text((0, 0), glyph, font=font, fill=255)
        # No anti-aliasing blur between letters: threshold to keep it crisp.
        layer = layer.point(lambda value: 255 if value > 110 else 0)
        white = Image.new("RGBA", (cell_w, cell_h), (255, 255, 255, 255))
        image.paste(white, ((index % COLUMNS) * cell_w, (index // COLUMNS) * cell_h), layer)
    os.makedirs(out_dir, exist_ok=True)
    image.save(os.path.join(out_dir, name + ".png"))
    atlas = {
        "schema_version": 1,
        "name": name,
        "image": name + ".png",
        "cell_width": cell_w,
        "cell_height": cell_h,
        "glyphs": GLYPHS,
    }
    with open(os.path.join(out_dir, name + ".json"), "w", encoding="utf-8") as out:
        json.dump(atlas, out, ensure_ascii=False, indent=2)
        out.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
