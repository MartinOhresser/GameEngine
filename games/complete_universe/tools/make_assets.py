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


# What the lens adds at rest: a dark vignette, as in Limbo, a little glow and grain.
BASE_VIGNETTE = 0.45
BASE_VIGNETTE_SIZE = 0.45


def hands(unlocked=("black", "white"), offered=()):
    """The two hands (module loadout): the left one answers to the left button, the right one to
    the right button. Each is an arm beside the hero, coloured by its power (empty: grey). The
    kit says which powers work; black and white together make the harvest (GDD §6)."""
    kit = entity("kit", 0, 0, {"loadout.Kit": {
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
    return [
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
                                 "mask": ["ground"]},
        "collision2d.Body": {},
        "gravity2d.Gravity": {"y": 1800, "terminal_speed": 1100},
        "attractor2d.Absorbable": {"mass": 10},
        "reserve.Yield": {"kind": kind, "amount": amount},
    })


def camera(bound_right):
    return entity("camera", VIEW_W / 2, VIEW_H / 2, {
        "tag.Tag": {"tag": "camera"},
        "camera2d.Camera": {"zoom": 1},
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
            hero_entity(400, GROUND_Y - HERO_H / 2), camera(SANDBOX_W)] + hands() + powers() \
        + reserves()


def sandbox_low():
    """The test ground with little black and white left: the edges of the screen show it."""
    return sandbox()[:-6] + reserves(matter=20, carbon=8)


# Life (GDD §8): black (matter) and white (carbon) wear away to keep the body whole; full, they
# last about five minutes (P8). With less than half of either, the edges of the screen turn: white
# when the matter runs out, black when the carbon does, until the whole screen is gone.
LIFE_DRAIN = 100 / 300


def reserves(matter=100, carbon=100):
    return [
        entity("matter", 0, 0, {"tag.Tag": {"tag": "matter"}, "reserve.Reserve": {
            "kind": "matter", "amount": matter, "max": 100, "drain": LIFE_DRAIN,
            "empty_action": "out_of_matter"}}),
        entity("carbon", 0, 0, {"tag.Tag": {"tag": "carbon"}, "reserve.Reserve": {
            "kind": "carbon", "amount": carbon, "max": 100, "drain": LIFE_DRAIN,
            "empty_action": "out_of_carbon"}}),
        bind("carbon_darkens", "carbon", "reserve.Reserve.fill", "postfx.Effects.vignette",
             0.5, 0, BASE_VIGNETTE, 1, 1.5),
        bind("carbon_closes", "carbon", "reserve.Reserve.fill", "postfx.Effects.vignette_size",
             0.5, 0, BASE_VIGNETTE_SIZE, -3, 4),
        bind("matter_whitens", "matter", "reserve.Reserve.fill", "postfx.Effects.edge_glow",
             0.5, 0, 0, 1, 1.5),
        bind("matter_closes", "matter", "reserve.Reserve.fill", "postfx.Effects.edge_glow_size",
             0.5, 0, 0.5, -3, 4),
    ]


def bind(name, source_tag, source, target, low, high, out_low, out_high, power,
         onto="camera"):
    """A number of an entity (the camera by default) that follows a number of another one."""
    return {"name": name, "components": {
        "bind.Bind": {"source": source, "from": source_tag, "target": target, "onto": onto,
                      "low": low, "high": high, "out_low": out_low, "out_high": out_high,
                      "power": power}}}


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
    write_scene("sandbox", sandbox())
    write_scene("sandbox_low", sandbox_low())


if __name__ == "__main__":
    main()
