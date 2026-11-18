#!/usr/bin/env python3
"""Rebuilds the generated files of treasure_run: the two atlas pictures and the scene.

    python3 games/treasure_run/tools/make_assets.py

The pictures are drawn here, pixel by pixel, so the game ships no binary that
cannot be reviewed. The scene is written from the ASCII map below: every tile is
an entity with a Position and a Sprite (the engine has no tilemap type; tiles
are ordinary entities).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
from make_png import write_png  # noqa: E402

TILE = 32  # world units per tile; the pictures are 16x16 and drawn twice as big

# '#' wall  '.' floor  'P' player  'c' coin  'g' gem  '^' spikes
MAP = [
    "################################",
    "#..............................#",
    "#.P..c..c....#.................#",
    "#............#....g............#",
    "#............#.................#",
    "#............#.........c.......#",
    "#............#.................#",
    "#.^..........######.##########.#",
    "#..............................#",
    "#..............................#",
    "#..####.####........c..........#",
    "#..............................#",
    "#.........................g....#",
    "#.....c........................#",
    "#........................####..#",
    "#.....####.....................#",
    "#.............c................#",
    "#..............................#",
    "#.....#######.#######..........#",
    "#.............................c#",
    "#....g.........................#",
    "#..............................#",
    "#..............................#",
    "################################",
]


def rgba(hex_color, alpha=255):
    return [int(hex_color[i : i + 2], 16) for i in (0, 2, 4)] + [alpha]


CLEAR = [0, 0, 0, 0]


def blank(width, height):
    return [[CLEAR] * width for _ in range(height)]


def floor_tile(variant):
    tile = blank(16, 16)
    base = rgba("2b5e3a" if variant == 0 else "2e633e")
    for y in range(16):
        for x in range(16):
            tile[y][x] = base
    for x, y in ((3, 4), (11, 9), (6, 13)) if variant == 0 else ((12, 3), (4, 10), (9, 14)):
        tile[y][x] = rgba("3d7a4c")
    return tile


def wall_tile():
    tile = blank(16, 16)
    for y in range(16):
        for x in range(16):
            mortar = y % 8 == 0 or (x + (4 if (y // 8) % 2 else 0)) % 8 == 0
            tile[y][x] = rgba("4a3b52" if mortar else "7a6486")
    return tile


def spikes_tile():
    tile = floor_tile(0)
    for base_x in (1, 6, 11):
        for height in range(6):
            for x in range(base_x + (5 - height) // 3, base_x + 4 - (5 - height) // 3):
                tile[13 - height][x] = rgba("d8d8e0" if height > 1 else "9a9aa8")
    return tile


def disc(color, edge, radius):
    tile = blank(16, 16)
    for y in range(16):
        for x in range(16):
            distance = (x - 7.5) ** 2 + (y - 7.5) ** 2
            if distance <= radius * radius:
                tile[y][x] = rgba(edge if distance > (radius - 1.6) ** 2 else color)
    return tile


def gem_tile():
    tile = blank(16, 16)
    for y in range(16):
        for x in range(16):
            if abs(x - 7.5) + abs(y - 7.5) <= 6.5:
                inner = abs(x - 7.5) + abs(y - 7.5) <= 3.5
                tile[y][x] = rgba("bff4ff" if inner else "28b4d8")
    return tile


def hero_frame(bob):
    tile = blank(16, 16)
    top = 3 + bob
    for y in range(top, 14):
        for x in range(3, 13):
            corner = (x in (3, 12)) and (y in (top, 13))
            if not corner:
                tile[y][x] = rgba("e8a33d" if y < top + 8 else "c4802a")
    for x in (5, 10):  # eyes
        tile[top + 3][x] = rgba("1c1c28")
        tile[top + 4][x] = rgba("1c1c28")
    feet = 14 if bob == 0 else 13
    for x in (4, 5, 10, 11):
        tile[feet][x] = rgba("5a3a1a")
    return tile


def atlas(cells, columns):
    rows = (len(cells) + columns - 1) // columns
    pixels = blank(columns * 16, rows * 16)
    for index, cell in enumerate(cells):
        ox, oy = (index % columns) * 16, (index // columns) * 16
        for y in range(16):
            for x in range(16):
                pixels[oy + y][ox + x] = cell[y][x]
    return pixels


def save(path, pixels):
    height, width = len(pixels), len(pixels[0])
    write_png(path, width, height, [[c for pixel in row for c in pixel] for row in pixels])


def write_json(path, data):
    with open(path, "w") as out:
        json.dump(data, out, indent=2)
        out.write("\n")


def main():
    game = os.path.join(HERE, "..")
    # Cells of "tiles": 0 floor, 1 floor (variant), 2 wall, 3 spikes, 4 coin, 5 gem.
    save(os.path.join(game, "art", "tiles.png"),
         atlas([floor_tile(0), floor_tile(1), wall_tile(), spikes_tile(),
                disc("f2c230", "a87810", 6.5), gem_tile()], 3))
    # Cells of "hero": four frames of a bobbing walk.
    save(os.path.join(game, "art", "hero.png"),
         atlas([hero_frame(0), hero_frame(1), hero_frame(1), hero_frame(0)], 4))

    entities = []
    player = None
    for row, line in enumerate(MAP):
        assert len(line) == 32, (row, len(line))
        for col, char in enumerate(line):
            at = {"transform2d.Position": {"x": col * TILE + TILE // 2, "y": row * TILE + TILE // 2}}
            if char == "#":
                entities.append({"prefab": "wall", "components": at})
                continue
            entities.append({"prefab": "floor_b" if (col + row) % 2 else "floor_a", "components": at})
            if char == "^":
                entities.append({"prefab": "spikes", "components": at})
            elif char == "c":
                entities.append({"prefab": "coin", "components": at})
            elif char == "g":
                entities.append({"prefab": "gem", "components": at})
            elif char == "P":
                player = at
    assert player is not None
    entities.append({"name": "player", "prefab": "player", "components": player})
    entities.append({"name": "camera", "prefab": "camera", "components": player})
    # The interface: texts in window pixels (screen) that the camera never moves.
    # The words come from text/<language>.json; font and string are named there.
    entities.append({"name": "title", "components": {
        "transform2d.Position": {"x": 12, "y": 10},
        "text2d.Text": {"string": "title", "font": "font", "size": 20, "screen": True}}})
    entities.append({"name": "hint", "components": {
        "transform2d.Position": {"x": 12, "y": 34},
        "text2d.Text": {"string": "hint", "font": "font", "size": 14, "screen": True,
                        "red": 200, "green": 200, "blue": 200}}})
    write_json(os.path.join(game, "scenes", "main.json"),
               {"schema_version": 1, "name": "main", "entities": entities})
    print("wrote %d entities" % len(entities))


if __name__ == "__main__":
    main()
