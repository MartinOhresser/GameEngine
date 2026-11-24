#!/usr/bin/env python3
"""Rebuilds the generated files of lueur: every picture, their atlases and the scene.

    python3 games/lueur/tools/make_assets.py      (numpy and Pillow: apt install python3-numpy python3-pil)

The look (ADR 0015): the silhouettes of Limbo, the light of Ori. A dusk forest seen through
mist: black shapes in front, planes that fade into the fog as they go back, warm light low in the
sky. Everything is drawn here from a fixed seed, so the same script always gives the same
pictures: the game ships no binary that cannot be rebuilt and reviewed.

Each plane of the parallax is one picture, alone in its atlas (one cell the size of the picture),
smoothed ("filter": "linear"). The planes that repeat sideways are drawn seamlessly: whatever leaves
the right edge comes back on the left.
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
GROUND_Y = 560  # top of the ground the hero walks on
LEVEL_W = 12 * 512  # the level runs from x 0 to here: the camera never shows past its ends
SUPER = 2  # shapes are drawn this many times bigger, then reduced: smooth edges

# Dusk: deep blue above, warm haze at the horizon, a cold grey-green fog.
SKY_TOP = (8, 16, 28)
SKY_MID = (34, 58, 72)
SKY_LOW = (196, 160, 112)
FOG = (118, 138, 140)
INK = (4, 6, 8)  # the silhouettes in front: almost black


# ---------------------------------------------------------------------------------------------
# Small tools


def lerp(a, b, t):
    return a + (b - a) * t


def mix(color_a, color_b, t):
    return tuple(int(round(lerp(x, y, t))) for x, y in zip(color_a, color_b))


def periodic_noise(rng, width, octaves):
    """A smooth line of `width` values in [-1, 1] whose end meets its start (sums of sines with
    whole numbers of periods across the width). `octaves`: (number of periods, weight) pairs."""
    x = np.arange(width) / width * 2.0 * math.pi
    line = np.zeros(width)
    total = 0.0
    for periods, weight in octaves:
        phase = rng.uniform(0, 2 * math.pi)
        line += weight * np.sin(periods * x + phase)
        total += weight
    return line / total


def tinted(mask, color, opacity=1.0):
    """An RGBA picture of `color`, as opaque as the grey `mask` (L mode) says."""
    picture = Image.new("RGBA", mask.size, color + (0,))
    alpha = mask if opacity >= 1.0 else mask.point(lambda v: int(v * opacity))
    picture.putalpha(alpha)
    return picture


def reduce(mask, size):
    return mask.resize(size, Image.LANCZOS)


def save(picture, name):
    os.makedirs(ART, exist_ok=True)
    picture.save(os.path.join(ART, name + ".png"), optimize=True)
    width, height = picture.size
    atlas = {
        "schema_version": 1,
        "name": name,
        "image": name + ".png",
        "cell_width": width,
        "cell_height": height,
        "filter": "linear",
    }
    with open(os.path.join(ART, name + ".json"), "w", encoding="utf-8") as out:
        json.dump(atlas, out, indent=2)
        out.write("\n")


def save_sheet(frames, name):
    """Several pictures of the same size side by side: the cells of one atlas."""
    width, height = frames[0].size
    sheet = Image.new("RGBA", (width * len(frames), height), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.paste(frame, (index * width, 0))
    os.makedirs(ART, exist_ok=True)
    sheet.save(os.path.join(ART, name + ".png"), optimize=True)
    atlas = {
        "schema_version": 1,
        "name": name,
        "image": name + ".png",
        "cell_width": width,
        "cell_height": height,
        "filter": "linear",
    }
    with open(os.path.join(ART, name + ".json"), "w", encoding="utf-8") as out:
        json.dump(atlas, out, indent=2)
        out.write("\n")


# ---------------------------------------------------------------------------------------------
# Trees: a trunk that leans and tapers, branches that split, clumps of leaves at their ends.


def tapered_segment(draw, x0, y0, x1, y1, w0, w1):
    angle = math.atan2(y1 - y0, x1 - x0)
    nx, ny = -math.sin(angle), math.cos(angle)
    points = [
        (x0 + nx * w0 / 2, y0 + ny * w0 / 2),
        (x1 + nx * w1 / 2, y1 + ny * w1 / 2),
        (x1 - nx * w1 / 2, y1 - ny * w1 / 2),
        (x0 - nx * w0 / 2, y0 - ny * w0 / 2),
    ]
    draw.polygon(points, fill=255)
    draw.ellipse((x1 - w1 / 2, y1 - w1 / 2, x1 + w1 / 2, y1 + w1 / 2), fill=255)


def branch(draw, rng, x, y, angle, length, width, depth, leafy):
    """One limb, drawn as a few bent segments, then its children."""
    steps = 4
    for _ in range(steps):
        angle += rng.normal(0, 0.12)
        nx = x + math.cos(angle) * length / steps
        ny = y + math.sin(angle) * length / steps
        next_width = width * 0.86
        tapered_segment(draw, x, y, nx, ny, width, next_width)
        x, y, width = nx, ny, next_width
    if depth == 0:
        if leafy:
            # A loose spray of small leaves rather than one round clump: light gets through.
            spread = length * 0.45
            for _ in range(rng.integers(14, 26)):
                r = rng.uniform(0.04, 0.11) * length
                cx = x + rng.normal(0, spread * 0.6)
                cy = y + rng.normal(0, spread * 0.35) + abs(rng.normal(0, spread * 0.25))
                tilt = rng.uniform(0, math.pi)
                points = [(cx + math.cos(tilt + k * math.pi / 3) * r * (1.6 if k % 3 == 0 else 0.7),
                           cy + math.sin(tilt + k * math.pi / 3) * r * (1.6 if k % 3 == 0 else 0.7))
                          for k in range(6)]
                draw.polygon(points, fill=255)
        return
    children = rng.integers(2, 4)
    for _ in range(children):
        spread = rng.uniform(0.35, 0.8) * (1 if rng.random() < 0.5 else -1)
        branch(draw, rng, x, y, angle + spread, length * rng.uniform(0.55, 0.75), width * 0.7,
               depth - 1, leafy)


def tree(draw, rng, x, ground, height, width, leafy):
    """A whole tree standing on `ground` at `x`: a tall trunk, then a crown of branches."""
    lean = rng.normal(0, 0.05)
    angle = -math.pi / 2 + lean
    trunk_top = height * rng.uniform(0.45, 0.65)
    # Roots: a flare at the foot.
    draw.polygon([(x - width * 1.6, ground + 4), (x - width / 2, ground - width * 1.2),
                  (x + width / 2, ground - width * 1.2), (x + width * 1.6, ground + 4)], fill=255)
    tx = x + math.cos(angle) * trunk_top
    ty = ground + math.sin(angle) * trunk_top
    tapered_segment(draw, x, ground, tx, ty, width, width * 0.7)
    for _ in range(rng.integers(2, 4)):
        side = rng.uniform(-0.9, 0.9)
        branch(draw, rng, tx, ty, angle + side, (height - trunk_top) * rng.uniform(0.5, 0.8),
               width * 0.6, 2, leafy)
    # A few dead twigs lower on the trunk: the Limbo look.
    for _ in range(rng.integers(0, 3)):
        t = rng.uniform(0.25, 0.9)
        bx = lerp(x, tx, t)
        by = lerp(ground, ty, t)
        side = math.pi / 2 * (1 if rng.random() < 0.5 else -1)
        branch(draw, rng, bx, by, angle + side * rng.uniform(0.5, 0.9), height * 0.12,
               width * 0.25, 0, False)


def forest_band(seed, width, height, ground_level, hill, trees, tree_height, trunk, leafy,
                grass):
    """The mask of a band of forest that repeats sideways: hills, trees and grass, all one shape.
    Drawn `SUPER` times bigger and reduced."""
    rng = np.random.default_rng(seed)
    big_w, big_h = width * SUPER, height * SUPER
    mask = Image.new("L", (big_w, big_h), 0)
    draw = ImageDraw.Draw(mask)
    line = periodic_noise(rng, big_w, [(1, 1.0), (3, 0.5), (7, 0.25), (17, 0.1)])
    ground = ground_level * SUPER + line * hill * SUPER
    draw.polygon([(0, big_h)] + [(x, ground[x]) for x in range(0, big_w, 4)] +
                 [(big_w - 1, ground[-1]), (big_w, big_h)], fill=255)
    for _ in range(trees):
        x = rng.uniform(0, big_w)
        h = tree_height * SUPER * rng.uniform(0.7, 1.2)
        w = trunk * SUPER * rng.uniform(0.7, 1.3)
        for shift in (-big_w, 0, big_w):  # seamless: the parts that stick out come back
            sub = np.random.default_rng(int(x * 1000) + seed)
            g = ground[int(x) % big_w] + 6 * SUPER
            tree(draw, sub, x + shift, g, h, w, leafy)
    if grass:
        for x in range(0, big_w, 3):
            if rng.random() < 0.55:
                g = ground[x] + 2
                blade = rng.uniform(4, grass) * SUPER
                lean = rng.normal(0, blade * 0.35)
                for shift in (-big_w, 0, big_w):
                    draw.polygon([(x + shift - 2, g), (x + shift + lean, g - blade),
                                  (x + shift + 2, g)], fill=255)
    return reduce(mask, (width, height))


# ---------------------------------------------------------------------------------------------
# The planes


def sky():
    """The sky, from top to horizon, with the warm haze low down. A thin picture stretched
    across the view."""
    height = VIEW_H
    rows = []
    for y in range(height):
        t = y / (height - 1)
        if t < 0.55:
            color = mix(SKY_TOP, SKY_MID, t / 0.55)
        else:
            color = mix(SKY_MID, SKY_LOW, ((t - 0.55) / 0.45) ** 1.6)
        rows.append(color + (255,))
    column = np.array(rows, dtype=np.uint8).reshape(height, 1, 4)
    picture = np.repeat(column, 64, axis=1)
    save(Image.fromarray(picture, "RGBA"), "sky")


def glow():
    """A round soft light, white: tinted by the Sprite and added (additive) to what is under it.
    The setting sun, a lantern, the spirit, a firefly."""
    size = 256
    y, x = np.mgrid[0:size, 0:size]
    r = np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) / (size / 2)
    core = np.clip(1 - r, 0, 1)
    light = core ** 2.2 * 0.85 + np.exp(-(r * 6) ** 2) * 0.15 * 1.0
    alpha = np.clip(light, 0, 1) * 255
    picture = np.zeros((size, size, 4), dtype=np.uint8)
    picture[..., :3] = 255
    picture[..., 3] = alpha.astype(np.uint8)
    save(Image.fromarray(picture, "RGBA"), "glow")


def ray():
    """A shaft of light: soft across, fading towards its end. Turned (transform2d.Rotation) and
    added on top of the scene."""
    width, height = 128, 1024
    y, x = np.mgrid[0:height, 0:width]
    across = np.exp(-((x - width / 2 + 0.5) / (width * 0.22)) ** 2)
    along = np.clip(1 - y / height, 0, 1) ** 1.5 * np.clip(y / (height * 0.08), 0, 1)
    alpha = across * along * 255
    picture = np.zeros((height, width, 4), dtype=np.uint8)
    picture[..., :3] = 255
    picture[..., 3] = alpha.astype(np.uint8)
    save(Image.fromarray(picture, "RGBA"), "ray")


def mist(name, seed, width, height, density):
    """A band of fog that repeats sideways: soft at the top and bottom, uneven along."""
    rng = np.random.default_rng(seed)
    along = periodic_noise(rng, width, [(2, 1.0), (5, 0.6), (11, 0.3), (23, 0.15)])
    y = np.arange(height)[:, None] / (height - 1)
    across = np.exp(-((y - 0.5) / 0.22) ** 2)
    thickness = 0.55 + 0.45 * along[None, :]
    alpha = np.clip(across * thickness * density, 0, 1) * 255
    picture = np.zeros((height, width, 4), dtype=np.uint8)
    picture[..., :3] = 255
    picture[..., 3] = alpha.astype(np.uint8)
    save(Image.fromarray(picture, "RGBA"), name)


# The forest planes, far to near. Each is a tall band (PLANE_H) whose ground line sits at `ground`
# on the screen when the camera starts: the further, the higher the horizon. The band reaches
# below the bottom of the view, so no edge ever shows. Colours go from pale and warm (the air
# lit by the low sun) to almost black, and the far planes are blurred.
PLANE_H = 1000
PLANES = [
    # name, seed, depth, ground on screen, hill, trees, tree height, trunk, leafy, grass, colour,
    # blur
    ("ridge", 11, 0.12, 430, 50, 0, 0, 0, False, 0, (156, 162, 150), 3.0),
    ("far_trees", 12, 0.3, 470, 26, 40, 330, 7, True, 0, (112, 124, 120), 2.2),
    ("mid_trees", 13, 0.55, 510, 20, 22, 470, 12, True, 8, (58, 70, 72), 1.2),
    ("near_trees", 14, 0.78, 548, 14, 12, 640, 20, False, 14, (22, 28, 31), 0.5),
]


def plane_ground_in_picture(ground_on_screen):
    """Where the ground line is in the picture, so that the picture's bottom is 40 below the
    view's."""
    return PLANE_H - (VIEW_H + 40 - ground_on_screen)


def planes():
    for name, seed, _, ground, hill, trees, tall, trunk, leafy, grass, color, blur in PLANES:
        mask = forest_band(seed, 2048, PLANE_H, plane_ground_in_picture(ground), hill, trees,
                           tall, trunk, leafy, grass)
        if blur > 0:
            mask = mask.filter(ImageFilter.GaussianBlur(blur))
        save(tinted(mask, color), name)


def ground():
    """The ground the hero walks on: black earth with a ragged grassy top. A block that repeats
    sideways (several entities side by side)."""
    width, height = 512, 256
    rng = np.random.default_rng(21)
    big_w, big_h = width * SUPER, height * SUPER
    mask = Image.new("L", (big_w, big_h), 0)
    draw = ImageDraw.Draw(mask)
    line = periodic_noise(rng, big_w, [(1, 1.0), (4, 0.4), (9, 0.2)]) * 6 * SUPER + 14 * SUPER
    draw.polygon([(0, big_h)] + [(x, line[x]) for x in range(0, big_w, 4)] +
                 [(big_w - 1, line[-1]), (big_w, big_h)], fill=255)
    for x in range(0, big_w, 2):
        if rng.random() < 0.7:
            blade = rng.uniform(6, 22) * SUPER
            lean = rng.normal(0, blade * 0.3)
            for shift in (-big_w, 0, big_w):
                draw.polygon([(x + shift - 2.5, line[x] + 4), (x + shift + lean, line[x] - blade),
                              (x + shift + 2.5, line[x] + 4)], fill=255)
    mask = reduce(mask, (width, height))
    save(tinted(mask, INK), "ground")


def foreground():
    """What hangs and grows between the camera and the hero: branches from the top, tall grass
    and ferns at the bottom. Black, blurred a little (too close to be in focus). A band that
    repeats sideways, with an empty middle."""
    width, height = 2048, VIEW_H
    rng = np.random.default_rng(31)
    big_w, big_h = width * SUPER, height * SUPER
    mask = Image.new("L", (big_w, big_h), 0)
    draw = ImageDraw.Draw(mask)
    for _ in range(5):  # branches that hang from above the view
        x = rng.uniform(0, big_w)
        reach = rng.uniform(140, 260) * SUPER
        for shift in (-big_w, 0, big_w):
            sub = np.random.default_rng(int(x) + 7)
            branch(draw, sub, x + shift, -20, math.pi / 2 + sub.normal(0, 0.4), reach,
                   18 * SUPER, 2, True)
    for _ in range(9):  # clumps of tall grass at the bottom
        x = rng.uniform(0, big_w)
        for blade in range(40):
            sub = np.random.default_rng(int(x) * 50 + blade)
            bx = x + sub.normal(0, 40 * SUPER)
            tall = sub.uniform(60, 170) * SUPER
            lean = sub.normal(0, tall * 0.35)
            for shift in (-big_w, 0, big_w):
                draw.polygon([(bx + shift - 5, big_h + 4), (bx + shift + lean, big_h - tall),
                              (bx + shift + 5, big_h + 4)], fill=255)
    mask = reduce(mask, (width, height)).filter(ImageFilter.GaussianBlur(2.5))
    save(tinted(mask, INK), "foreground")


# ---------------------------------------------------------------------------------------------
# The hero: a small silhouette with two pale eyes. Drawn from a pose (angles of the limbs), so a
# whole walk is a list of poses.

CELL = 128


def limb(draw, x, y, angle_upper, angle_lower, upper, lower, width):
    knee_x = x + math.sin(angle_upper) * upper
    knee_y = y + math.cos(angle_upper) * upper
    foot_x = knee_x + math.sin(angle_upper + angle_lower) * lower
    foot_y = knee_y + math.cos(angle_upper + angle_lower) * lower
    tapered_segment(draw, x, y, knee_x, knee_y, width, width * 0.85)
    tapered_segment(draw, knee_x, knee_y, foot_x, foot_y, width * 0.85, width * 0.7)
    return foot_x, foot_y


def hero_pose(legs, arms, bob, lean):
    """One picture of the hero. `legs` and `arms`: ((upper, lower) angle) for the near and far
    limb, in radians from straight down, forward positive. `bob`: how much the body is lifted."""
    s = SUPER * 2
    size = CELL * s
    mask = Image.new("L", (size, size), 0)
    eyes = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    cx = size / 2
    feet = size - 6 * s  # the feet touch the bottom of the cell (the pivot)
    hip_y = feet - 40 * s - bob * s
    shoulder_y = hip_y - 30 * s
    for upper, lower in legs:
        limb(draw, cx, hip_y, upper, lower, 21 * s, 21 * s, 9 * s)
    # The body: a slightly leaning rounded block.
    top_x = cx + lean * s
    tapered_segment(draw, cx, hip_y + 2 * s, top_x, shoulder_y, 18 * s, 16 * s)
    for upper, lower in arms:
        limb(draw, top_x, shoulder_y + 3 * s, upper, -abs(lower), 15 * s, 15 * s, 6 * s)
    # The head: big, as a child's.
    head_r = 15 * s
    hx = top_x + 2 * s
    hy = shoulder_y - head_r + 1 * s
    draw.ellipse((hx - head_r, hy - head_r, hx + head_r, hy + head_r), fill=255)
    # A tuft of hair.
    draw.polygon([(hx - head_r * 0.6, hy - head_r * 0.7), (hx - head_r * 1.2, hy - head_r * 1.1),
                  (hx - head_r * 0.1, hy - head_r * 0.95)], fill=255)
    eye = ImageDraw.Draw(eyes)
    for ex in (hx + 5 * s, hx + 11 * s):
        eye.ellipse((ex - 2.2 * s, hy - 2.6 * s, ex + 2.2 * s, hy + 1.4 * s), fill=255)
    body = tinted(reduce(mask, (CELL, CELL)), INK)
    light = tinted(reduce(eyes, (CELL, CELL)), (235, 240, 225))
    return Image.alpha_composite(body, light)


# The hero's box: its feet are at the bottom of the picture, a little above the bottom of the cell.
HERO_W, HERO_H = 34, 92
HERO_FEET = 6  # empty pixels of the cell under the feet (the drawing's margin)
HERO_PIVOT_Y = round((CELL - HERO_H / 2 - HERO_FEET) / CELL, 4)


def hero():
    """Cells 0-7: standing (breathing); 8-19: running; 20: going up; 21: coming down."""
    frames = []
    for i in range(8):  # idle: the body rises and falls a little
        bob = math.sin(i / 8 * 2 * math.pi) * 1.2
        frames.append(hero_pose([(0.05, 0.0), (-0.05, 0.0)], [(0.1, 0.2), (-0.1, 0.2)], bob, 1))
    for i in range(12):  # run: legs and arms swing in opposition
        t = i / 12 * 2 * math.pi
        swing = math.sin(t)
        near = (swing * 0.75, max(0.0, -math.cos(t)) * 1.1)
        far = (-swing * 0.75, max(0.0, math.cos(t)) * 1.1)
        bob = abs(math.cos(t)) * 3
        frames.append(hero_pose([near, far], [(-swing * 0.9, 0.6), (swing * 0.9, 0.6)], bob, 5))
    frames.append(hero_pose([(0.7, 1.2), (-0.3, 0.8)], [(-1.6, 0.4), (1.2, 0.5)], 6, 3))  # up
    frames.append(hero_pose([(0.3, 0.3), (-0.2, 0.5)], [(-2.3, 0.2), (2.2, 0.3)], 2, 2))  # down
    save_sheet(frames, "hero")


# ---------------------------------------------------------------------------------------------
# The scene: the planes, far to near, around the place the hero stands.

LAYERS = {
    "sky": -100, "sun": -95, "ridge": -90, "mist_far": -85, "far_trees": -80, "haze": -78,
    "mist_mid": -75, "mid_trees": -70, "rays": -65, "near_trees": -60, "mist_near": -55,
    "ground": 0, "hero": 10, "foreground": 50,
}


def at_screen(screen_x, screen_y, depth):
    """The world point of a plane of `depth` that shows at this point of the screen when the
    camera is where it starts (the middle of the first view): world = screen - half + camera x
    depth."""
    return (round(screen_x - VIEW_W / 2 + VIEW_W / 2 * depth, 2),
            round(screen_y - VIEW_H / 2 + VIEW_H / 2 * depth, 2))


def entity(name, x, y, sprite, parallax=None, extra=None):
    components = {"transform2d.Position": {"x": x, "y": y}, "sprite2d.Sprite": sprite}
    if parallax is not None:
        components["sprite2d.Parallax"] = parallax
    if extra:
        components.update(extra)
    return {"name": name, "components": components}


def plane(name, atlas, depth, y, width, height, color=None, additive=False):
    """A band of the forest at `depth` (the share of the camera's movement it follows), centred
    on the view at the start (the camera starts at x = VIEW_W / 2)."""
    sprite = {"atlas": atlas, "width": width, "height": height, "layer": LAYERS[name]}
    if color:
        sprite.update({"red": color[0], "green": color[1], "blue": color[2], "alpha": color[3]})
    if additive:
        sprite["additive"] = True
    x, y = at_screen(VIEW_W / 2, y, depth)
    return entity(name, x, y, sprite, {"x": depth, "y": depth, "repeat_x": True})


def scene():
    entities = [
        entity("sky", 0, 0, {"atlas": "sky", "width": VIEW_W, "height": VIEW_H,
                             "layer": LAYERS["sky"]}, {"x": 0, "y": 0}),
        entity("sun", *at_screen(420, 470, 0.05), {
            "atlas": "glow", "width": 900, "height": 700, "red": 255, "green": 196, "blue": 128,
            "alpha": 200, "additive": True, "layer": LAYERS["sun"]}, {"x": 0.05, "y": 0.05}),
        plane("mist_far", "mist_far", 0.2, 440, 2048, 220, (230, 214, 186, 120)),
        plane("mist_mid", "mist_mid", 0.42, 490, 2048, 240, (196, 206, 198, 110)),
        plane("mist_near", "mist_near", 0.9, 560, 2048, 200, (150, 164, 162, 80)),
        # The low sun again, in front of the far planes: its light bleeds through the trees.
        entity("haze", *at_screen(420, 450, 0.3), {
            "atlas": "glow", "width": 1400, "height": 900, "red": 255, "green": 180,
            "blue": 110, "alpha": 90, "additive": True, "layer": LAYERS["haze"]},
            {"x": 0.3, "y": 0.3}),
        plane("foreground", "foreground", 1.35, VIEW_H / 2, 2048, VIEW_H),
    ]
    for name, _, depth, ground, *_ in PLANES:
        centre = ground - plane_ground_in_picture(ground) + PLANE_H / 2
        entities.append(plane(name, name, depth, centre, 2048, PLANE_H))
    # Shafts of warm light through the trees, added on top of the far planes.
    for index, (x, angle, alpha) in enumerate([(300, -28, 70), (520, -24, 50), (760, -31, 40)]):
        entities.append(entity(f"ray_{index}", *at_screen(x, -60, 0.6), {
            "atlas": "ray", "width": 180, "height": 900, "pivot_y": 0.0, "red": 255,
            "green": 214, "blue": 150, "alpha": alpha, "additive": True, "layer": LAYERS["rays"],
        }, {"x": 0.6, "y": 0.6}, {"transform2d.Rotation": {"angle": angle}}))
    # The ground: blocks side by side, on the plane of the action. What stops the hero is one
    # long solid box under them, and a wall at each end of the level.
    for index in range(LEVEL_W // 512):
        entities.append(entity(f"ground_{index}", index * 512 + 256, GROUND_Y + 128 - 14,
                               {"atlas": "ground", "width": 512, "height": 256,
                                "layer": LAYERS["ground"]}))
    for name, x, y, width, height in [
            ("floor", LEVEL_W / 2, GROUND_Y + 100, LEVEL_W + 400, 200),
            ("wall_left", -100, GROUND_Y - 1000, 200, 2400),
            ("wall_right", LEVEL_W + 100, GROUND_Y - 1000, 200, 2400)]:
        entities.append({"name": name, "components": {
            "transform2d.Position": {"x": x, "y": y},
            "collision2d.Collider": {"width": width, "height": height, "layer": ["ground"],
                                     "solid": True},
        }})
    # The hero: its Position is the middle of its box (HERO_H tall), so the picture hangs from a
    # pivot that puts its feet on the bottom of the box.
    entities.append(entity("hero", 520, GROUND_Y - HERO_H / 2, {
        "atlas": "hero", "width": CELL, "height": CELL, "pivot_y": HERO_PIVOT_Y,
        "layer": LAYERS["hero"],
    }, None, {
        "sprite2d.SpriteAnimation": {"first_frame": 0, "frame_count": 8, "ticks_per_frame": 9},
        "transform2d.Velocity": {},
        "collision2d.Collider": {"width": HERO_W, "height": HERO_H, "layer": ["hero"],
                                 "mask": ["ground"]},
        "collision2d.Body": {},
        "collision2d.Blocked": {},
        "gravity2d.Gravity": {"y": 1800, "terminal_speed": 1100},
        "action_control.Controlled": {"speed": 260, "vertical": False, "acceleration": 1800,
                                      "braking": 2600},
        "jump.Jumper": {"speed": 720, "coyote_time": 0.1, "buffer_time": 0.12, "cut": 0.45},
        "camera2d.CameraTarget": {},
    }))
    # The camera follows the hero loosely: small steps and hops leave the view still, it looks
    # ahead of where he runs, and it never shows past the ends of the level or below the ground.
    resting_y = GROUND_Y - HERO_H / 2
    entities.append({"name": "camera", "components": {
        "transform2d.Position": {"x": VIEW_W / 2, "y": VIEW_H / 2},
        "camera2d.Camera": {"zoom": 1},
        "camera2d.CameraFollow": {
            "stiffness": 0.08, "offset_y": VIEW_H / 2 - resting_y, "dead_x": 40, "dead_y": 110,
            "look_ahead": 140, "bounded": True, "bound_left": 0, "bound_right": LEVEL_W,
            "bound_top": -2000, "bound_bottom": VIEW_H},
    }})
    os.makedirs(SCENES, exist_ok=True)
    with open(os.path.join(SCENES, "forest.json"), "w", encoding="utf-8") as out:
        out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
        json.dump({"schema_version": 1, "name": "forest", "entities": entities}, out, indent=2)
        out.write("\n")


def main():
    sky()
    glow()
    ray()
    mist("mist_far", 41, 2048, 220, 0.9)
    mist("mist_mid", 42, 2048, 240, 0.8)
    mist("mist_near", 43, 2048, 200, 0.7)
    planes()
    ground()
    foreground()
    hero()
    scene()
    print("lueur: pictures, atlases and scene written in", GAME)


if __name__ == "__main__":
    main()
