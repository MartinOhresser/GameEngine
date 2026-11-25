#!/usr/bin/env python3
"""Rebuilds the generated files of Complete Universe: every picture, their atlases and the scenes.

    python3 games/complete_universe/tools/make_assets.py      (numpy and Pillow)

The look (docs/complete_universe): Limbo at the start, Ori as the colours come back. A side view,
as in Ori: the top of the ground shows a little, the hero is seen from the side with his chest
turned a little towards us. Everything is drawn here from fixed seeds, so the same script always
gives the same pictures and scenes.
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, ".."))
ART = os.path.join(GAME, "art")
SCENES = os.path.join(GAME, "scenes")
TEXT = os.path.join(GAME, "text")
PREFABS = os.path.join(GAME, "prefabs")

VIEW_W, VIEW_H = 1280, 720  # the window, in world units at zoom 1
SUPER = 3  # shapes are drawn this many times bigger, then reduced: smooth edges

HERO_W, HERO_H = 34, 84  # the hero's box: his Position is its middle
CELL = 128  # one picture of the hero

SANDBOX_W = 4096  # the test ground runs from x 0 to here
GROUND_Y = 600  # top of the test ground

BLACK = (10, 10, 12)
WHITE = (236, 236, 232)
GREY = (120, 120, 124)

# The drawing order of the scenes: lower is further back.
LAYERS = {"sky": -100, "ground": 0, "hero": 10}


# ---------------------------------------------------------------------------------------------
# Small tools


def save(picture, name, cell=None):
    """Writes the picture and its atlas: one cell the size of the picture, or `cell` (w, h)."""
    os.makedirs(ART, exist_ok=True)
    picture.save(os.path.join(ART, name + ".png"), optimize=True)
    width, height = cell or picture.size
    atlas = {"schema_version": 1, "name": name, "image": name + ".png", "cell_width": width,
             "cell_height": height, "filter": "linear"}
    with open(os.path.join(ART, name + ".json"), "w", encoding="utf-8") as out:
        json.dump(atlas, out, indent=2)
        out.write("\n")


def canvas(width, height):
    """A transparent picture SUPER times bigger than wanted, and a drawing tool for it."""
    picture = Image.new("RGBA", (width * SUPER, height * SUPER), (0, 0, 0, 0))
    return picture, ImageDraw.Draw(picture)


def reduce(picture, width, height):
    return picture.resize((width, height), Image.LANCZOS)


def oval(draw, cx, cy, rx, ry, color, angle=0.0):
    """An ellipse centred on (cx, cy), in final pixels, turned by `angle` degrees."""
    s = SUPER
    if angle == 0.0:
        draw.ellipse([(cx - rx) * s, (cy - ry) * s, (cx + rx) * s, (cy + ry) * s], fill=color)
        return
    points = []
    a = math.radians(angle)
    for i in range(48):
        t = 2 * math.pi * i / 48
        x, y = rx * math.cos(t), ry * math.sin(t)
        points.append(((cx + x * math.cos(a) - y * math.sin(a)) * s,
                       (cy + x * math.sin(a) + y * math.cos(a)) * s))
    draw.polygon(points, fill=color)


# ---------------------------------------------------------------------------------------------
# Pictures


def hero():
    """A placeholder of the hero, facing right: ovals, the body split down the middle (his left
    half white, the one we see most when he faces right; his right half black), grey arms."""
    picture, draw = canvas(CELL, CELL)
    cx, feet = CELL / 2, CELL / 2 + HERO_H / 2
    oval(draw, cx - 5, feet - 10, 6, 11, BLACK)  # far leg
    oval(draw, cx + 4, feet - 10, 6, 11, WHITE)  # near leg
    oval(draw, cx - 4, feet - 44, 10, 22, BLACK)  # chest: the far half
    oval(draw, cx + 2, feet - 44, 11, 22, WHITE)  # chest: the near half
    oval(draw, cx + 2, feet - 76, 10, 11, WHITE)  # head
    oval(draw, cx - 3, feet - 76, 6, 10, BLACK)
    return reduce(picture, CELL, CELL)


# The powers (GDD §6), in the order of the game's power list; the last ones are what two hands make
# together. A hand's arm takes the colour of its power (GDD §3).
POWERS = ["black", "white", "blue", "green", "red", "yellow", "orange", "violet", "pink",
          "harvest"]
ARM_COLOURS = {"black": (14, 14, 16), "white": (240, 240, 236), "blue": (70, 130, 230),
               "green": (80, 190, 110), "red": (220, 60, 50), "yellow": (240, 210, 80),
               "orange": (240, 140, 50), "violet": (150, 90, 210), "pink": (240, 130, 190)}
ARM = 48  # one picture of an arm


def arms():
    """One arm for each power that a hand can hold, then the empty arm (grey): the cells of the
    arm atlas, chosen by the power of the hand."""
    frames = []
    for colour in list(ARM_COLOURS.values()) + [GREY]:
        picture, draw = canvas(ARM, ARM)
        oval(draw, ARM / 2, ARM / 2, 5, 15, colour + (255,), angle=-8)
        # A thin rim, so that a black arm shows against the dark and a white one in the light.
        frames.append(reduce(picture, ARM, ARM))
    sheet = Image.new("RGBA", (ARM * len(frames), ARM), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.paste(frame, (index * ARM, 0))
    return sheet


HOLE = 256  # the picture of a black hole, drawn for a radius of 1 (a sprite 2 units wide)


def black_hole():
    """A black hole seen from the side of its disc: a black heart (the share of the radius that
    swallows, 0.4), a thin bright ring of matter falling in, and a faint halo out to the edge of
    its pull."""
    size = HOLE
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    r = np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) / (size / 2)
    heart = 0.4
    ring = np.exp(-((r - heart - 0.035) / 0.03) ** 2)
    halo = np.clip(1.0 - (r - heart) / (1.0 - heart), 0, 1) ** 3 * 0.35
    light = np.clip(ring * 1.0 + halo, 0, 1)
    alpha = np.where(r <= heart, 1.0, np.clip(light * 1.4, 0, 1))
    alpha *= np.clip((1.0 - r) * 20, 0, 1)
    soft = np.clip((r - heart + 0.01) / 0.02, 0, 1)  # a smooth edge to the heart
    value = light * soft * 255
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., 0] = value.astype(np.uint8)
    rgba[..., 1] = value.astype(np.uint8)
    rgba[..., 2] = np.clip(value * 1.05, 0, 255).astype(np.uint8)
    rgba[..., 3] = (alpha * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


BARRIER = 256  # a cell of the barrier atlas


def barriers():
    """The pictures of the white (GDD, Blanc): a bubble (a crystal rim with faint hexagons, GDD:
    "hexagones sur le bras"), a wall (a band, stretched along its length), and what a hand is
    about to raise (a fainter rim)."""
    size = BARRIER
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    r = np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) / (size / 2)
    # Hexagons: the distance to the nearest centre of a hexagonal grid.
    scale = 18.0
    q = (x * math.sqrt(3) / 3 - y / 3) / scale
    rr = y * 2 / 3 / scale
    hex_edge = np.abs(((q + rr / 2) % 1) - 0.5) + np.abs(((rr) % 1) - 0.5)
    lines = np.clip(1 - np.abs(hex_edge - 0.5) * 8, 0, 1) * 0.25
    rim = np.exp(-((r - 0.96) / 0.035) ** 2)
    inner = np.clip(r, 0, 1) ** 4 * 0.35
    inside = r <= 1.0

    def bubble_cell(strength):
        alpha = np.where(inside, np.clip((rim + inner + lines * r) * strength, 0, 1), 0)
        cell = np.zeros((size, size, 4), np.uint8)
        cell[..., 0:3] = 240
        cell[..., 3] = (alpha * 255).astype(np.uint8)
        return cell

    band = np.abs(y - size / 2 + 0.5) / (size / 2)
    wall_alpha = np.clip(np.exp(-(band / 0.75) ** 6) * 0.75 + np.exp(-((band - 0.85) / 0.1) ** 2)
                         * 0.5 + lines * 0.6, 0, 1)
    wall = np.zeros((size, size, 4), np.uint8)
    wall[..., 0:3] = 236
    wall[..., 3] = (wall_alpha * 255).astype(np.uint8)
    sheet = np.concatenate([bubble_cell(1.0), wall, bubble_cell(0.45)], axis=1)
    return Image.fromarray(sheet, "RGBA")


def harvest():
    """The harvest (black and white together): a small black heart in a bubble of white rim, that
    swallows everything inside it and sorts it (GDD §7)."""
    hole = np.asarray(black_hole(), dtype=np.float64) / 255.0
    rim = np.asarray(barriers().crop((0, 0, BARRIER, BARRIER)).resize((HOLE, HOLE)),
                     dtype=np.float64) / 255.0
    y, x = np.mgrid[0:HOLE, 0:HOLE].astype(np.float64)
    r = np.hypot(x - HOLE / 2 + 0.5, y - HOLE / 2 + 0.5) / (HOLE / 2)
    # The black hole shrunk to a third, in the middle of the bubble.
    small = np.asarray(black_hole().resize((HOLE // 3, HOLE // 3), Image.LANCZOS),
                       dtype=np.float64) / 255.0
    core = np.zeros_like(hole)
    o = HOLE // 2 - HOLE // 6
    core[o:o + small.shape[0], o:o + small.shape[1]] = small
    alpha = np.clip(rim[..., 3] + core[..., 3], 0, 1)
    rgb = (rim[..., :3] * rim[..., 3:4] * (1 - core[..., 3:4]) + core[..., :3] * core[..., 3:4])
    rgb = rgb / np.maximum(alpha[..., None], 1e-6)
    out = np.dstack([np.clip(rgb, 0, 1), alpha[..., None]])
    del hole, r
    return Image.fromarray((out * 255).astype(np.uint8), "RGBA")


ROCK = 40


def rock(seed, colour=(70, 70, 74), light=(150, 150, 154)):
    """A rock (matter): a lumpy grey stone, lit from above."""
    rng = np.random.default_rng(seed)
    picture, draw = canvas(ROCK, ROCK)
    points = []
    for i in range(11):
        t = 2 * math.pi * i / 11
        radius = 15 * rng.uniform(0.75, 1.05)
        points.append(((ROCK / 2 + radius * math.cos(t)) * SUPER,
                       (ROCK / 2 + 3 + radius * 0.8 * math.sin(t)) * SUPER))
    draw.polygon(points, fill=colour + (255,))
    draw.ellipse([(ROCK / 2 - 9) * SUPER, (ROCK / 2 - 6) * SUPER, (ROCK / 2 + 5) * SUPER,
                  (ROCK / 2 - 1) * SUPER], fill=light + (255,))
    return reduce(picture.filter(ImageFilter.GaussianBlur(SUPER * 0.6)), ROCK, ROCK)


def fossil():
    """A fossil (carbon): a pale stone with the spiral of a shell."""
    picture = rock(9, colour=(118, 116, 108), light=(190, 188, 178))
    draw = ImageDraw.Draw(picture)
    cx, cy = ROCK / 2, ROCK / 2 + 3
    points = []
    for i in range(60):
        t = i / 59 * 3.2 * math.pi
        radius = 1.5 + t * 1.0
        points.append((cx + radius * math.cos(t), cy + radius * 0.8 * math.sin(t)))
    draw.line(points, fill=(70, 68, 62, 255), width=1)
    return picture


INVENTORY = 64


def inventory_bubbles():
    """What the body keeps (GDD §7, inventory): a bubble for each resource, drawn full; its size
    shows how much there is. Matter is black, carbon white, each in a thin white rim."""
    frames = []
    for fill in ((14, 14, 16), (236, 236, 232)):
        picture, draw = canvas(INVENTORY, INVENTORY)
        c = INVENTORY / 2
        oval(draw, c, c, 30, 30, (236, 236, 232, 200))
        oval(draw, c, c, 28, 28, fill + (255,))
        oval(draw, c - 9, c - 10, 7, 5, (255, 255, 255, 90))
        frames.append(reduce(picture, INVENTORY, INVENTORY))
    sheet = Image.new("RGBA", (INVENTORY * len(frames), INVENTORY), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.paste(frame, (index * INVENTORY, 0))
    return sheet


FONT_GLYPHS = (" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
               "abcdefghijklmnopqrstuvwxyz{|}~àâäçéèêëîïôöùûüÿœÀÉÈÊÇ«»…’")
FONT_W, FONT_H = 16, 32


def font():
    """The letters of the texts, each in a cell of FONT_W by FONT_H, white on clear."""
    from PIL import ImageFont
    face = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                              26 * SUPER)
    sheet = Image.new("RGBA", (FONT_W * len(FONT_GLYPHS), FONT_H), (0, 0, 0, 0))
    for index, glyph in enumerate(FONT_GLYPHS):
        cell = Image.new("L", (FONT_W * SUPER, FONT_H * SUPER), 0)
        ImageDraw.Draw(cell).text((FONT_W * SUPER / 2, FONT_H * SUPER * 0.55), glyph, fill=255,
                                  font=face, anchor="mm")
        alpha = cell.resize((FONT_W, FONT_H), Image.LANCZOS)
        letter = Image.new("RGBA", (FONT_W, FONT_H), (255, 255, 255, 0))
        letter.putalpha(alpha)
        sheet.paste(letter, (index * FONT_W, 0))
    return sheet


def save_font():
    os.makedirs(ART, exist_ok=True)
    font().save(os.path.join(ART, "font.png"), optimize=True)
    atlas = {"schema_version": 1, "name": "font", "image": "font.png", "cell_width": FONT_W,
             "cell_height": FONT_H, "filter": "linear", "glyphs": FONT_GLYPHS}
    with open(os.path.join(ART, "font.json"), "w", encoding="utf-8") as out:
        json.dump(atlas, out, indent=2, ensure_ascii=False)
        out.write("\n")


def glow():
    """A soft white glow, bright in the middle: the flash of the explosion."""
    size = 256
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    r = np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) / (size / 2)
    alpha = np.clip(1 - r, 0, 1) ** 2.2
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., 0:3] = 255
    rgba[..., 3] = (alpha * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def planet():
    """The planet of the tutorial seen from space: a grey disc lit from the upper left, with
    craters, its night side lost in the dark."""
    size = 512
    rng = np.random.default_rng(21)
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    cx = cy = size / 2 - 0.5
    r = np.hypot(x - cx, y - cy) / (size / 2 - 4)
    inside = r <= 1
    nz = np.sqrt(np.clip(1 - r ** 2, 0, 1))
    nx, ny = (x - cx) / (size / 2), (y - cy) / (size / 2)
    light = np.clip(-0.55 * nx - 0.6 * ny + 0.58 * nz, 0, 1)
    value = 30 + 170 * light ** 0.9
    for _ in range(28):
        px, py = rng.uniform(-0.8, 0.8, 2)
        pr = rng.uniform(0.03, 0.12)
        d = np.hypot(nx - px, ny - py) / pr
        value -= np.where(d < 1, 30 * (1 - d) * light, 0)
        value += np.where((d > 0.85) & (d < 1.1), 18 * light, 0)
    alpha = np.where(inside, np.clip((1 - r) * 120, 0, 1), 0)
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., 0] = np.clip(value, 0, 255)
    rgba[..., 1] = np.clip(value, 0, 255)
    rgba[..., 2] = np.clip(value * 1.04, 0, 255)
    rgba[..., 3] = (alpha * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def nebula():
    """A grey veil of dust across the stars: the universe once its colours are gone."""
    w, h = 1024, 576
    rng = np.random.default_rng(8)
    field = np.zeros((h, w))
    for scale, weight in ((128, 1.0), (64, 0.5), (32, 0.25), (16, 0.12)):
        small = rng.uniform(0, 1, (h // scale + 2, w // scale + 2))
        big = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize(
            (w + 2 * scale, h + 2 * scale), Image.BICUBIC), dtype=np.float64)[:h, :w] / 255
        field += big * weight
    field /= 1.87
    y = np.linspace(-1, 1, h)[:, None]
    x = np.linspace(-1, 1, w)[None, :]
    band = np.exp(-((y - 0.35 * x) / 0.45) ** 2)
    alpha = np.clip((field - 0.45) * 2.2, 0, 1) * band
    rgba = np.zeros((h, w, 4), np.uint8)
    rgba[..., 0:3] = 150
    rgba[..., 3] = (alpha * 140).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA").filter(ImageFilter.GaussianBlur(3))


def hero_curled():
    """The hero curled up asleep, as he drifts in his barrier: a ball of ovals, white and black."""
    picture, draw = canvas(CELL, CELL)
    c = CELL / 2
    oval(draw, c - 4, c + 6, 22, 16, BLACK, angle=20)
    oval(draw, c + 4, c + 4, 20, 15, WHITE, angle=20)
    oval(draw, c + 14, c - 12, 10, 10, WHITE)
    oval(draw, c + 10, c - 12, 6, 9, BLACK)
    oval(draw, c - 10, c + 14, 12, 6, BLACK, angle=-15)
    return reduce(picture, CELL, CELL)


def sky():
    """Black, with a few grey stars."""
    rng = np.random.default_rng(3)
    picture = Image.new("RGBA", (VIEW_W, VIEW_H), (4, 4, 6, 255))
    draw = ImageDraw.Draw(picture)
    for _ in range(220):
        x, y = rng.uniform(0, VIEW_W), rng.uniform(0, VIEW_H)
        light = int(rng.uniform(60, 200))
        r = rng.choice([0.6, 0.8, 1.2])
        draw.ellipse([x - r, y - r, x + r, y + r], fill=(light, light, light, 255))
    return picture


# ---------------------------------------------------------------------------------------------
# Scenes


def entity(name, x, y, components):
    return {"name": name, "components": {"transform2d.Position": {"x": x, "y": y}, **components}}


def hero_entity(x, y, locked=False):
    hero = entity("hero", x, y, {
        "sprite2d.Sprite": {"atlas": "hero", "width": CELL, "height": CELL,
                            "layer": LAYERS["hero"]},
        "sprite2d.Facing": {},
        "tag.Tag": {"tag": "hero"},
        "transform2d.Velocity": {},
        "collision2d.Collider": {"width": HERO_W, "height": HERO_H, "layer": ["hero"],
                                 "mask": ["ground", "barrier"]},
        "collision2d.Body": {},
        "collision2d.Blocked": {},
        "gravity2d.Gravity": {"y": 1800, "terminal_speed": 1100},
        "camera2d.CameraTarget": {},
        "action_control.Controlled": {"speed": 240, "vertical": False, "acceleration": 1600,
                                      "braking": 2400},
        # Jumping (GDD §5, D5): Space held makes a small black hole above his head that grows;
        # let go, he leaps, higher and less straight the bigger it was. Full, he takes off.
        "jump.Jumper": {"speed": 560, "charge_time": 1.2, "charge_low": 0.55,
                        "charge_high": 1.8, "wobble": 160, "full_action": "take_off"},
        # Dead, he does nothing until the checkpoint puts him back (D7).
        "toggle.ActiveBy": {"toggle": "dead", "inverse": True},
        "checkpoint.Saved": {},
        # Walking (GDD §5): he pulls his legs by gravity where his steps fall, at a small cost.
        "reserve.Drain": {"kind": "matter", "field": "transform2d.Velocity.x",
                          "per_unit": 0.002},
    })
    if locked:
        # He does not know how to walk yet (D4): the tutorial gives him move_x, then jump.
        hero["components"]["action_control.Controlled"]["action_x"] = "locked"
        hero["components"]["jump.Jumper"]["action"] = "locked"
    return hero


def jump_hole():
    """The small black hole above the hero's head while he charges a jump: its size follows the
    charge, and it costs matter while it is there."""
    return [
        entity("jump_hole", 0, 0, {
            "tag.Tag": {"tag": "jump_hole"},
            "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                                "layer": LAYERS["hero"] + 5, "unlit": True},
            "transform2d.Scale": {"factor": 0},
            "follow2d.Follow": {"target": "hero", "offset_y": -80, "stiffness": 1},
            # 0.15 matter a second for each unit of its radius: 3.6 a second when full.
            "reserve.Drain": {"kind": "matter", "field": "transform2d.Scale.factor",
                              "per_unit": 0.15},
        }),
        bind("jump_hole_grows", "hero", "jump.Jumper.charge", "transform2d.Scale.factor",
             0, 1, 0, 24, 1, onto="jump_hole"),
    ]


# What the lens adds at rest: a dark vignette, as in Limbo, a little glow and grain.
BASE_VIGNETTE = 0.45
BASE_VIGNETTE_SIZE = 0.45


def hands(unlocked=("black", "white"), offered=()):
    """The two hands (module loadout): the left one answers to the left button, the right one to
    the right button. Each is an arm beside the hero, coloured by its power (empty: grey). The
    kit says which powers work; black and white together make the harvest (GDD §6)."""
    kit = entity("kit", 0, 0, {"tag.Tag": {"tag": "kit"}, "loadout.Kit": {
        "unlocked": list(unlocked), "offered": list(offered), "together": 0.12}})
    combo = {"name": "harvest_combo", "components": {"loadout.Combo": {
        "a": "black", "b": "white", "result": "harvest"}}}
    made = [kit, combo]
    for name, slot, side in (("hand_left", 0, -1), ("hand_right", 1, 1)):
        made.append(entity(name, 0, 0, {
            "loadout.Hand": {"slot": slot, "trigger": name},
            "sprite2d.Sprite": {"atlas": "arm", "width": ARM, "height": ARM,
                                "layer": LAYERS["hero"] + side},
            "sprite2d.FrameBind": {"field": "loadout.Hand.item",
                                   "frame_count": len(ARM_COLOURS) + 1},
            # The left arm is the far one when he faces right, the near one when he faces left.
            "follow2d.Follow": {"target": "hero", "offset_x": side * 9, "offset_y": -6,
                                "stiffness": 1, "turn": True},
        }))
    return made


def powers():
    """The effects of the hands (loadout.Ability), each switched on while a hand uses it.

    The black (GDD, Noir): a black hole where the pointer is, within reach, that grows while the
    button is held and costs more matter the bigger it is. It pulls loose things, swallows what
    reaches its heart and digs the ground there: alone, it turns everything into matter."""
    hole = entity("black_hole", 0, 0, {
        "tag.Tag": {"tag": "black_hole"},
        "transform2d.Disabled": {},
        "loadout.Ability": {"power": "black", "kind": "matter", "per_use": 1},
        "aim2d.Aim": {"from": "hero", "range": 240},
        "attractor2d.Attractor": {"start_radius": 12, "grow": 45, "max_radius": 90,
                                  "shrink": 260, "pull": 1500, "pulls": ["rock"],
                                  "inner": 0.4, "convert": "matter"},
        # Matter a second for each unit of radius: 1.8 a second at 60.
        "reserve.Drain": {"kind": "matter", "field": "attractor2d.Attractor.radius",
                          "per_unit": 0.03},
        "terrain2d.Digger": {"radius": 0, "layers": ["ground"]},
        "reserve.Gain": {"kind": "matter", "field": "terrain2d.Digger.last", "per_unit": 0.08},
        "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                            "layer": LAYERS["hero"] + 5, "unlit": True},
        "transform2d.Scale": {"factor": 0},
    })
    # The white (GDD, Blanc): pressed, a bubble grows where the pointer was; dragged further than
    # wall_drag, it becomes a wall to the pointer. Let go, the barrier is raised and paid in carbon
    # for its size; three at most (the oldest gives way).
    white = entity("white_caster", 0, 0, {
        "tag.Tag": {"tag": "white_caster"},
        "transform2d.Disabled": {},
        "loadout.Ability": {"power": "white"},
        "aim2d.Aim": {"from": "hero", "range": 260, "lock": True},
        "barrier2d.Caster": {"pool": 0, "start_radius": 18, "grow": 70, "max_radius": 140,
                             "wall_drag": 50, "thickness": 12, "kind": "carbon",
                             "per_unit": 0.06, "atlas": "barrier", "preview": 2,
                             "layer": LAYERS["hero"] + 4},
    })
    # The harvest (GDD §7): black and white together, or the harvest key alone. A bubble grows
    # where the pointer is; everything inside it is swallowed and sorted, and comes into the
    # reserves when the harvest ends. It costs matter and carbon while it lasts.
    gather = entity("harvest_bubble", 0, 0, {
        "tag.Tag": {"tag": "harvest_bubble"},
        "transform2d.Disabled": {},
        "loadout.Ability": {"power": "harvest", "kind": "carbon", "per_second": 0.6},
        "aim2d.Aim": {"from": "hero", "range": 240},
        "attractor2d.Attractor": {"start_radius": 14, "grow": 36, "max_radius": 80,
                                  "shrink": 300, "pull": 300, "pulls": ["rock"], "inner": 1,
                                  "deferred": True},
        "reserve.Drain": {"kind": "matter", "per_second": 0.3,
                          "field": "attractor2d.Attractor.radius", "per_unit": 0.01},
        "terrain2d.Digger": {"radius": 0, "layers": ["ground"]},
        "reserve.Gain": {"kind": "matter", "field": "terrain2d.Digger.last", "per_unit": 0.08},
        "sprite2d.Sprite": {"atlas": "harvest", "width": 2, "height": 2,
                            "layer": LAYERS["hero"] + 5, "unlit": True},
        "transform2d.Scale": {"factor": 0},
    })
    shortcut = {"name": "harvest_key", "components": {"loadout.Shortcut": {
        "action": "harvest", "first": "black", "second": "white"}}}
    walls = [entity(f"barrier_{index}", 0, 0, {"barrier2d.Barrier": {
        "pool": 0, "layer": ["barrier"], "freezes": ["rock"], "atlas": "barrier", "bubble": 0,
        "wall": 1, "draw_layer": LAYERS["hero"] + 4}}) for index in (1, 2, 3)]
    return [
        white,
        bind("white_reaches_x", "white_caster", "aim2d.Aim.reach_x", "barrier2d.Caster.end_x",
             -100000, 100000, -100000, 100000, 1, onto="white_caster"),
        bind("white_reaches_y", "white_caster", "aim2d.Aim.reach_y", "barrier2d.Caster.end_y",
             -100000, 100000, -100000, 100000, 1, onto="white_caster"),
    ] + walls + [
        gather, shortcut,
        # The harvest leaves a hole (GDD §7), half as wide as its bubble.
        bind("harvest_digs", "harvest_bubble", "attractor2d.Attractor.heart",
             "terrain2d.Digger.radius", 0, 1000, 0, 500, 1, onto="harvest_bubble"),
        bind("harvest_shows", "harvest_bubble", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 1000, 0, 1000, 1, onto="harvest_bubble"),
        hole,
        bind("black_hole_digs", "black_hole", "attractor2d.Attractor.heart",
             "terrain2d.Digger.radius", 0, 1000, 0, 1000, 1, onto="black_hole"),
        bind("black_hole_shows", "black_hole", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 1000, 0, 1000, 1, onto="black_hole"),
    ]


def rock_entity(name, x, y, kind="matter", amount=12, atlas="rock"):
    """A loose rock on the ground: it falls, a black hole pulls and swallows it."""
    return entity(name, x, y, {
        "sprite2d.Sprite": {"atlas": atlas, "width": ROCK, "height": ROCK,
                            "layer": LAYERS["hero"] - 2},
        "transform2d.Velocity": {},
        "collision2d.Collider": {"width": 26, "height": 22, "layer": ["rock"],
                                 "mask": ["ground", "barrier"]},
        "collision2d.Body": {},
        "collision2d.Blocked": {},
        "gravity2d.Gravity": {"y": 1800, "terminal_speed": 1100},
        "attractor2d.Absorbable": {"mass": 10},
        "reserve.Yield": {"kind": kind, "amount": amount},
    })


def inventory():
    """The inventory (GDD §7): the inventory key shows, around the body, a bubble for the matter
    and one for the carbon, as big as there is of it."""
    made = [entity("inventory", 0, 0, {"tag.Tag": {"tag": "inventory"},
                                       "toggle.Toggle": {"action": "inventory"}})]
    for index, (kind, side) in enumerate((("matter", -1), ("carbon", 1))):
        name = f"{kind}_bubble"
        made.append(entity(name, 0, 0, {
            "tag.Tag": {"tag": name},
            "transform2d.Hidden": {},
            "toggle.ShownBy": {"toggle": "inventory"},
            "sprite2d.Sprite": {"atlas": "inventory", "frame": index, "width": INVENTORY,
                                "height": INVENTORY, "layer": LAYERS["hero"] + 6,
                                "unlit": True},
            "transform2d.Scale": {"factor": 1},
            "follow2d.Follow": {"target": "hero", "offset_x": side * 34, "offset_y": -62,
                                "stiffness": 0.3},
        }))
        made.append(bind(f"{kind}_fills", kind, "reserve.Reserve.fill",
                         "transform2d.Scale.factor", 0, 1, 0.15, 1, 0.5, onto=name))
    return made


def camera(bound_right):
    return entity("camera", VIEW_W / 2, VIEW_H / 2, {
        "tag.Tag": {"tag": "camera"},
        "camera2d.Camera": {"zoom": 1},
        "camera2d.CameraShake": {"strength": 0, "decay": 3, "speed": 30},
        # Brought back to life, the picture comes back from black.
        "spawner2d.Shooter": {"prefab": "fade_back", "action": "revive"},
        "postfx.Effects": {"vignette": BASE_VIGNETTE, "vignette_size": BASE_VIGNETTE_SIZE,
                           "edge_glow": 0, "edge_glow_size": 0.5, "bloom": 0.25,
                           "grain": 0.08},
        "camera2d.CameraFollow": {"stiffness": 0.1, "dead_x": 40, "dead_y": 120,
                                  "bounded": True, "bound_left": 0, "bound_right": bound_right,
                                  "bound_top": -2000, "bound_bottom": GROUND_Y + 120},
    })


def sky_entity():
    return entity("sky", 0, 0, {
        "sprite2d.Sprite": {"atlas": "sky", "width": VIEW_W, "height": VIEW_H,
                            "layer": LAYERS["sky"]},
        "sprite2d.Parallax": {"x": 0, "y": 0},
    })


TERRAIN_CELL = 8  # side of a cell of the ground, in world units


def terrain_entity(name, left, top, width, height, surface_y, extra=None):
    """A ground that can be dug (terrain2d): its grid covers width by height from (left, top),
    solid under surface_y."""
    components = {
        "terrain2d.Terrain": {"columns": int(width // TERRAIN_CELL),
                              "rows": int(height // TERRAIN_CELL), "cell": TERRAIN_CELL,
                              "layer": ["ground"],
                              "surface": (surface_y - top) / TERRAIN_CELL},
        "terrain2d.Look": {"layer": LAYERS["ground"], "pixels": 4, "red": 16, "green": 16,
                           "blue": 18, "top_red": 150, "top_green": 150, "top_blue": 156,
                           "lip": 12, "grain": 0.06},
    }
    if extra:
        components["terrain2d.Terrain"].update(extra)
    return entity(name, left, top, components)


def shape(name, x, y, width, height, fill):
    return entity(name, x, y, {"terrain2d.Shape": {"width": width, "height": height,
                                                   "fill": fill}})


def sandbox():
    """The test ground of the modules: flat, wide, nothing on it."""
    return [sky_entity(), terrain_entity("ground", 0, 0, SANDBOX_W, GROUND_Y + 200, GROUND_Y),
            # A pit on the left of the hero, a low step and a wall on his right: what the modules
            # are tried on.
            shape("pit", 232, GROUND_Y + 80, 64, 160, False),
            shape("step", 800, GROUND_Y - 4, 200, 8, True),
            shape("wall", 1250, GROUND_Y - 60, 32, 120, True),
            rock_entity("rock_1", 560, GROUND_Y - 11), rock_entity("rock_2", 640, GROUND_Y - 11),
            # A rock high up, that falls as the scene starts.
            rock_entity("falling_rock", 480, 200),
            rock_entity("fossil_1", 330, GROUND_Y - 11, kind="carbon", amount=15,
                        atlas="fossil"),
            hero_entity(400, GROUND_Y - HERO_H / 2), camera(SANDBOX_W)] + hands() + powers() \
        + inventory() + jump_hole() + death() + reserves()


def sandbox_low():
    """The test ground with little black and white left: the edges of the screen show it."""
    return sandbox()[:-6] + reserves(matter=20, carbon=8)


# Life (GDD §8): black (matter) and white (carbon) wear away to keep the body whole; full, they
# last about five minutes (P8). With less than half of either, the edges of the screen turn: white
# when the matter runs out, black when the carbon does, until the whole screen is gone.
LIFE_DRAIN = 100 / 300


def reserves(matter=100, carbon=100):
    return [
        # Out of matter, the screen goes white (GDD §8: he can no longer move); out of carbon,
        # black (his own black hole takes him). Either way the step starts again (D7).
        entity("matter", 0, 0, {"tag.Tag": {"tag": "matter"}, "checkpoint.Saved": {},
                                "reserve.Reserve": {
            "kind": "matter", "amount": matter, "max": 100, "drain": LIFE_DRAIN,
            "empty_action": "out_of_matter"},
            "spawner2d.Shooter": {"prefab": "fade_white", "action": "out_of_matter"}}),
        entity("carbon", 0, 0, {"tag.Tag": {"tag": "carbon"}, "checkpoint.Saved": {},
                                "reserve.Reserve": {
            "kind": "carbon", "amount": carbon, "max": 100, "drain": LIFE_DRAIN,
            "empty_action": "out_of_carbon"},
            "spawner2d.Shooter": {"prefab": "fade_black", "action": "out_of_carbon"}}),
        bind("carbon_darkens", "carbon", "reserve.Reserve.fill", "postfx.Effects.vignette",
             0.5, 0, BASE_VIGNETTE, 1, 1.5),
        bind("carbon_closes", "carbon", "reserve.Reserve.fill", "postfx.Effects.vignette_size",
             0.5, 0, BASE_VIGNETTE_SIZE, -3, 4),
        bind("matter_whitens", "matter", "reserve.Reserve.fill", "postfx.Effects.edge_glow",
             0.5, 0, 0, 1, 1.5),
        bind("matter_closes", "matter", "reserve.Reserve.fill", "postfx.Effects.edge_glow_size",
             0.5, 0, 0.5, -3, 4),
    ]


def death(watch_story=False):
    """Dying (GDD §8, D7): an action of a reserve that runs out sets the dead toggle (the hero
    stops) and starts a fade; at its end, revive puts back what the checkpoint kept and the
    picture comes back."""
    def rule(name, action, value):
        return {"name": name, "components": {"rules.OnAction": {
            "action": action, "source": "dead", "field": "toggle.Toggle.on", "value": value}}}
    return [
        entity("dead", 0, 0, {"tag.Tag": {"tag": "dead"}, "toggle.Toggle": {}}),
        rule("dies_white", "out_of_matter", 1),
        rule("dies_black", "out_of_carbon", 1),
        rule("revives", "revive", 0),
        entity("checkpoint", 0, 0, {"checkpoint.Checkpoint": dict(
            {"load": "revive"},
            # In the tutorial, each new step is a point to start again from.
            **({"watch": "phase.Phase.current", "source": "story"} if watch_story else {}))}),
    ]


def fade(name, colour, ticks, start, end, action=None):
    """A prefab that covers the screen with a colour, from start to end of its opacity."""
    tween = {"field": "sprite2d.Sprite.alpha", "from": start, "to": end, "ticks": ticks,
             "easing": 3, "despawn_when_done": True}
    if action:
        tween.update({"use_action": True, "action": action})
    return {"schema_version": 1, "name": name, "components": {
        "transform2d.Position": {"x": VIEW_W / 2, "y": VIEW_H / 2},
        "sprite2d.Sprite": {"atlas": "plain", "width": 20000, "height": 20000, "screen": True,
                            "red": colour[0], "green": colour[1], "blue": colour[2],
                            "alpha": start, "unlit": True, "layer": 1000},
        "tween2d.Tween": tween,
    }}


def write_prefabs():
    os.makedirs(PREFABS, exist_ok=True)
    for prefab in (fade("fade_white", (240, 240, 236), 50, 0, 255, "revive"),
                   fade("fade_black", (0, 0, 0), 50, 0, 255, "revive"),
                   fade("fade_back", (0, 0, 0), 40, 255, 0)):
        with open(os.path.join(PREFABS, prefab["name"] + ".json"), "w", encoding="utf-8") as out:
            out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
            json.dump(prefab, out, indent=2)
            out.write("\n")


def bind(name, source_tag, source, target, low, high, out_low, out_high, power,
         onto="camera"):
    """A number of an entity (the camera by default) that follows a number of another one."""
    return {"name": name, "components": {
        "bind.Bind": {"source": source, "from": source_tag, "target": target, "onto": onto,
                      "low": low, "high": high, "out_low": out_low, "out_high": out_high,
                      "power": power}}}


# ---------------------------------------------------------------------------------------------
# The tutorial (docs/complete_universe/TUTORIEL.md): one planet in the great barrier, played flat.

TUTORIAL_W = 5600  # the planet's ground runs from x 0 to here
TUTORIAL_TOP = -1000  # the ground's grid starts this high (walls at both ends go up to it)
LANDING_X = 600
WALK_TO = 1600  # step 6 ends here
GAP = (1800, 1900)  # step 7: a pit to jump over...
LEDGE = (2100, 2600, 110)  # ...then a ledge this high
BASIN = (2700, 4000, 80)  # steps 8 and 9: a basin with fossils, then rocks

STEPS = ["landing", "hands", "black", "white", "together", "inventory", "harvest", "walk", "jump",
         "lack_white", "lack_black", "departure", "end"]

# The texts of the controls (GDD §12: text is only for the controls), shown during each step.
TEXTS = {
    "hands": "Maj + clic gauche ou droit : mettre une couleur dans une main",
    "black": "Main noire, clic maintenu : un trou noir là où tu vises",
    "white": "Main blanche : clic, une bulle. Clic glissé, une barrière",
    "together": "Les deux clics ensemble : récolter",
    "inventory": "Tab : l'inventaire",
    "harvest": "F maintenu : récolter là où tu vises",
    "walk": "Q / D ou flèches : marcher",
    "jump": "Espace maintenu, puis relâché : sauter",
    "lack_white": "F : récolter du carbone",
    "lack_black": "F : récolter de la matière",
    "departure": "Main noire vers le ciel, clic maintenu",
}


def step_text(step):
    return entity(f"text_{step}", VIEW_W / 2, VIEW_H - 56, {
        "text2d.Text": {"string": f"tutorial.{step}", "font": "font", "size": 28,
                        "pivot_x": 0.5, "pivot_y": 0.5, "screen": True, "alpha": 225},
        "phase.ShowIn": {"phases": [step]},
        "transform2d.Hidden": {},
    })


def watch(name, step, source, field, action, low=None, high=None, inside=False):
    """Raises action when the number field of the tagged entity comes into [low, high], only
    during step (it is not watched in the other steps)."""
    rule = {"source": source, "own": False, "field": field, "action": action}
    if low is not None:
        rule["min"] = low
    if high is not None:
        rule["max"] = high
    if inside:
        # Already in the range when the step starts: it must leave it and come back.
        rule["inside"] = True
    return {"name": name, "components": {"rules.Watch": rule,
                                         "phase.ActiveIn": {"phases": [step]},
                                         "transform2d.Disabled": {}}}


def on(name, action, source, field, value):
    return {"name": name, "components": {"rules.OnAction": {
        "action": action, "source": source, "field": field, "value": value}}}


def story():
    """The steps of the tutorial (module phase) and what ends each one (rules.Watch)."""
    made = [entity("story", 0, 0, {"tag.Tag": {"tag": "story"},
                                   "phase.Phase": {"current": "landing"}})]
    ends = [
        ("landing", "landed"), ("hands", "hands_ready"), ("black", "black_done"),
        ("white", "white_done"), ("together", "together_done"),
        ("inventory", "inventory_done"), ("harvest", "harvest_done"), ("walk", "walk_done"),
        ("jump", "jump_done"), ("lack_white", "white_ok"), ("lack_black", "black_ok"),
        ("departure", "departed"),
    ]
    for index, (step, action) in enumerate(ends):
        made.append({"name": f"after_{step}", "components": {"phase.Transition": {
            "action": action, "from": [step], "from_any": False, "to": STEPS[index + 1]}}})
    # 1. The landing (the opening comes later, U11): a moment, then the hands.
    made.append(entity("landing_timer", 0, 0, {"tween2d.Tween": {
        "field": "transform2d.Position.x", "from": 0, "to": 1, "ticks": 90,
        "use_action": True, "action": "landed"}}))
    made += [
        # 2. Black and white in the hands (bits 0 and 1 of the held powers: 3).
        watch("hands_watch", "hands", "kit", "loadout.Kit.held", "hands_ready", 3, 3),
        # 3. The black swallows a rock; the white raises a barrier; both harvest.
        watch("black_watch", "black", "black_hole", "attractor2d.Attractor.swallowed",
              "black_done", 1),
        watch("white_watch", "white", "white_caster", "barrier2d.Caster.raised", "white_done",
              1),
        watch("together_watch", "together", "harvest_bubble", "loadout.Ability.uses",
              "together_done", 1),
        # 4. The inventory opened.
        watch("inventory_watch", "inventory", "inventory", "toggle.Toggle.times",
              "inventory_done", 1),
        # 5. The fossil harvested: its carbon came in.
        watch("harvest_watch", "harvest", "carbon", "reserve.Reserve.gained", "harvest_done",
              15),
        on("learns_walking", "harvest_done", "hero", "action_control.Controlled.action_x", 0),
        # 6. Walking to the end of the path; 7. jumping the pit and onto the ledge.
        watch("walk_watch", "walk", "hero", "transform2d.Position.x", "walk_done", WALK_TO),
        on("learns_jumping", "walk_done", "hero", "jump.Jumper.action", 1),
        watch("jump_watch", "jump", "hero", "transform2d.Position.x", "jump_done",
              LEDGE[0] + 60),
        # 8. The white runs low and fast: harvest carbon back above half.
        on("white_falls", "jump_done", "carbon", "reserve.Reserve.amount", 25),
        on("white_drains", "jump_done", "carbon", "reserve.Reserve.drain", 1.2),
        watch("white_watch_back", "lack_white", "carbon", "reserve.Reserve.amount", "white_ok",
              50, inside=True),
        on("white_steadies", "white_ok", "carbon", "reserve.Reserve.drain", LIFE_DRAIN),
        # 9. Then the black.
        on("black_falls", "white_ok", "matter", "reserve.Reserve.amount", 25),
        on("black_drains", "white_ok", "matter", "reserve.Reserve.drain", 1.2),
        watch("black_watch_back", "lack_black", "matter", "reserve.Reserve.amount", "black_ok",
              50, inside=True),
        on("black_steadies", "black_ok", "matter", "reserve.Reserve.drain", LIFE_DRAIN),
        # 10. A black hole as big as it gets, towards the sky: the departure (its film: U11).
        watch("departure_watch", "departure", "black_hole", "attractor2d.Attractor.radius",
              "departed", 85),
    ]
    made += [step_text(step) for step in TEXTS]
    return made


def tutorial():
    """The planet of the tutorial, from left to right: the landing, the path, the basin, the
    edge under the top of the dome."""
    height = 2000
    ground = terrain_entity("ground", 0, TUTORIAL_TOP, TUTORIAL_W, height, GROUND_Y,
                            # Craters dug at the landing must not trap a hero who cannot jump yet:
                            # their rounded sides can be walked up.
                            {"amplitude": 1.2, "wavelength": 40, "seed": 3, "climb": 20})
    shapes = [
        # Walls at both ends, up to the top of the grid: the barrier holds the planet.
        shape("wall_left", 32, (TUTORIAL_TOP + GROUND_Y) / 2, 64, GROUND_Y - TUTORIAL_TOP, True),
        shape("wall_right", TUTORIAL_W - 32, (TUTORIAL_TOP + GROUND_Y) / 2, 64,
              GROUND_Y - TUTORIAL_TOP, True),
        # The landing is flat.
        shape("landing_floor", 700, GROUND_Y + 40, 800, 80, True),
        shape("landing_air", 700, GROUND_Y - 100, 800, 200, False),
        # The pit, then the ledge.
        shape("pit", (GAP[0] + GAP[1]) / 2, GROUND_Y + 90, GAP[1] - GAP[0], 180, False),
        shape("ledge", (LEDGE[0] + LEDGE[1]) / 2, GROUND_Y - LEDGE[2] / 2 - 4,
              LEDGE[1] - LEDGE[0], LEDGE[2] + 8, True),
        # The basin.
        shape("basin", (BASIN[0] + BASIN[1]) / 2, GROUND_Y + BASIN[2] / 2 - 8,
              BASIN[1] - BASIN[0], BASIN[2] + 16, False),
    ]
    basin_floor = GROUND_Y + BASIN[2]
    things = [
        rock_entity("rock_b", 480, GROUND_Y - 11),
        rock_entity("rock_a", 760, GROUND_Y - 11),
        rock_entity("fossil_landing", 380, GROUND_Y - 11, kind="carbon", amount=15,
                    atlas="fossil"),
    ]
    for index, x in enumerate((2850, 3000, 3150, 3300)):
        things.append(rock_entity(f"fossil_{index + 1}", x, basin_floor - 11, kind="carbon",
                                  amount=20, atlas="fossil"))
    for index, x in enumerate((3500, 3650, 3800, 3950)):
        things.append(rock_entity(f"rock_{index + 1}", x, basin_floor - 11, amount=20))
    return ([sky_entity(), ground] + shapes + things +
            [hero_entity(LANDING_X, 220, locked=True),
             camera(TUTORIAL_W)] + hands() + powers() + inventory() + jump_hole() +
            death(watch_story=True) + reserves(matter=70, carbon=60) + story() + landing() +
            departure())


def landing():
    """The start of the tutorial: out of the white of the opening, he falls to the ground."""
    return [
        entity("whitein", VIEW_W / 2, VIEW_H / 2, {"tag.Tag": {"tag": "whitein"},
                                                  "sprite2d.Sprite": {
            "atlas": "plain", "width": 20000, "height": 20000, "screen": True, "alpha": 255,
            "unlit": True, "layer": 1000}}),
        track("whitein_fades", "whitein", "sprite2d.Sprite.alpha", [(0, 255), (70, 0)]),
        # The great barrier, high above: a dome over the whole planet.
        entity("dome", 0, 2400, {"tag.Tag": {"tag": "dome"}, "sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 5400, "height": 5400, "layer": -90,
            "alpha": 150, "unlit": True}, "sprite2d.Parallax": {"x": 0.04, "y": 0.04}}),
    ]


def departure():
    """The end of the tutorial (GDD §12, 10): the black hole he raised slips out of his hands. It
    grows and grows, the ground shakes and is torn up, the dome melts, and everything goes white:
    the planet is gone."""
    return [
        entity("runaway", 0, 0, {
            "tag.Tag": {"tag": "runaway"},
            "transform2d.Disabled": {},
            "phase.ActiveIn": {"phases": ["end"]},
            "follow2d.Follow": {"target": "hero", "offset_y": -200, "stiffness": 0.04},
            "attractor2d.Attractor": {"start_radius": 90, "grow": 150, "max_radius": 1600,
                                      "pull": 2600, "pulls": ["rock"], "inner": 0.45},
            "terrain2d.Digger": {"radius": 0, "layers": ["ground"]},
            "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                                "layer": LAYERS["hero"] + 7, "unlit": True},
            "transform2d.Scale": {"factor": 0},
        }),
        bind("runaway_digs", "runaway", "attractor2d.Attractor.heart",
             "terrain2d.Digger.radius", 0, 5000, 0, 5000, 1, onto="runaway"),
        bind("runaway_shows", "runaway", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 5000, 0, 5000, 1, onto="runaway"),
        on("ground_shakes", "departed", "camera", "camera2d.CameraShake.strength", 14),
        track("dome_melts", "dome", "sprite2d.Sprite.alpha", [(60, 150), (300, 0)],
              start="departed"),
        entity("blast", VIEW_W / 2, VIEW_H / 2, {"tag.Tag": {"tag": "blast"},
                                                "sprite2d.Sprite": {
            "atlas": "plain", "width": 20000, "height": 20000, "screen": True, "alpha": 0,
            "unlit": True, "layer": 1000}}),
        track("blast_comes", "blast", "sprite2d.Sprite.alpha", [(240, 0), (330, 255)],
              start="departed", end_action="exploded"),
        entity("to_drift", 0, 0, {"scene_switch.GoToScene": {
            "scene": "drift", "use_action": True, "action": "exploded"}}),
    ]


def track(name, onto, field, keys, start=None, end_action=None, smooth=True):
    """A number of the tagged entity played along (tick, value) keys (module tween2d)."""
    made = {"field": field, "onto": onto, "smooth": smooth, "keys": len(keys)}
    for index, (tick, value) in enumerate(keys):
        made[f"t{index}"] = tick
        made[f"v{index}"] = value
    if start:
        made["start"] = start
    if end_action:
        made["end_action"] = end_action
    return {"name": name, "components": {"tween2d.Track": made}}


# The colours of the seven fragments, and the order they go out after the explosion (GDD §2:
# "plein de couleurs, puis de moins en moins").
FRAGMENT_COLOURS = [("pink", (240, 130, 190)), ("violet", (150, 90, 210)),
                    ("orange", (240, 140, 50)), ("red", (220, 60, 50)),
                    ("yellow", (240, 210, 80)), ("green", (80, 190, 110)),
                    ("blue", (70, 130, 230))]
PLANET_AT = (700, -120)
OPENING_TICKS = 840


def opening():
    """The opening (GDD §2): an explosion, the universe spreads out full of colours, fewer and
    fewer, down to black and white and a sky like ours. The camera finds the hero curled up in an
    immense barrier, far, then drifting down to a planet, and comes close: white, and the game."""
    made = [
        entity("backdrop", 0, 0, {"sprite2d.Sprite": {
            "atlas": "plain", "width": 40000, "height": 40000, "red": 0, "green": 0, "blue": 0,
            "layer": -120, "unlit": True}}),
        entity("stars", 0, 0, {"tag.Tag": {"tag": "stars"}, "sprite2d.Sprite": {
            "atlas": "sky", "width": VIEW_W, "height": VIEW_H, "layer": -110, "alpha": 0,
            "unlit": True}, "sprite2d.Parallax": {"x": 0, "y": 0}}),
        entity("nebula", 200, 0, {"tag.Tag": {"tag": "nebula"}, "sprite2d.Sprite": {
            "atlas": "nebula", "width": 2600, "height": 1460, "layer": -105, "alpha": 0,
            "unlit": True}, "sprite2d.Parallax": {"x": 0.25, "y": 0.25}}),
        entity("flash", 0, 0, {"tag.Tag": {"tag": "flash"}, "sprite2d.Sprite": {
            "atlas": "glow", "width": 300, "height": 300, "layer": 50, "additive": True,
            "unlit": True}, "transform2d.Scale": {"factor": 0.2}}),
        entity("camera", 0, 0, {"tag.Tag": {"tag": "camera"}, "camera2d.Camera": {"zoom": 1.3},
                                "postfx.Effects": {"bloom": 0.7, "vignette": 0.5,
                                                   "vignette_size": 0.4, "grain": 0.08}}),
        entity("great_barrier", *PLANET_AT, {"tag.Tag": {"tag": "great_barrier"},
                                             "sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 820, "height": 820, "layer": 20,
            "alpha": 0, "unlit": True}}),
        entity("planet", PLANET_AT[0], PLANET_AT[1] + 70, {"tag.Tag": {"tag": "planet"},
                                                          "sprite2d.Sprite": {
            "atlas": "planet", "width": 300, "height": 300, "layer": 10, "alpha": 0,
            "unlit": True}}),
        entity("sleeper", 470, -330, {"tag.Tag": {"tag": "sleeper"}, "sprite2d.Sprite": {
            "atlas": "hero_curled", "width": 40, "height": 40, "layer": 30, "alpha": 0,
            "unlit": True}}),
        entity("whiteout", VIEW_W / 2, VIEW_H / 2, {"tag.Tag": {"tag": "whiteout"},
                                                   "sprite2d.Sprite": {
            "atlas": "plain", "width": 20000, "height": 20000, "screen": True, "alpha": 0,
            "unlit": True, "layer": 1000}}),
        # The end, or a press of Escape: the planet.
        entity("to_tutorial", 0, 0, {"scene_switch.GoToScene": {
            "scene": "tutorial", "after_ticks": OPENING_TICKS, "use_action": True,
            "action": "skip"}}),
    ]
    # The explosion: every colour flies out, and goes out in turn; the grey stays longest.
    for index, (name, colour) in enumerate(FRAGMENT_COLOURS + [("grey", (200, 200, 200))]):
        made.append(entity(f"burst_{name}", 0, 0, {"particles2d.Emitter": {
            "rate": 0, "burst": 140, "lifetime": 2.0 + 0.6 * index if name != "grey" else 9,
            "lifetime_spread": 0.4, "speed": 420, "speed_spread": 300, "spread": 360,
            "drag": 0.45, "size": 9, "end_size": 4, "red": colour[0], "green": colour[1],
            "blue": colour[2], "alpha": 255, "fade": True, "additive": True, "unlit": True,
            "layer": 40, "max": 160}}))
    made += [
        track("flash_grows", "flash", "transform2d.Scale.factor", [(0, 0.2), (30, 9)]),
        track("flash_fades", "flash", "sprite2d.Sprite.alpha", [(0, 255), (70, 0)]),
        track("stars_come", "stars", "sprite2d.Sprite.alpha", [(160, 0), (420, 255)]),
        track("dust_comes", "nebula", "sprite2d.Sprite.alpha", [(280, 0), (520, 200)]),
        # The camera: back from the explosion, still, then over to the barrier and down to it.
        track("camera_zoom", "camera", "camera2d.Camera.zoom",
              [(0, 1.3), (220, 0.55), (420, 0.55), (560, 0.75), (760, 2.6)]),
        track("camera_x", "camera", "transform2d.Position.x",
              [(420, 0), (600, PLANET_AT[0]), (760, PLANET_AT[0] - 10)]),
        track("camera_y", "camera", "transform2d.Position.y",
              [(420, 0), (600, PLANET_AT[1]), (760, PLANET_AT[1] - 90)]),
        track("barrier_shows", "great_barrier", "sprite2d.Sprite.alpha", [(430, 0), (540, 210)]),
        track("planet_shows", "planet", "sprite2d.Sprite.alpha", [(430, 0), (520, 255)]),
        track("sleeper_shows", "sleeper", "sprite2d.Sprite.alpha", [(470, 0), (540, 255)]),
        track("sleeper_x", "sleeper", "transform2d.Position.x",
              [(480, 470), (780, PLANET_AT[0] - 12)]),
        track("sleeper_y", "sleeper", "transform2d.Position.y",
              [(480, -330), (780, PLANET_AT[1] - 82)]),
        track("white_comes", "whiteout", "sprite2d.Sprite.alpha", [(730, 0), (820, 255)]),
    ]
    return made


def drift():
    """After the tutorial (its end, until the space of the next steps): the hero curled up again,
    drifting among what is left of the planet; 'À suivre'."""
    return [
        entity("backdrop", 0, 0, {"sprite2d.Sprite": {
            "atlas": "plain", "width": 40000, "height": 40000, "red": 0, "green": 0, "blue": 0,
            "layer": -120, "unlit": True}}),
        sky_entity(),
        entity("nebula", 0, 0, {"sprite2d.Sprite": {
            "atlas": "nebula", "width": 2600, "height": 1460, "layer": -105, "alpha": 200,
            "unlit": True}, "sprite2d.Parallax": {"x": 0.25, "y": 0.25}}),
        entity("camera", 0, 0, {"tag.Tag": {"tag": "camera"}, "camera2d.Camera": {"zoom": 1.6},
                                "postfx.Effects": {"bloom": 0.5, "vignette": 0.55,
                                                   "vignette_size": 0.35, "grain": 0.08}}),
        entity("sleeper", -40, 10, {"tag.Tag": {"tag": "sleeper"}, "sprite2d.Sprite": {
            "atlas": "hero_curled", "width": 60, "height": 60, "layer": 30, "unlit": True},
            "transform2d.Velocity": {"x": 6, "y": -2}, "transform2d.Rotation": {"degrees": 0},
            "transform2d.Spin": {"speed": 4}} if False else {"tag.Tag": {"tag": "sleeper"},
            "sprite2d.Sprite": {"atlas": "hero_curled", "width": 60, "height": 60,
                                "layer": 30, "unlit": True},
            "transform2d.Velocity": {"x": 6, "y": -2}}),
        # What is left of the planet: grey dust flying out.
        entity("debris", -300, 120, {"particles2d.Emitter": {
            "rate": 40, "lifetime": 7, "speed": 60, "speed_spread": 40, "angle": -160,
            "spread": 60, "size": 4, "end_size": 2, "red": 170, "green": 170, "blue": 170,
            "alpha": 220, "fade": True, "unlit": True, "layer": 20, "area_width": 160,
            "area_height": 160, "max": 300}}),
        entity("to_follow", VIEW_W / 2, VIEW_H - 120, {"tag.Tag": {"tag": "to_follow"},
                                                      "text2d.Text": {
            "string": "tutorial.next", "font": "font", "size": 30, "pivot_x": 0.5,
            "pivot_y": 0.5, "screen": True, "alpha": 0}}),
        track("to_follow_shows", "to_follow", "text2d.Text.alpha", [(120, 0), (240, 230)]),
    ]


def write_strings():
    os.makedirs(TEXT, exist_ok=True)
    entries = {f"tutorial.{step}": text for step, text in TEXTS.items()}
    entries["tutorial.next"] = "À suivre…"
    with open(os.path.join(TEXT, "fr.json"), "w", encoding="utf-8") as out:
        json.dump({"schema_version": 1, "locale": "fr", "entries": entries}, out, indent=2,
                  ensure_ascii=False)
        out.write("\n")


def write_scene(name, entities):
    os.makedirs(SCENES, exist_ok=True)
    with open(os.path.join(SCENES, name + ".json"), "w", encoding="utf-8") as out:
        out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
        json.dump({"schema_version": 1, "name": name, "entities": entities}, out, indent=2)
        out.write("\n")


def main():
    save(hero(), "hero")
    save(sky(), "sky")
    save(arms(), "arm", cell=(ARM, ARM))
    save(black_hole(), "black_hole")
    save(rock(5), "rock")
    save(barriers(), "barrier", cell=(BARRIER, BARRIER))
    save(harvest(), "harvest")
    save(fossil(), "fossil")
    save(inventory_bubbles(), "inventory", cell=(INVENTORY, INVENTORY))
    save(Image.new("RGBA", (4, 4), (255, 255, 255, 255)), "plain")
    write_prefabs()
    write_scene("sandbox", sandbox())
    write_scene("sandbox_low", sandbox_low())
    save_font()
    write_strings()
    write_scene("tutorial", tutorial())
    save(glow(), "glow")
    save(planet(), "planet")
    save(nebula(), "nebula")
    save(hero_curled(), "hero_curled")
    write_scene("opening", opening())
    write_scene("drift", drift())


if __name__ == "__main__":
    main()
