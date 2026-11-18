#!/usr/bin/env python3
"""Rebuilds the generated files of treasure_run: the atlas pictures and the three scenes.

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
# 'k' key  's' sword  'p' potion (items the hero can pick up into the hotbar)
MAP = [
    "################################",
    "#..............................#",
    "#.P..c..c....#.................#",
    "#............#....g............#",
    "#.p..k.......#.................#",
    "#.p..s.......#.........c.......#",
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


def solid(color, alpha=255):
    return [[rgba(color, alpha)] * 16 for _ in range(16)]


def slot_tile():
    tile = solid("1c1c2a", 210)
    for i in range(16):
        for edge in (0, 15):
            tile[edge][i] = rgba("8a8aa8")
            tile[i][edge] = rgba("8a8aa8")
    return tile


def frame_tile(color, thickness):
    tile = blank(16, 16)
    for y in range(16):
        for x in range(16):
            if min(x, y, 15 - x, 15 - y) < thickness:
                tile[y][x] = rgba(color)
    return tile


def key_tile():
    tile = blank(16, 16)
    for y in range(2, 8):  # the bow: a ring
        for x in range(2, 8):
            d = (x - 4.5) ** 2 + (y - 4.5) ** 2
            if 3 <= d <= 7.5:
                tile[y][x] = rgba("f2c230")
    for x in range(7, 14):  # the shaft
        tile[5][x] = rgba("f2c230")
        tile[6][x] = rgba("a87810")
    for x, y in ((11, 7), (11, 8), (13, 7), (13, 8), (13, 9)):  # the teeth
        tile[y][x] = rgba("f2c230")
    return tile


def sword_tile():
    tile = blank(16, 16)
    for i in range(2, 12):  # the blade, along the diagonal
        tile[13 - i][i + 1] = rgba("dfe6ee")
        tile[13 - i][i] = rgba("9aa6b4")
    for i in range(-2, 3):  # the guard
        tile[10 - i][3 + i] = rgba("a87810")
    for i in range(0, 3):  # the grip
        tile[12 + i // 2][2 - i // 2] = rgba("5a3a1a")
    return tile


def potion_tile():
    tile = blank(16, 16)
    for y in range(7, 14):  # the flask
        for x in range(3, 13):
            if (x - 7.5) ** 2 + (y - 10.5) ** 2 <= 17:
                tile[y][x] = rgba("d83a4a" if y > 9 else "f0707c")
    for y in range(3, 8):  # the neck
        for x in (6, 7, 8, 9):
            tile[y][x] = rgba("d9e9f2")
    for x in (6, 7, 8, 9):  # the cork
        tile[2][x] = rgba("8a5a2a")
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


def text(string, size, x, y, anchor_x=0.0, anchor_y=0.0, pivot_x=0.0, pivot_y=0.0, **extra):
    """A text in window pixels, anchored to the window: it follows its size."""
    components = {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": anchor_x, "anchor_y": anchor_y, "offset_x": x, "offset_y": y},
        "text2d.Text": dict({"string": string, "font": "font", "size": size, "screen": True,
                             "pivot_x": pivot_x, "pivot_y": pivot_y}, **extra),
    }
    return components


def bar_pair(name, x, y, max_value):
    """A dark bar with a coloured one over it that fills with the player's score."""
    back = {"transform2d.Position": {}, "ui.Anchor": {"offset_x": x, "offset_y": y},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "screen": True, "layer": 11,
                                "width": 200, "height": 10, "pivot_x": 0,
                                "red": 40, "green": 40, "blue": 60}}
    front = {"transform2d.Position": {}, "ui.Anchor": {"offset_x": x, "offset_y": y},
             "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "screen": True, "layer": 12,
                                 "pivot_x": 0, "red": 90, "green": 220, "blue": 110},
             "ui.Bar": {"max": max_value, "length": 200, "thickness": 10},
             "ui.Bind": {"source": "player", "field": "pickup.Collector.score"}}
    return [{"name": name + "_back", "components": back}, {"name": name, "components": front}]


def hud():
    """Score, a progress bar and the hotbar: every number and every rule is data."""
    out = [
        {"name": "title", "components": text("title", 20, 12, 10)},
        {"name": "hint", "components": text("hint", 12, 12, 34, red=200, green=200, blue=200)},
        {"name": "score", "components": dict(text("hud.score", 18, 12, 56), **{
            "ui.Bind": {"source": "player", "field": "pickup.Collector.score"}})},
    ]
    out += bar_pair("progress", 12, 86, 140)
    # The hotbar: five slots in a row at the bottom of the window. Slot 0 only holds
    # tools, slot 1 only weapons; the others hold anything, and slot 4 at most 2 of a kind.
    out.append({"name": "hotbar", "components": {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 1, "offset_x": -88, "offset_y": -34},
        "ui.Grid": {"group": "hotbar", "columns": 5, "cell_width": 40, "cell_height": 40,
                    "spacing_x": 4}}})
    rules = [{"accepts": ["tool"]}, {"accepts": ["weapon"]}, {}, {}, {"max_stack": 2}]
    for index, rule in enumerate(rules):
        slot = {"inventory.Slot": dict({"index": index}, **rule), "ui.Cell": {"index": index}}
        out.append({"name": "slot_%d" % index, "prefab": "slot", "components": slot})
    out.append({"name": "hand", "components": {
        "transform2d.Position": {},
        "sprite2d.Sprite": {"atlas": "ui", "frame": 2, "width": 44, "height": 44,
                            "screen": True, "layer": 30},
        "inventory.Hand": {"container": "backpack", "columns": 5, "left": "slot_prev",
                           "right": "slot_next", "select": "slot_select", "cancel": "slot_cancel",
                           "use_pointer": True, "pointer_x": "pointer_x", "pointer_y": "pointer_y",
                           "click": "click", "slot_width": 40, "slot_height": 40,
                           "cursor_frame": 2, "holding_frame": 3}}})
    out.append({"name": "hotbar_label", "components": text(
        "hud.items", 12, 0, -62, anchor_x=0.5, anchor_y=1, pivot_x=0.5)})
    return out


def menu_scene():
    buttons = [("play", "menu.play", "start_game"), ("credits", "menu.credits", "show_credits")]
    out = [
        {"name": "menu_title", "components": text("menu.title", 44, 0, -190, 0.5, 0.5, 0.5, 0.5)},
        {"name": "menu_input", "components": {"ui.Menu": {
            "group": "main_menu", "up": "menu_up", "down": "menu_down", "accept": "menu_accept",
            "use_pointer": True, "pointer_x": "pointer_x", "pointer_y": "pointer_y",
            "click": "click"}}},
        {"name": "menu_list", "components": {
            "transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5, "offset_y": -10},
            "ui.Grid": {"group": "menu_buttons", "columns": 1, "cell_width": 240,
                        "cell_height": 48, "spacing_y": 14}}},
    ]
    for index, (name, label, action) in enumerate(buttons):
        out.append({"name": "button_" + name, "prefab": "button", "components": {
            "ui.Cell": {"group": "menu_buttons", "index": index},
            "ui.Button": {"order": index, "action": action},
            "text2d.Text": {"string": label}}})
    out.append({"name": "go_play", "components": {"scene_switch.GoToScene": {
        "scene": "main", "use_action": True, "action": "start_game"}}})
    out.append({"name": "go_credits", "components": {"scene_switch.GoToScene": {
        "scene": "credits", "use_action": True, "action": "show_credits"}}})
    return out


def credits_scene():
    return [
        {"name": "credits_title", "components": text("credits.title", 44, 0, -120, 0.5, 0.5, 0.5, 0.5)},
        {"name": "credits_one", "components": text("credits.line1", 20, 0, -20, 0.5, 0.5, 0.5, 0.5)},
        {"name": "credits_two", "components": text("credits.line2", 16, 0, 30, 0.5, 0.5, 0.5, 0.5,
                                                    red=200, green=200, blue=200)},
        # Back to the menu with Enter, or by itself after six seconds.
        {"name": "back", "components": {"scene_switch.GoToScene": {
            "scene": "menu", "use_action": True, "action": "menu_accept",
            "after_ticks": 360}}},
    ]


def main():
    game = os.path.join(HERE, "..")
    # Cells of "tiles": 0 floor, 1 floor (variant), 2 wall, 3 spikes, 4 coin, 5 gem.
    save(os.path.join(game, "art", "tiles.png"),
         atlas([floor_tile(0), floor_tile(1), wall_tile(), spikes_tile(),
                disc("f2c230", "a87810", 6.5), gem_tile()], 3))
    # Cells of "hero": four frames of a bobbing walk.
    save(os.path.join(game, "art", "hero.png"),
         atlas([hero_frame(0), hero_frame(1), hero_frame(1), hero_frame(0)], 4))

    # Cells of "ui": 0 white, 1 slot, 2 cursor, 3 cursor holding, 4 button, 5 button
    # with the focus, 6 key, 7 sword, 8 potion.
    save(os.path.join(game, "art", "ui.png"),
         atlas([solid("ffffff"), slot_tile(), frame_tile("f2c230", 1), frame_tile("5ad8f0", 2),
                solid("2a3a6a"), solid("d98a2a"), key_tile(), sword_tile(), potion_tile()], 3))

    entities = []
    count = {}
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
            elif char in "ksp":
                kind = {"k": "key", "s": "sword", "p": "potion"}[char]
                count[kind] = count.get(kind, 0) + 1
                entities.append({"name": "%s_%d" % (kind, count[kind]), "prefab": kind,
                                 "components": at})
    assert player is not None
    entities.append({"name": "player", "prefab": "player", "components": player})
    entities.append({"name": "camera", "prefab": "camera", "components": player})
    entities += hud()
    write_json(os.path.join(game, "scenes", "main.json"),
               {"schema_version": 1, "name": "main", "entities": entities})
    write_json(os.path.join(game, "scenes", "menu.json"),
               {"schema_version": 1, "name": "menu", "entities": menu_scene()})
    write_json(os.path.join(game, "scenes", "credits.json"),
               {"schema_version": 1, "name": "credits", "entities": credits_scene()})
    print("wrote %d entities" % len(entities))


if __name__ == "__main__":
    main()
