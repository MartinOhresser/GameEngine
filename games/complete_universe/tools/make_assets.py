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
    oval(draw, cx - 10, feet - 44, 5, 16, GREY, angle=12)  # far arm
    oval(draw, cx + 12, feet - 44, 5, 16, GREY, angle=-12)  # near arm
    oval(draw, cx + 2, feet - 76, 10, 11, WHITE)  # head
    oval(draw, cx - 3, feet - 76, 6, 10, BLACK)
    return reduce(picture, CELL, CELL)


def ground():
    """A plain grey ground with a lighter top edge: the test ground, until the real terrain."""
    picture = Image.new("RGBA", (64, 64), (40, 40, 44, 255))
    ImageDraw.Draw(picture).rectangle([0, 0, 63, 5], fill=(150, 150, 150, 255))
    return picture


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


def hero_entity(x, y):
    return entity("hero", x, y, {
        "sprite2d.Sprite": {"atlas": "hero", "width": CELL, "height": CELL,
                            "layer": LAYERS["hero"]},
        "sprite2d.Facing": {},
        "tag.Tag": {"tag": "hero"},
        "transform2d.Velocity": {},
        "collision2d.Collider": {"width": HERO_W, "height": HERO_H, "layer": ["hero"],
                                 "mask": ["ground"]},
        "collision2d.Body": {},
        "collision2d.Blocked": {},
        "gravity2d.Gravity": {"y": 1800, "terminal_speed": 1100},
        "camera2d.CameraTarget": {},
        "action_control.Controlled": {"speed": 240, "vertical": False, "acceleration": 1600,
                                      "braking": 2400},
        "jump.Jumper": {"speed": 680, "coyote_time": 0.1, "buffer_time": 0.12, "cut": 0.45},
    })


def camera(bound_right):
    return entity("camera", VIEW_W / 2, VIEW_H / 2, {
        "camera2d.Camera": {"zoom": 1},
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


def ground_entity(name, left, right, top, depth=400):
    width = right - left
    return entity(name, left + width / 2, top + depth / 2, {
        "sprite2d.Sprite": {"atlas": "ground", "width": width, "height": depth,
                            "layer": LAYERS["ground"]},
        "collision2d.Collider": {"width": width, "height": depth, "layer": ["ground"],
                                 "solid": True},
    })


def sandbox():
    """The test ground of the modules: flat, wide, nothing on it."""
    return [sky_entity(), ground_entity("ground", 0, SANDBOX_W, GROUND_Y),
            hero_entity(400, GROUND_Y - HERO_H / 2), camera(SANDBOX_W)]


def write_scene(name, entities):
    os.makedirs(SCENES, exist_ok=True)
    with open(os.path.join(SCENES, name + ".json"), "w", encoding="utf-8") as out:
        out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
        json.dump({"schema_version": 1, "name": name, "entities": entities}, out, indent=2)
        out.write("\n")


def main():
    save(hero(), "hero")
    save(ground(), "ground")
    save(sky(), "sky")
    write_scene("sandbox", sandbox())


if __name__ == "__main__":
    main()
