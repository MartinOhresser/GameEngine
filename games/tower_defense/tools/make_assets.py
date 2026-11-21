#!/usr/bin/env python3
"""Rebuilds the generated files of tower_defense: the pictures, the prefabs, the scenes, the
scenarios and the manifest.

    python3 games/tower_defense/tools/make_assets.py

The towers, the monsters and the shots are drawn here, pixel by pixel, from a few shapes (no
binary that cannot be reviewed); the font is the one of doodle_jump (scripts/make_font.py).
The game itself is not here: a tower is a prefab with an AutoFire and the prices of its
improvement and of its sale, a monster a prefab with a Health, a Bounty and a PathFollower, the
waves are entities (a WaveGroup each), the road is entities (a Waypoint each), what is bought
is chosen by buttons that set a number of the Placer. This script only writes them down.

The test benches (scenes/test_*.json) are the real game with few monsters, so that each rule
is checked on a situation that is easy to read.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
from make_png import write_png  # noqa: E402

GAME = os.path.join(HERE, "..")

# --- the board -----------------------------------------------------------------------------

CELL = 48  # the size of a cell, in world units
COLS, ROWS = 14, 9
WIDTH, HEIGHT = 800, 480  # the window, and what the camera makes of it (see CameraFit)
AREA_W, AREA_H = COLS * CELL + 16, ROWS * CELL + 16
MARGIN_TOP, MARGIN_BOTTOM = 36, 64
ZOOM = min(WIDTH / AREA_W, (HEIGHT - MARGIN_TOP - MARGIN_BOTTOM) / AREA_H)
MIDDLE_Y = (MARGIN_TOP + HEIGHT - MARGIN_BOTTOM) / 2

# The road: the cells where its direction changes, from the entrance to the base.
ROAD = [(0, 1), (11, 1), (11, 4), (2, 4), (2, 7), (13, 7)]
ROCKS = [(0, 3), (7, 0), (9, 2), (6, 6), (11, 6), (13, 3)]

# The phases of the game (the list phase), and their bits in the sets of phase.ShowIn.
PLAYING, TOWER, TOWER_FINAL, WON, LOST = 1, 2, 4, 8, 16
PHASES = ["playing", "tower", "tower_final", "won", "lost"]
ALIVE = PLAYING | TOWER | TOWER_FINAL  # the phases in which the game goes on
INDEX = {"playing": 0, "tower": 1, "tower_final": 2, "won": 3, "lost": 4}

START_GOLD, START_LIVES = 150, 20


# The bottom bar, in pixels from the middle of the window: it must fit the narrowest shape the
# game takes (600 wide, a 5:4 screen), so that nothing is cut off on a phone held upright.
SHOP_X, SHOP_STEP = -250, 92
UPGRADE_X, SELL_X, NEXT_X = -200, -60, 230


def centre(col, row):
    return (col - (COLS - 1) / 2) * CELL, (row - (ROWS - 1) / 2) * CELL


def pixel_of(col, row):
    """Where a touch on a cell is, in pixels of the window."""
    x, y = centre(col, row)
    return round(WIDTH / 2 + x * ZOOM), round(MIDDLE_Y + y * ZOOM)


def road_cells():
    cells = []
    for (c0, r0), (c1, r1) in zip(ROAD, ROAD[1:]):
        step_c = (c1 > c0) - (c1 < c0)
        step_r = (r1 > r0) - (r1 < r0)
        col, row = c0, r0
        while True:
            if (col, row) not in cells:
                cells.append((col, row))
            if (col, row) == (c1, r1):
                break
            col, row = col + step_c, row + step_r
    return cells


# --- the game's numbers --------------------------------------------------------------------

TOWERS = ["cannon", "mortar", "ice", "sniper"]
# Per tower: the price, the way it picks its target (0 closest, 1 first, 3 strongest), and for
# each of its three levels the numbers of the tower and of its shot, and the price of the
# improvement that makes it this level.
TOWER_STATS = {
    "cannon": {"cost": 50, "targeting": 1, "levels": [
        {"damage": 2, "range": 140, "cooldown": 45, "speed": 420, "upgrade": 0},
        {"damage": 3, "range": 150, "cooldown": 38, "speed": 440, "upgrade": 40},
        {"damage": 5, "range": 160, "cooldown": 30, "speed": 460, "upgrade": 70}]},
    "mortar": {"cost": 90, "targeting": 1, "levels": [
        {"damage": 3, "range": 150, "cooldown": 100, "speed": 220, "splash": 36, "upgrade": 0},
        {"damage": 5, "range": 165, "cooldown": 90, "speed": 230, "splash": 44, "upgrade": 70},
        {"damage": 8, "range": 180, "cooldown": 80, "speed": 240, "splash": 52, "upgrade": 110}]},
    "ice": {"cost": 70, "targeting": 0, "levels": [
        {"damage": 1, "range": 110, "cooldown": 50, "speed": 360, "slow": 0.55, "slow_ticks": 120,
         "upgrade": 0},
        {"damage": 1, "range": 120, "cooldown": 40, "speed": 360, "slow": 0.45, "slow_ticks": 150,
         "upgrade": 55},
        {"damage": 2, "range": 130, "cooldown": 30, "speed": 380, "slow": 0.35, "slow_ticks": 180,
         "upgrade": 85}]},
    "sniper": {"cost": 120, "targeting": 3, "levels": [
        {"damage": 10, "range": 260, "cooldown": 120, "speed": 900, "upgrade": 0},
        {"damage": 18, "range": 300, "cooldown": 110, "speed": 900, "upgrade": 90},
        {"damage": 30, "range": 340, "cooldown": 100, "speed": 900, "upgrade": 140}]},
}
BUY = {"cannon": 50, "mortar": 90, "ice": 70, "sniper": 120}

# The monsters: health, speed (units per second), bounty, damage to the base at the end.
ENEMIES = {
    "footman": {"health": 8, "speed": 40, "bounty": 5, "damage": 1},
    "runner": {"health": 5, "speed": 75, "bounty": 4, "damage": 1},
    "armored": {"health": 30, "speed": 32, "bounty": 12, "damage": 2},
    "boss": {"health": 400, "speed": 24, "bounty": 100, "damage": 10},
}

# The waves: (wave, monster, count, ticks between two, ticks before the first).
WAVES = [
    (1, "footman", 6, 50, 0),
    (2, "footman", 8, 40, 0),
    (3, "runner", 6, 30, 0), (3, "footman", 4, 50, 200),
    (4, "armored", 3, 90, 0), (4, "footman", 6, 40, 60),
    (5, "runner", 10, 25, 0), (5, "armored", 5, 80, 150),
    (6, "footman", 12, 30, 0), (6, "armored", 4, 80, 100),
    (7, "runner", 14, 20, 0), (7, "armored", 6, 70, 120),
    (8, "armored", 8, 60, 0), (8, "footman", 10, 30, 100),
    (9, "runner", 20, 15, 0), (9, "armored", 8, 60, 100),
    (10, "armored", 12, 50, 0), (10, "footman", 15, 25, 80),
    (11, "runner", 25, 12, 0), (11, "armored", 12, 40, 100),
    (12, "boss", 1, 0, 0), (12, "armored", 10, 60, 120), (12, "runner", 20, 20, 300),
]


def refund(name, level):
    levels = TOWER_STATS[name]["levels"]
    paid = TOWER_STATS[name]["cost"] + sum(levels[i]["upgrade"] for i in range(1, level))
    return paid * 6 // 10


def tower_prefab(name, level):
    return "%s_%d" % (name, level)


def shot_prefab(name, level):
    return "shot_%s_%d" % (name, level)


# The prefabs, in the order of the manifest (a "prefab" field holds the index): the towers
# come first, so that the index of a tower is also the cell of its picture in the atlas.
PREFAB_ORDER = ([tower_prefab(t, lv) for t in TOWERS for lv in (1, 2, 3)] +
                [shot_prefab(t, lv) for t in TOWERS for lv in (1, 2, 3)] +
                list(ENEMIES) + ["button"])
FIRST_OF = {t: PREFAB_ORDER.index(tower_prefab(t, 1)) for t in TOWERS}


# --- pictures ------------------------------------------------------------------------------

def rgba(hex_color, alpha=255):
    return [int(hex_color[i: i + 2], 16) for i in (0, 2, 4)] + [alpha]


CLEAR = [0, 0, 0, 0]
P = 32  # the size of the pictures of towers, monsters and shots
OUTLINE = rgba("1c1c24")


def circle(cx, cy, r):
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def ellipse(cx, cy, rx, ry):
    return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1


def rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1


def polygon(points):
    def inside(x, y):
        hit = False
        j = len(points) - 1
        for i, (xi, yi) in enumerate(points):
            xj, yj = points[j]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                hit = not hit
            j = i
        return hit
    return inside


def union(*shapes):
    return lambda x, y: any(shape(x, y) for shape in shapes)


def paint(layers, size=P):
    """Draws layers (shape, colour) one over the other, and an outline round the whole."""
    tile = [[CLEAR] * size for _ in range(size)]
    inside = [[False] * size for _ in range(size)]
    for shape, colour in layers:
        for y in range(size):
            for x in range(size):
                if shape(x, y):
                    tile[y][x] = rgba(colour)
                    inside[y][x] = True
    for y in range(size):
        for x in range(size):
            if inside[y][x]:
                continue
            if any(0 <= x + dx < size and 0 <= y + dy < size and inside[y + dy][x + dx]
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                tile[y][x] = OUTLINE
    return tile


def pips(tile, level):
    """One golden dot per level of the tower, at the foot of the picture."""
    for i in range(level):
        for dy in range(3):
            for dx in range(3):
                tile[28 + dy][9 + i * 5 + dx] = rgba("ffd23f")
    return tile


def tower_layers(name):
    if name == "cannon":
        return [(circle(15.5, 18, 11), "4a4f5c"), (rect(13, 3, 18, 18), "2c2f38"),
                (circle(15.5, 18, 6), "8d93a5")]
    if name == "mortar":
        return [(ellipse(15.5, 21, 12, 7), "7a5436"),
                (polygon([(9, 20), (12, 6), (20, 8), (22, 20)]), "b8621b"),
                (rect(11, 5, 21, 8), "3a2412")]
    if name == "ice":
        return [(circle(15.5, 22, 8), "2f6f9f"),
                (polygon([(15.5, 2), (23, 14), (15.5, 27), (8, 14)]), "9fe3ff"),
                (polygon([(15.5, 6), (19, 14), (15.5, 22), (12, 14)]), "e8fbff")]
    return [(circle(15.5, 21, 8), "3c5a3a"), (rect(14, 1, 17, 20), "1f2a1f"),
            (rect(11, 10, 20, 13), "6b8e5a"), (circle(15.5, 21, 4), "9ec28a")]


def enemy_layers(name):
    if name == "footman":
        return [(ellipse(15.5, 17, 10, 10), "5fb04a"), (circle(12, 14, 2), "ffffff"),
                (circle(19, 14, 2), "ffffff"), (circle(12, 14, 1), "1c1c24"),
                (circle(19, 14, 1), "1c1c24")]
    if name == "runner":
        return [(ellipse(17, 17, 8, 7), "f0b429"), (polygon([(3, 17), (10, 12), (10, 22)]), "f7d77a"),
                (circle(20, 14, 2), "ffffff"), (circle(20, 14, 1), "1c1c24")]
    if name == "armored":
        return [(circle(15.5, 17, 12), "8f98a8"), (rect(5, 5, 26, 14), "5a6273"),
                (rect(5, 14, 26, 16), "3b414f"), (circle(12, 20, 2), "ffffff"),
                (circle(19, 20, 2), "ffffff"), (circle(12, 20, 1), "1c1c24"),
                (circle(19, 20, 1), "1c1c24")]
    return [(circle(15.5, 18, 13), "c0392b"),
            (polygon([(4, 10), (7, 1), (12, 9)]), "e8e0c8"),
            (polygon([(27, 10), (24, 1), (19, 9)]), "e8e0c8"),
            (circle(11, 16, 3), "ffe14d"), (circle(20, 16, 3), "ffe14d"),
            (circle(11, 16, 1), "1c1c24"), (circle(20, 16, 1), "1c1c24"),
            (rect(10, 24, 21, 26), "1c1c24")]


def world_layers():
    return [
        [(rect(4, 12, 27, 28), "b9a78a"), (rect(4, 6, 9, 12), "b9a78a"),
         (rect(13, 4, 18, 12), "b9a78a"), (rect(22, 6, 27, 12), "b9a78a"),
         (rect(13, 20, 18, 28), "4b3b2a")],
        [(ellipse(15.5, 19, 12, 9), "7d7f86"), (ellipse(13, 15, 7, 5), "a3a5ac")],
        [(circle(15.5, 15.5, 5), "3a3d48")],
        [(circle(15.5, 15.5, 7), "e8731a"), (circle(15.5, 15.5, 3), "ffd27a")],
        [(polygon([(15.5, 7), (21, 15.5), (15.5, 24), (10, 15.5)]), "bff0ff")],
        [(circle(15.5, 15.5, 3), "fff3a0")],
    ]


def solid(color, size=16):
    return [[color] * size for _ in range(size)]


def ring(radius, thickness):
    tile = [[CLEAR] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            if radius - thickness <= d <= radius:
                tile[y][x] = rgba("ffffff")
    return tile


def atlas(cells, columns, size):
    rows = (len(cells) + columns - 1) // columns
    pixels = [[CLEAR] * (columns * size) for _ in range(rows * size)]
    for index, cell in enumerate(cells):
        ox, oy = (index % columns) * size, (index // columns) * size
        for y in range(size):
            for x in range(size):
                pixels[oy + y][ox + x] = cell[y][x]
    return pixels


def save_png(path, pixels):
    height, width = len(pixels), len(pixels[0])
    write_png(path, width, height, [[c for pixel in row for c in pixel] for row in pixels])


def write_json(path, data, comment=None):
    with open(path, "w") as out:
        if comment:
            for line in comment.strip().splitlines():
                out.write("// " + line.strip() + "\n")
        json.dump(data, out, indent=2)
        out.write("\n")


def write_atlas(name, cells, columns, size):
    save_png(os.path.join(GAME, "art", name + ".png"), atlas(cells, columns, size))
    write_json(os.path.join(GAME, "art", name + ".json"),
               {"schema_version": 1, "name": name, "image": name + ".png",
                "cell_width": size, "cell_height": size})


def write_art():
    towers = []
    for name in TOWERS:
        for level in (1, 2, 3):
            towers.append(pips(paint(tower_layers(name)), level))
    write_atlas("towers", towers, 6, P)
    write_atlas("monsters", [paint(enemy_layers(name)) for name in ENEMIES], 4, P)
    write_atlas("world", [paint(layers) for layers in world_layers()], 6, P)
    # Cells of "ui": 0 white, 1 button, 2 button with the focus, 3 button that is chosen,
    # 4 a ring.
    write_atlas("ui", [solid(rgba("ffffff")), solid(rgba("46586e")), solid(rgba("f0a03c")),
                       solid(rgba("4f9d5d")), ring(7.5, 1.8)], 5, 16)


# --- prefabs -------------------------------------------------------------------------------

def tower_prefabs():
    out = {}
    for name in TOWERS:
        stats = TOWER_STATS[name]
        for level in (1, 2, 3):
            numbers = stats["levels"][level - 1]
            components = {
                "transform2d.Position": {},
                "grid2d.GridPos": {"grid": "board", "from_position": True},
                "sprite2d.Sprite": {"atlas": "towers", "frame": PREFAB_ORDER.index(
                    tower_prefab(name, level)), "width": 44, "height": 44, "layer": 3},
                "autofire.AutoFire": {
                    "range": numbers["range"], "cooldown": numbers["cooldown"],
                    "prefab": shot_prefab(name, level), "projectile_speed": numbers["speed"],
                    "targeting": stats["targeting"], "target_layer": ["enemy"]},
                "economy.Sellable": {"refund": refund(name, level)},
                "phase.ActiveIn": {"phases": ALIVE},
            }
            if level < 3:
                components["economy.Upgradable"] = {
                    "next_prefab": tower_prefab(name, level + 1),
                    "price": stats["levels"][level]["upgrade"]}
            out[tower_prefab(name, level)] = (
                "A %s of level %d: it picks a target in range and shoots at it. Click it to "
                "improve it or to sell it." % (name, level),
                {"schema_version": 1, "name": tower_prefab(name, level),
                 "components": components})
    return out


def shot_prefabs():
    out = {}
    shot_frame = {"cannon": 2, "mortar": 3, "ice": 4, "sniper": 5}
    for name in TOWERS:
        for level in (1, 2, 3):
            numbers = TOWER_STATS[name]["levels"][level - 1]
            projectile = {"damage": numbers["damage"], "layers": ["enemy"],
                          "homing": 0.0 if name == "sniper" else 0.2}
            if "splash" in numbers:
                projectile["splash_radius"] = numbers["splash"]
            if "slow" in numbers:
                projectile["on_hit_speed_factor"] = numbers["slow"]
                projectile["on_hit_speed_ticks"] = numbers["slow_ticks"]
            life = int(numbers["range"] / numbers["speed"] * 60 * 1.6) + 10
            size = 26 if name == "mortar" else 18
            out[shot_prefab(name, level)] = (
                "What a %s of level %d shoots." % (name, level),
                {"schema_version": 1, "name": shot_prefab(name, level), "components": {
                    "transform2d.Position": {}, "transform2d.Velocity": {},
                    "sprite2d.Sprite": {"atlas": "world", "frame": shot_frame[name],
                                        "width": size, "height": size, "layer": 6},
                    "collision2d.Collider": {"width": 10, "height": 10, "layer": ["shot"],
                                             "mask": 0},
                    "combat2d.Projectile": projectile,
                    "lifetime.Lifetime": {"ticks": life},
                    "phase.ActiveIn": {"phases": ALIVE}}})
    return out


def enemy_prefabs():
    out = {}
    for frame, (name, stats) in enumerate(ENEMIES.items()):
        size = 52 if name == "boss" else 32
        out[name] = (
            "A monster (%s): it walks the road, is worth its bounty when it dies, and hurts the "
            "base when it gets there." % name,
            {"schema_version": 1, "name": name, "components": {
                "transform2d.Position": {},
                "sprite2d.Sprite": {"atlas": "monsters", "frame": frame, "width": size,
                                    "height": size, "layer": 2},
                "collision2d.Collider": {"width": size - 8, "height": size - 8,
                                         "layer": ["enemy"], "mask": 0},
                "health.Health": {"amount": stats["health"], "max": stats["health"]},
                "economy.Bounty": {"amount": stats["bounty"]},
                "path2d.PathFollower": {"path": "road", "speed": stats["speed"],
                                        "end_target": "base", "end_damage": stats["damage"]},
                "phase.ActiveIn": {"phases": ALIVE}}})
    return out


def prefabs():
    out = {}
    out.update(tower_prefabs())
    out.update(shot_prefabs())
    out.update(enemy_prefabs())
    out["button"] = ("A button of the bottom bar: a picture and its text.", {
        "schema_version": 1, "name": "button", "components": {
            "transform2d.Position": {},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 1, "width": 88, "height": 40,
                                "screen": True, "layer": 10},
            "text2d.Text": {"font": "font", "size": 15, "pivot_x": 0.5, "pivot_y": 0.5,
                            "screen": True},
            "ui.Button": {"menu": "game_menu", "width": 88, "height": 40, "normal_frame": 1,
                          "focus_frame": 2}}})
    return out


# --- scenes --------------------------------------------------------------------------------

WHITE = {"red": 240, "green": 240, "blue": 235}


def text(string, size, x, y, anchor_x=0.0, anchor_y=0.0, pivot_x=0.0, pivot_y=0.0, **extra):
    """A text in window pixels, anchored to the window: it follows its size."""
    return {
        "transform2d.Position": {},
        "transform2d.Anchor": {"anchor_x": anchor_x, "anchor_y": anchor_y, "offset_x": x, "offset_y": y},
        "text2d.Text": dict({"string": string, "font": "font", "size": size, "screen": True,
                             "pivot_x": pivot_x, "pivot_y": pivot_y}, **dict(WHITE, **extra)),
    }


def tagged(components, tag):
    return dict(components, **{"tag.Tag": {"tag": tag}})


def bound(components, source, field):
    return dict(components, **{"ui.Bind": {"source": source, "field": field}})


def shown_in(components, phases):
    return dict(components, **{"phase.ShowIn": {"phases": phases}})


def sprite(frame, size, layer, atlas_name="ui", **tint):
    return {"atlas": atlas_name, "frame": frame, "width": size, "height": size,
            "layer": layer, **tint}


def terrain():
    out = [
        {"name": "paper", "components": {
            "transform2d.Position": {}, "transform2d.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": 4000, "height": 4000,
                                "screen": True, "layer": -10, "red": 30, "green": 36,
                                "blue": 44}}},
        {"name": "frame", "components": {
            "transform2d.Position": {},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": COLS * CELL + 16,
                                "height": ROWS * CELL + 16, "layer": -4,
                                "red": 62, "green": 48, "blue": 36}}},
        {"name": "field", "components": {
            "transform2d.Position": {},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": COLS * CELL,
                                "height": ROWS * CELL, "layer": -3,
                                "red": 96, "green": 150, "blue": 80}}},
        {"name": "board", "components": {
            "transform2d.Position": {},
            "grid2d.Grid": {"id": "board", "columns": COLS, "rows": ROWS,
                            "cell_width": CELL, "cell_height": CELL}}},
        {"name": "camera", "components": {
            "transform2d.Position": {}, "camera2d.Camera": {"zoom": 1},
            "camera2d.CameraFit": {"width": AREA_W, "height": AREA_H,
                                   "margin_top": MARGIN_TOP, "margin_bottom": MARGIN_BOTTOM,
                                   "max_zoom": 1.5}}},
    ]
    for col, row in road_cells():
        x, y = centre(col, row)
        out.append({"name": "road_%d_%d" % (col, row), "components": {
            "transform2d.Position": {"x": x, "y": y},
            "grid2d.GridPos": {"grid": "board", "col": col, "row": row},
            "grid2d.Blocked": {},
            "sprite2d.Sprite": sprite(0, CELL + 1, -2, red=214, green=190, blue=140)}})
    for col, row in ROCKS:
        x, y = centre(col, row)
        out.append({"name": "rock_%d_%d" % (col, row), "components": {
            "transform2d.Position": {"x": x, "y": y},
            "grid2d.GridPos": {"grid": "board", "col": col, "row": row},
            "grid2d.Blocked": {},
            "sprite2d.Sprite": sprite(1, 42, 1, "world")}})
    for index, (col, row) in enumerate(ROAD):
        x, y = centre(col, row)
        out.append({"name": "waypoint_%d" % index, "components": {
            "transform2d.Position": {"x": x, "y": y},
            "path2d.Waypoint": {"path": "road", "index": index}}})
    return out


def base_entity(lives):
    col, row = ROAD[-1]
    x, y = centre(col, row)
    return {"name": "base", "components": tagged({
        "transform2d.Position": {"x": x, "y": y},
        "sprite2d.Sprite": sprite(0, 44, 2, "world"),
        "health.Health": {"amount": lives, "max": START_LIVES, "on_zero": "lost",
                          "despawn_on_zero": False}}, "base")}


def phases():
    def transition(name, action, source, target):
        return {"name": name, "components": {"phase.Transition": {
            "action": action, "from_any": False, "from": source, "to": target}}}

    return [
        {"name": "game_phase", "components": {"phase.Phase": {"current": "playing"}}},
        transition("to_tower", "selected", ALIVE, "tower"),
        transition("to_tower_final", "final", ALIVE, "tower_final"),
        transition("to_playing", "deselected", TOWER | TOWER_FINAL, "playing"),
        transition("to_won", "all_cleared", ALIVE, "won"),
        transition("to_lost", "lost", ALIVE, "lost"),
    ]


def waves_entities(groups, current=0):
    x, y = centre(*ROAD[0])
    out = [{"name": "waves", "components": tagged({
        "waves.WaveController": {
            "current": current, "spawn_x": x, "spawn_y": y, "start_action": "start_wave",
            "started_action": "wave_started", "cleared_action": "wave_cleared",
            "all_cleared_action": "all_cleared"},
        "phase.ActiveIn": {"phases": ALIVE}}, "waves")}]
    for index, (wave, enemy, count, interval, delay) in enumerate(groups):
        out.append({"name": "group_%02d" % index, "components": {"waves.WaveGroup": {
            "wave": wave, "prefab": enemy, "count": count, "interval_ticks": interval,
            "delay_ticks": delay}}})
    return out


def shop_entities(gold):
    out = [
        {"name": "wallet", "components": tagged({"economy.Wallet": {"amount": gold}}, "wallet")},
        # The hand: it follows the pointer over the board (GridCursor), buys what the shop
        # chose (Placer), and its ring shows the tower that is chosen.
        {"name": "hand", "components": tagged({
            "transform2d.Position": {},
            "grid2d.GridCursor": {"grid": "board", "pointer_x": "pointer_x",
                                  "pointer_y": "pointer_y", "click": "click"},
            "economy.Placer": {
                "grid": "board", "prefab": "cannon_1", "price": BUY["cannon"],
                "placed_action": "placed", "refused_action": "refused",
                "selected_action": "selected", "final_action": "final",
                "deselected_action": "deselected", "upgrade_action": "upgrade",
                "sell_action": "sell"},
            "sprite2d.Sprite": sprite(4, 54, 5, red=255, green=224, blue=70),
            "phase.ActiveIn": {"phases": ALIVE}}, "hand")},
    ]
    for name in TOWERS:
        out.append({"name": "pick_%s_prefab" % name, "components": {"rules.OnAction": {
            "action": "pick_" + name, "source": "hand", "field": "economy.Placer.prefab",
            "value": FIRST_OF[name]}}})
        out.append({"name": "pick_%s_price" % name, "components": {"rules.OnAction": {
            "action": "pick_" + name, "source": "hand", "field": "economy.Placer.price",
            "value": BUY[name]}}})
        # The button of the chosen tower looks chosen, whether the pointer is on it or not.
        for other in TOWERS:
            for field, chosen, usual in (("normal_frame", 3, 1), ("focus_frame", 3, 2)):
                out.append({"name": "pick_%s_marks_%s_%s" % (name, other, field),
                            "components": {"rules.OnAction": {
                                "action": "pick_" + name, "source": "button_" + other,
                                "field": "ui.Button." + field,
                                "value": chosen if other == name else usual}}})
    return out


def interface_entities():
    out = [
        {"name": "gold", "components": bound(
            text("hud.gold", 20, 14, 8, 0, 0, 0, 0), "wallet", "economy.Wallet.amount")},
        {"name": "wave", "components": bound(
            text("hud.wave", 20, -4, 8, 0.5, 0, 1, 0), "waves", "waves.WaveController.current")},
        {"name": "wave_last", "components": bound(
            text("hud.wave_last", 20, 4, 8, 0.5, 0, 0, 0), "waves", "waves.WaveController.last")},
        {"name": "lives", "components": bound(
            text("hud.lives", 20, -14, 8, 1, 0, 1, 0), "base", "health.Health.amount")},
        {"name": "victory", "components": shown_in(
            text("end.won", 34, 0, 150, 0.5, 0, 0.5, 0.5, red=255, green=224, blue=90), WON)},
        {"name": "defeat", "components": shown_in(
            text("end.lost", 34, 0, 150, 0.5, 0, 0.5, 0.5, red=240, green=100, blue=90), LOST)},
        {"name": "game_menu_input", "components": {"ui.Menu": {
            "group": "game_menu", "use_pointer": True, "pointer_x": "pointer_x",
            "pointer_y": "pointer_y", "click": "click"}}},
    ]
    for index, name in enumerate(TOWERS):
        out.append({"name": "button_" + name, "prefab": "button", "components": tagged({
            "transform2d.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_x": SHOP_X + index * SHOP_STEP,
                          "offset_y": -32},
            "ui.Button": {"order": index, "action": "pick_" + name,
                          "normal_frame": 3 if name == "cannon" else 1,
                          "focus_frame": 3 if name == "cannon" else 2},
            "text2d.Text": {"string": "button." + name},
            "phase.ShowIn": {"phases": PLAYING}}, "button_" + name)})
    out.append({"name": "button_upgrade", "prefab": "button", "components": bound({
        "transform2d.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_x": UPGRADE_X, "offset_y": -32},
        "sprite2d.Sprite": {"width": 130},
        "ui.Button": {"order": 4, "action": "upgrade", "width": 130},
        "text2d.Text": {"string": "button.upgrade"},
        "phase.ShowIn": {"phases": TOWER}}, "hand", "economy.Placer.upgrade_price")})
    out.append({"name": "button_sell", "prefab": "button", "components": bound({
        "transform2d.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_x": SELL_X, "offset_y": -32},
        "sprite2d.Sprite": {"width": 130},
        "ui.Button": {"order": 5, "action": "sell", "width": 130},
        "text2d.Text": {"string": "button.sell"},
        "phase.ShowIn": {"phases": TOWER | TOWER_FINAL}}, "hand", "economy.Placer.sell_value")})
    out.append({"name": "button_next", "prefab": "button", "components": {
        "transform2d.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_x": NEXT_X, "offset_y": -32},
        "sprite2d.Sprite": {"width": 120},
        "ui.Button": {"order": 6, "action": "start_wave", "width": 120},
        "text2d.Text": {"string": "button.next"},
        "phase.ShowIn": {"phases": ALIVE}}})
    out.append({"name": "button_new", "prefab": "button", "components": {
        "transform2d.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_x": 0, "offset_y": -32},
        "sprite2d.Sprite": {"width": 170},
        "ui.Button": {"order": 7, "action": "new_game", "width": 170},
        "text2d.Text": {"string": "button.new"},
        "phase.ShowIn": {"phases": WON | LOST}}})
    out.append({"name": "new_game", "components": {"scene_switch.GoToScene": {
        "scene": "game", "use_action": True, "action": "new_game"}}})
    return out


def scene(towers=(), groups=WAVES, enemies=(), gold=START_GOLD, lives=START_LIVES, current=0):
    """The game (a few towers, monsters and waves given for a bench)."""
    out = terrain() + [base_entity(lives)] + phases() + waves_entities(groups, current)
    out += shop_entities(gold) + interface_entities()
    for name, col, row in towers:
        x, y = centre(col, row)
        out.append({"name": "tower_%d_%d" % (col, row), "prefab": name, "components": {
            "transform2d.Position": {"x": x, "y": y}}})
    for name, kind, x, y, speed in enemies:
        out.append({"name": name, "prefab": kind, "components": {
            "transform2d.Position": {"x": x, "y": y},
            "path2d.PathFollower": {"speed": speed}}})
    return out


def scenes():
    start = centre(*ROAD[0])
    return {
        "game": scene(),
        # A mortar and three monsters standing together (they do not walk): one shell hurts all.
        "test_splash": scene(
            [("mortar_1", 4, 2)], [(1, "footman", 1, 0, 100000)],
            [("m1", "footman", centre(3, 1)[0], centre(3, 1)[1], 0),
             ("m2", "footman", centre(3, 1)[0] - 6, centre(3, 1)[1] + 3, 0),
             ("m3", "footman", centre(3, 1)[0] + 6, centre(3, 1)[1] - 3, 0)]),
        # An ice tower and a monster that does not walk by itself fast enough to matter.
        "test_slow": scene(
            [("ice_1", 3, 2)], [(1, "footman", 1, 0, 100000)],
            [("m1", "footman", centre(3, 1)[0], centre(3, 1)[1], 1)]),
        # Two waves of one monster against a sniper that never misses: the wave is cleared.
        "test_waves": scene(
            [("sniper_3", 4, 2)], [(1, "footman", 1, 0, 0), (2, "runner", 2, 40, 0)]),
        # The last wave: the boss, against four snipers of the last level.
        "test_boss": scene(
            [("sniper_3", 3, 2), ("sniper_3", 4, 2), ("sniper_3", 5, 2), ("sniper_3", 6, 2)],
            [(12, "boss", 1, 0, 0)], current=11),
        # Plenty of gold, to go through the three levels of a tower.
        "test_rich": scene(gold=1000),
        # The base has two lives left and two fast monsters come to it.
        "test_defeat": scene(
            [], [(1, "footman", 1, 0, 100000)],
            [("m1", "footman", start[0], start[1], 400),
             ("m2", "footman", start[0], start[1], 400)], lives=2),
    }


# --- scenarios -----------------------------------------------------------------------------

def tap(tick, action):
    return [{"tick": tick, "action": action, "value": 1},
            {"tick": tick + 1, "action": action, "value": 0}]


def touch_pixel(tick, x, y):
    return [{"tick": tick - 1, "action": "pointer_x", "value": x},
            {"tick": tick - 1, "action": "pointer_y", "value": y}] + tap(tick, "click")


def touch(tick, col, row):
    return touch_pixel(tick, *pixel_of(col, row))


def press_button(tick, offset_x):
    """A touch on a button of the bottom bar (they are anchored to the bottom of the window)."""
    return touch_pixel(tick, round(WIDTH / 2 + offset_x), HEIGHT - 32)


def at_tick(tick, *checks):
    return [{"at": tick, "check": check} for check in checks]


def scenario(name, comment, ticks, inputs, expect, scene_name=None, screen=None):
    data = {"schema_version": 1, "name": name}
    if scene_name:
        data["scene"] = scene_name
    if screen:
        data["screen"] = {"width": screen[0], "height": screen[1]}
    data.update({"ticks": ticks, "inputs": inputs, "expect": expect})
    return name, comment, data


# A phone held upright (320 x 640): the game is wider than tall, so it takes its narrowest shape,
# 600 x 480, with bars above and below. The same touches, in pixels of that smaller area.
PHONE = (320, 640)
PHONE_WIDTH = 600
PHONE_ZOOM = min(PHONE_WIDTH / AREA_W, (HEIGHT - MARGIN_TOP - MARGIN_BOTTOM) / AREA_H)


def phone_touch(tick, col, row):
    x, y = centre(col, row)
    return touch_pixel(tick, round(PHONE_WIDTH / 2 + x * PHONE_ZOOM), round(MIDDLE_Y + y * PHONE_ZOOM))


def phone_button(tick, offset_x):
    return touch_pixel(tick, round(PHONE_WIDTH / 2 + offset_x), HEIGHT - 32)


GOLD = "wallet.economy.Wallet.amount == %d"
LIVES = "base.health.Health.amount == %d"
PHASE = "game_phase.phase.Phase.current == %d"


def scenarios():
    size = len(scene())
    out = []
    out.append(scenario(
        "start_state", "A new game: gold, lives, no wave yet, the first phase.", 5, [], at_tick(
            5, GOLD % START_GOLD, LIVES % START_LIVES, PHASE % 0,
            "waves.waves.WaveController.current == 0", "waves.waves.WaveController.last == 12",
            "waves.waves.WaveController.running == false", "entities == %d" % size)))
    out.append(scenario(
        "buying_takes_the_price", "A touch on a free cell builds the chosen tower (the cannon "
        "to begin with) and takes its price.", 30, touch(10, 3, 2),
        at_tick(30, GOLD % (START_GOLD - 50), "entities == %d" % (size + 1))))
    out.append(scenario(
        "not_enough_gold", "A tower that costs more than the gold left is not built.", 60,
        touch(10, 3, 2) + tap(20, "pick_sniper") + touch(30, 4, 2),
        at_tick(60, GOLD % (START_GOLD - 50), "entities == %d" % (size + 1))))
    out.append(scenario(
        "no_building_on_the_road", "The road and the rocks are not for towers.", 40,
        touch(10, 5, 1) + touch(20, 7, 0) + touch(30, 11, 3),
        at_tick(40, GOLD % START_GOLD, "entities == %d" % size)))
    out.append(scenario(
        "the_shop_chooses_what_is_bought", "A key (or a button) chooses the tower of the next "
        "purchase: the mortar costs 90.", 40,
        tap(5, "pick_mortar") + touch(15, 3, 2) + tap(25, "pick_cannon") + touch(35, 4, 2),
        at_tick(40, GOLD % (START_GOLD - 90 - 50), "entities == %d" % (size + 2))))
    out.append(scenario(
        "the_buttons_of_the_shop_work", "The buttons of the bottom bar choose the tower too: "
        "the second is the mortar.", 40, press_button(5, SHOP_X + SHOP_STEP) + touch(20, 3, 2),
        at_tick(40, GOLD % (START_GOLD - 90), "entities == %d" % (size + 1))))
    out.append(scenario(
        "a_tower_kills_and_the_bounty_is_paid", "A cannon beside the road shoots the first "
        "monsters of the first wave: they are worth their bounty, and none reaches the base.",
        900, touch(10, 3, 2) + tap(20, "start_wave"),
        at_tick(900, "wallet.economy.Wallet.amount > 100", LIVES % START_LIVES,
                "waves.waves.WaveController.current == 1")))
    out.append(scenario(
        "played_on_a_phone_held_upright", "On a 320 x 640 screen the game takes its narrowest shape "
        "(600 x 480, bars above and below): a cannon is built with a touch, a mortar chosen with "
        "its shop button, the next wave called with its button, and every one of them is within "
        "reach.", 900,
        phone_touch(10, 3, 2) + phone_button(20, SHOP_X + SHOP_STEP) + phone_touch(30, 4, 2) +
        phone_button(40, NEXT_X),
        at_tick(35, GOLD % (START_GOLD - 50 - 90), "entities == %d" % (size + 2)) +
        at_tick(900, "waves.waves.WaveController.current == 1", LIVES % START_LIVES),
        screen=PHONE))
    out += improving_and_selling(size)
    out += rules_of_the_waves()
    out += the_end(size)
    return out


def improving_and_selling(size):
    built = touch(10, 3, 2)  # the cannon of the first purchase, 50 gold
    selected = built + touch(20, 3, 2)
    return [
        scenario(
            "touching_a_tower_selects_it", "A touch on a tower selects it: the phase shows what "
            "can be done with it, and a touch elsewhere lets it go.", 70,
            selected + touch(40, 7, 0),
            at_tick(30, PHASE % INDEX["tower"], "hand.economy.Placer.upgrade_price == 40",
                    "hand.economy.Placer.sell_value == 30") +
            at_tick(70, PHASE % INDEX["playing"], "hand.economy.Placer.sell_value == 0")),
        scenario(
            "a_tower_can_be_improved", "Improving a tower takes the price, replaces it with the "
            "next level and selects that one.", 60, selected + tap(30, "upgrade"),
            at_tick(60, GOLD % (START_GOLD - 50 - 40), PHASE % INDEX["tower"],
                    "entities == %d" % (size + 1), "hand.economy.Placer.upgrade_price == 70",
                    "hand.economy.Placer.sell_value == 54")),
        scenario(
            "an_improvement_needs_the_gold", "Without the gold, the improvement is refused and "
            "nothing changes.", 80, selected + tap(30, "upgrade") + tap(50, "upgrade"),
            at_tick(80, GOLD % (START_GOLD - 50 - 40), PHASE % INDEX["tower"],
                    "hand.economy.Placer.upgrade_price == 70")),
        scenario(
            "selling_gives_a_refund", "Selling gives back a part of what the tower cost and "
            "frees its cell.", 60, selected + tap(30, "sell"),
            at_tick(60, GOLD % (START_GOLD - 50 + 30), PHASE % INDEX["playing"],
                    "entities == %d" % size)),
        scenario(
            "the_last_level_can_only_be_sold", "A tower of the last level has nothing to improve: "
            "the phase says so, and the action does nothing.", 140,
            selected + tap(30, "upgrade") + tap(50, "upgrade") + tap(70, "upgrade") +
            tap(100, "sell"),
            at_tick(80, GOLD % (1000 - 50 - 40 - 70), PHASE % INDEX["tower_final"],
                    "hand.economy.Placer.upgrade_price == 0",
                    "hand.economy.Placer.sell_value == 96") +
            at_tick(140, GOLD % (1000 - 50 - 40 - 70 + 96), PHASE % INDEX["playing"],
                    "entities == %d" % size),
            "test_rich"),
        scenario(
            "the_buttons_improve_and_sell", "The Upgrade and Sell buttons of the bottom bar do the "
            "same as their actions.", 100,
            selected + press_button(30, UPGRADE_X) + press_button(60, SELL_X),
            at_tick(100, GOLD % (START_GOLD - 50 - 40 + 54), PHASE % INDEX["playing"],
                    "entities == %d" % size)),
    ]


def rules_of_the_waves():
    return [
        scenario(
            "a_mortar_hurts_a_group", "A shell hurts everything near where it lands: three "
            "monsters side by side lose three points each (they have eight).", 80, [],
            at_tick(80, "m1.health.Health.amount == 5", "m2.health.Health.amount == 5",
                    "m3.health.Health.amount == 5"), "test_splash"),
        scenario(
            "an_ice_tower_slows", "What an ice tower hits is slowed down for a while.", 60, [],
            at_tick(60, "m1.transform2d.SpeedScale.factor < 0.6"), "test_slow"),
        scenario(
            "a_wave_is_cleared_when_its_monsters_are_gone", "The wave starts when it is asked "
            "for, and is over when its monsters are all dead; the next one waits to be asked "
            "for.", 400, tap(5, "start_wave"),
            at_tick(20, "waves.waves.WaveController.current == 1",
                    "waves.waves.WaveController.running == true") +
            at_tick(400, "waves.waves.WaveController.current == 1",
                    "waves.waves.WaveController.running == false", PHASE % INDEX["playing"],
                    LIVES % START_LIVES), "test_waves"),
        scenario(
            "the_last_wave_wins", "When the last wave is cleared, the game is won.", 900,
            tap(5, "start_wave") + tap(300, "start_wave"),
            at_tick(320, "waves.waves.WaveController.current == 2") +
            at_tick(900, PHASE % INDEX["won"], LIVES % START_LIVES), "test_waves"),
        scenario(
            "the_boss_comes_last", "The twelfth wave is the boss: four snipers of the last level "
            "bring it down, and the game is won with the base untouched.", 1200,
            tap(5, "start_wave"),
            at_tick(20, "waves.waves.WaveController.current == 12",
                    "waves.waves.WaveController.last == 12") +
            at_tick(1200, PHASE % INDEX["won"], LIVES % START_LIVES), "test_boss"),
    ]


def the_end(size):
    return [
        scenario(
            "the_base_can_fall", "Monsters that reach the end of the road hurt the base, and with "
            "no life left the game is lost.", 500, [],
            at_tick(500, LIVES % 0, PHASE % INDEX["lost"]), "test_defeat"),
        scenario(
            "nothing_is_built_after_the_defeat", "When the game is lost, a touch builds nothing.",
            500, touch(450, 3, 2),
            at_tick(500, GOLD % START_GOLD, PHASE % INDEX["lost"]), "test_defeat"),
        scenario(
            "new_game_starts_again", "After a defeat, new game brings back the first scene.", 500,
            tap(400, "new_game") + touch(450, 3, 2),
            at_tick(500, "scene is game") +
            at_tick(500, GOLD % (START_GOLD - 50), LIVES % START_LIVES, PHASE % 0),
            "test_defeat"),
        scenario(
            "new_game_with_the_button", "The New game button of the end screen does the same.",
            500, press_button(400, 0),
            at_tick(500, "scene is game", LIVES % START_LIVES), "test_defeat"),
    ]


# --- the manifest and the texts ------------------------------------------------------------

HEADER = """// "Hold the road."
// A tower defense: monsters walk a road to your base, you build towers beside it with the gold
// they leave behind, improve them, sell them, and survive twelve waves. Every rule is data: a
// tower is a prefab with an AutoFire, a monster a prefab with a Health, a Bounty and a
// PathFollower, the waves are entities (a WaveGroup each), the road is entities (a Waypoint
// each), what the next purchase is comes from buttons. No game code.
// Pictures, prefabs, scenes and the manifest are generated by tools/make_assets.py.
"""

TEXTS = {
    "en": {
        "hud.gold": "Gold {value}", "hud.wave": "Wave {value}", "hud.wave_last": "/ {value}",
        "hud.lives": "Lives {value}",
        "end.won": "The road held: you win!", "end.lost": "The base has fallen",
        "button.cannon": "Cannon 50", "button.mortar": "Mortar 90", "button.ice": "Ice 70",
        "button.sniper": "Sniper 120", "button.upgrade": "Upgrade {value}",
        "button.sell": "Sell {value}", "button.next": "Next wave", "button.new": "New game",
    },
    "fr": {
        "hud.gold": "Or {value}", "hud.wave": "Vague {value}", "hud.wave_last": "/ {value}",
        "hud.lives": "Vies {value}",
        "end.won": "La route a tenu : victoire !", "end.lost": "La base est tombée",
        "button.cannon": "Canon 50", "button.mortar": "Mortier 90", "button.ice": "Glace 70",
        "button.sniper": "Sniper 120", "button.upgrade": "Améliorer {value}",
        "button.sell": "Vendre {value}", "button.next": "Vague suivante",
        "button.new": "Nouvelle partie",
    },
}


def write_manifest(scenes_list, scenarios_list):
    data = {
        "schema_version": 1,
        "name": "tower_defense",
        "title": "Hold the Road",
        "description": "A tower defense: build cannons, mortars, ice and sniper towers beside the "
                       "road, improve or sell them, and stop twelve waves of monsters before "
                       "they reach your base. Touch a free cell to build, a tower to improve it "
                       "or sell it; 1 to 4 choose the tower, space calls the next wave, R "
                       "starts again.",
        "modules": ["transform2d", "collision2d", "camera2d", "grid2d", "health", "lifetime",
                    "combat2d", "path2d", "autofire", "waves", "economy", "rules", "phase",
                    "sprite2d", "text2d", "scene_switch", "ui"],
        "simulation": {"tick_rate": 60, "seed": 1},
        "window": {"width": WIDTH, "height": HEIGHT, "adapt": True},
        "input": "input.json",
        "lists": {
            "grid": ["board"],
            "layer": ["enemy", "shot"],
            "path": ["road"],
            "phase": PHASES,
            "action": ["start_wave", "wave_started", "wave_cleared", "all_cleared", "placed",
                       "refused", "selected", "final", "deselected", "upgrade", "sell", "lost",
                       "new_game", "pick_cannon", "pick_mortar", "pick_ice", "pick_sniper"],
            "menu": ["game_menu"],
            "tag": ["base", "wallet", "hand", "waves"] + ["button_" + t for t in TOWERS],
        },
        "atlases": ["art/towers.json", "art/monsters.json", "art/world.json", "art/ui.json",
                    "art/font.json"],
        "strings": ["text/en.json", "text/fr.json"],
        "locale": "en",
        "prefabs": ["prefabs/%s.json" % name for name in PREFAB_ORDER],
        "scenes": scenes_list,
        "scenarios": scenarios_list,
        "start_scene": "game",
    }
    with open(os.path.join(GAME, "game.json"), "w") as out:
        out.write(HEADER)
        json.dump(data, out, indent=2)
        out.write("\n")


INPUT = """// A touch or a click on a cell builds, or selects a tower; the buttons of the bottom bar do
// the rest. 1 to 4 choose the tower to build, space calls the next wave, R starts again. The
// first finger of a touch screen is also the mouse.
{
  "schema_version": 1,
  "bindings": [
    { "action": "pick_cannon", "sources": { "key:1": 1 } },
    { "action": "pick_mortar", "sources": { "key:2": 1 } },
    { "action": "pick_ice", "sources": { "key:3": 1 } },
    { "action": "pick_sniper", "sources": { "key:4": 1 } },
    { "action": "start_wave", "sources": { "key:space": 1 } },
    { "action": "upgrade", "sources": { "key:u": 1 } },
    { "action": "sell", "sources": { "key:s": 1 } },
    { "action": "new_game", "sources": { "key:r": 1 } },
    { "action": "pointer_x", "sources": { "mouse:x": 1 } },
    { "action": "pointer_y", "sources": { "mouse:y": 1 } },
    { "action": "click", "sources": { "mouse:left": 1 } },
  ],
}
"""


def main():
    write_art()
    for name, (comment, data) in prefabs().items():
        write_json(os.path.join(GAME, "prefabs", name + ".json"), data, comment)
    for name, entities in scenes().items():
        write_json(os.path.join(GAME, "scenes", name + ".json"),
                   {"schema_version": 1, "name": name, "entities": entities})
    names = []
    for name, comment, data in scenarios():
        write_json(os.path.join(GAME, "scenarios", name + ".json"), data, comment)
        names.append(name)
    for locale, entries in TEXTS.items():
        write_json(os.path.join(GAME, "text", locale + ".json"),
                   {"schema_version": 1, "locale": locale, "entries": entries})
    with open(os.path.join(GAME, "input.json"), "w") as out:
        out.write(INPUT)
    write_manifest(["scenes/%s.json" % name for name in scenes()],
                   ["scenarios/%s.json" % name for name in names])
    print("wrote %d prefabs, %d scenes and %d scenarios" % (len(PREFAB_ORDER), len(scenes()),
                                                            len(names)))


if __name__ == "__main__":
    main()
