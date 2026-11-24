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


# A few falling leaves: small silhouettes, each turned another way (particles pick one).
LEAF = 32


def leaves():
    frames = []
    for index, angle in enumerate([0.3, 1.4, 2.4, 3.6]):
        s = SUPER * 4
        size = LEAF * s
        mask = Image.new("L", (size, size), 0)
        draw = ImageDraw.Draw(mask)
        c = size / 2
        length = size * (0.42 - 0.03 * index)
        width = length * 0.42
        # A pointed oval along `angle`, with a short stalk.
        points = []
        for step in range(33):
            t = step / 32 * 2 * math.pi
            along = math.cos(t) * length / 2
            across = math.sin(t) * width / 2 * (1 - 0.35 * math.cos(t))
            points.append((c + along * math.cos(angle) - across * math.sin(angle),
                           c + along * math.sin(angle) + across * math.cos(angle)))
        draw.polygon(points, fill=255)
        tip = (c - length / 2 * math.cos(angle), c - length / 2 * math.sin(angle))
        draw.line([tip, (tip[0] - length * 0.18 * math.cos(angle),
                         tip[1] - length * 0.18 * math.sin(angle))], fill=255, width=s)
        frames.append(tinted(reduce(mask, (LEAF, LEAF)), INK))
    save_sheet(frames, "leaves")


# The hero's box: its feet are at the bottom of the picture, a little above the bottom of the cell.
HERO_W, HERO_H = 34, 92
HERO_FEET = 6  # empty pixels of the cell under the feet (the drawing's margin)
HERO_PIVOT_Y = round((CELL - HERO_H / 2 - HERO_FEET) / CELL, 4)


def hero():
    """Cells 0-7: standing (breathing); 8-19: running; 20: going up; 21: coming down."""
    frames = []
    for i in range(8):  # idle: feet apart, the body rises and falls a little
        bob = math.sin(i / 8 * 2 * math.pi) * 1.2
        frames.append(hero_pose([(0.16, -0.06), (-0.14, 0.1)], [(0.12, 0.2), (-0.12, 0.2)],
                                bob, 1))
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


# The spirit: a small being of light that floats after the hero. A white core in a blue halo, with
# a few motes turning around it; it breathes (the halo swells and shrinks) over its cells. It is a
# picture with its own colours, drawn over what is behind it: light added to the pale fog would
# be lost in it.
SPIRIT = 64
SPIRIT_FRAMES = 8


def spirit():
    frames = []
    size = SPIRIT
    y, x = np.mgrid[0:size, 0:size]
    c = size / 2 - 0.5
    for index in range(SPIRIT_FRAMES):
        t = index / SPIRIT_FRAMES * 2 * math.pi
        breath = 1 + 0.12 * math.sin(t)
        r = np.hypot(x - c, y - c) / (size / 2)
        halo = np.clip(1 - r / (0.95 * breath), 0, 1) ** 1.8 * 0.85
        core = np.exp(-(r / (0.16 * breath)) ** 2)
        light = halo + core
        whiteness = core.copy()
        # Three motes on a slow circle, a third of a turn apart.
        for mote in range(3):
            angle = t / 3 + mote * 2 * math.pi / 3
            mx = c + math.cos(angle) * size * 0.27
            my = c + math.sin(angle) * size * 0.2
            mote_light = np.exp(-(np.hypot(x - mx, y - my) / 2.2) ** 2)
            light += mote_light
            whiteness += mote_light
        alpha = np.clip(light, 0, 1) * 255
        whiteness = np.clip(whiteness, 0, 1)[..., None]
        blue = np.array([70, 150, 255], dtype=float)
        picture = np.zeros((size, size, 4), dtype=np.uint8)
        picture[..., :3] = (blue + (255 - blue) * whiteness).astype(np.uint8)
        picture[..., 3] = alpha.astype(np.uint8)
        frames.append(Image.fromarray(picture, "RGBA"))
    save_sheet(frames, "spirit")


# ---------------------------------------------------------------------------------------------
# What the levels are made of: a crate to push, rocks to climb, thorns that kill, pods that swing
# on a thread. Black silhouettes, like the hero.

CRATE_W, CRATE_H = 140, 90


def crate():
    """A wooden crate: black, with the grooves of its planks and a brace a shade lighter."""
    s = SUPER * 2
    w, h = CRATE_W * s, CRATE_H * s
    picture = Image.new("RGBA", (w, h), INK + (255,))
    draw = ImageDraw.Draw(picture)
    groove = (26, 28, 32, 255)
    for y in (h * 0.34, h * 0.67):  # planks
        draw.line([(4 * s, y), (w - 4 * s, y)], fill=groove, width=2 * s)
    draw.rectangle((3 * s, 3 * s, w - 3 * s, h - 3 * s), outline=groove, width=2 * s)
    draw.line([(6 * s, h - 6 * s), (w - 6 * s, 6 * s)], fill=groove, width=3 * s)  # brace
    save(picture.resize((CRATE_W, CRATE_H), Image.LANCZOS), "crate")


def rock(name, width, height, seed):
    """A block of rock and earth with grass on top: `height` above the ground line, and 60 more
    below it so that its foot sinks into the ground. The top of the picture is the grass."""
    rng = np.random.default_rng(seed)
    total = height + 60
    big_w, big_h = width * SUPER, total * SUPER
    mask = Image.new("L", (big_w, big_h), 0)
    draw = ImageDraw.Draw(mask)
    top = 14 * SUPER
    line = periodic_noise(rng, big_w, [(1, 1.0), (5, 0.4)]) * 5 * SUPER + top
    side = periodic_noise(rng, big_h, [(2, 1.0), (7, 0.5)]) * 4 * SUPER
    outline = [(abs(side[y]) + 2 * SUPER, y) for y in range(big_h - 1, top, -4)]
    outline += [(x, line[x]) for x in range(0, big_w, 4)]
    outline += [(big_w - abs(side[y]) - 2 * SUPER, y) for y in range(top, big_h, 4)]
    draw.polygon(outline, fill=255)
    for x in range(int(4 * SUPER), big_w - int(4 * SUPER), 3):
        if rng.random() < 0.6:
            blade = rng.uniform(5, 18) * SUPER
            lean = rng.normal(0, blade * 0.3)
            draw.polygon([(x - 2.5, line[x] + 4), (x + lean, line[x] - blade),
                          (x + 2.5, line[x] + 4)], fill=255)
    save(tinted(reduce(mask, (width, total)), INK), name)


THORNS_W, THORNS_H = 256, 72


def thorns():
    """Brambles at the bottom of a pit: sharp spikes leaning every way over a tangle of stems.
    A band that repeats sideways."""
    rng = np.random.default_rng(51)
    big_w, big_h = THORNS_W * SUPER, THORNS_H * SUPER
    mask = Image.new("L", (big_w, big_h), 0)
    draw = ImageDraw.Draw(mask)
    draw.rectangle((0, big_h - 10 * SUPER, big_w, big_h), fill=255)
    for _ in range(60):
        x = rng.uniform(0, big_w)
        tall = rng.uniform(20, 66) * SUPER
        lean = rng.normal(0, tall * 0.35)
        foot = rng.uniform(3, 7) * SUPER
        for shift in (-big_w, 0, big_w):
            draw.polygon([(x + shift - foot, big_h), (x + shift + lean, big_h - tall),
                          (x + shift + foot, big_h)], fill=255)
    save(tinted(reduce(mask, (THORNS_W, THORNS_H)), INK), "thorns")


def shade():
    """Dark coming up from the bottom: clear at the top, black at the bottom. The inside of a
    pit."""
    height = 128
    alpha = (np.linspace(0, 1, height) ** 0.8 * 255).astype(np.uint8)
    picture = np.zeros((height, 8, 4), dtype=np.uint8)
    picture[..., :3] = INK
    picture[..., 3] = alpha[:, None]
    save(Image.fromarray(picture, "RGBA"), "shade")


POD_W, POD_H = 56, 640
POD_R = 20  # the radius of the pod itself, at the bottom of the picture


def pod():
    """A thorny seed pod hanging from a long thread: the pod at the bottom, the thread up to the
    top of the picture (higher than the view, so its end is never seen)."""
    s = SUPER * 2
    big_w, big_h = POD_W * s, POD_H * s
    mask = Image.new("L", (big_w, big_h), 0)
    draw = ImageDraw.Draw(mask)
    cx, cy = big_w / 2, big_h - (POD_R + 8) * s
    draw.line([(cx, 0), (cx, cy)], fill=255, width=2 * s)
    draw.ellipse((cx - POD_R * 0.8 * s, cy - POD_R * s, cx + POD_R * 0.8 * s, cy + POD_R * s),
                 fill=255)
    for index in range(14):  # spikes all around
        angle = index / 14 * 2 * math.pi
        base = 0.35
        tip = (cx + math.cos(angle) * (POD_R + 9) * s, cy + math.sin(angle) * (POD_R + 9) * s)
        left = (cx + math.cos(angle - base) * POD_R * 0.75 * s,
                cy + math.sin(angle - base) * POD_R * 0.9 * s)
        right = (cx + math.cos(angle + base) * POD_R * 0.75 * s,
                 cy + math.sin(angle + base) * POD_R * 0.9 * s)
        draw.polygon([left, tip, right], fill=255)
    save(tinted(reduce(mask, (POD_W, POD_H)), INK), "pod")


# Where the pod is in its picture, from the top: its Position is the middle of the pod.
POD_PIVOT_Y = round((POD_H - POD_R - 8) / POD_H, 4)


# ---------------------------------------------------------------------------------------------
# The levels. Each is a stretch of forest from x 0 to LEVEL_W: ground in segments, with pits
# between them (thorns at the bottom), rocks to climb, crates to push, pods to pass under, and an
# exit at the end that leads to the next level.
#
# What the hero can do: a jump rises 144 units at most and carries him about 200 units forward
# at full speed. A pit wider than that, or a rock higher, needs the crate.

LEVELS = {
    # The first level teaches the moves: a pit to jump, a rock too high to climb without the crate
    # (a small stone keeps the crate from being pushed back into the first pit), a second pit.
    "forest": {
        "next": "hollow",
        "segments": [(0, 1500), (1660, 4400), (4580, LEVEL_W)],
        "pits": [(1500, 1660, 130), (4400, 4580, 130)],
        "rocks": [("stone", 1790, 64, 40), ("ledge", 2950, 700, 200)],
        "crates": [2150],
        "pods": [],
        "look": {"ambient": (92, 100, 132), "tint": (255, 246, 232), "sun": 1.0},
    },
    # The hollow, deeper and darker: pods to pass under, then a pit too wide to jump, which the
    # crate fills up to the brim, then a last pit and more pods.
    "hollow": {
        "next": "ending",
        "segments": [(0, 1800), (2060, 4000), (4170, LEVEL_W)],
        "pits": [(1800, 2060, CRATE_H), (4000, 4170, 130)],
        "rocks": [],
        "crates": [1350],
        "pods": [(800, 0.5), (2900, 0.3), (3250, 0.7)],
        "look": {"ambient": (66, 74, 108), "tint": (232, 240, 255), "sun": 0.55},
    },
    # The end: the forest opens onto the low sun. Nothing to do; the hero walks on by himself
    # towards the light, the black bars come in, the picture fades out, and the game starts again.
    "ending": {
        "segments": [(0, LEVEL_W)],
        "pits": [],
        "rocks": [],
        "crates": [],
        "pods": [],
        "look": {"ambient": (128, 118, 120), "tint": (255, 238, 214), "sun": 1.5},
        "walker": True,
    },
}

# The game starts with the forest and a short opening (the prologue): the black bars are on, the
# hero cannot move yet, and the spirit comes down to him from the trees; then the bars slide away
# and the game begins. A death in it starts the forest over, without the opening.
LEVELS["prologue"] = dict(LEVELS["forest"], opening=True, restart="forest")

# What starts each level over after a death: the black veil spawned by the death (the prefab
# fade_<level>), which goes dark and then reloads the level.
FADE_TICKS = 40

ROCKS = {"stone": 21, "ledge": 22}


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


# What keeps its own colours when the forest is in the dark: the sky and everything far enough to
# be painted with the light of the low sun. The rest (near trees, near mist, ground, hero,
# foreground) is lit by the lights around it.
UNLIT = {"sky", "sun", "haze", "ridge", "far_trees", "mist_far", "mist_mid", "rays"}


def plane(name, atlas, depth, y, width, height, color=None, additive=False):
    """A band of the forest at `depth` (the share of the camera's movement it follows), centred
    on the view at the start (the camera starts at x = VIEW_W / 2)."""
    sprite = {"atlas": atlas, "width": width, "height": height, "layer": LAYERS[name]}
    if name in UNLIT:
        sprite["unlit"] = True
    if color:
        sprite.update({"red": color[0], "green": color[1], "blue": color[2], "alpha": color[3]})
    if additive:
        sprite["additive"] = True
    x, y = at_screen(VIEW_W / 2, y, depth)
    return entity(name, x, y, sprite, {"x": depth, "y": depth, "repeat_x": True})


def scene(name, level):
    look = level["look"]
    sun = look["sun"]
    entities = [
        entity("sky", 0, 0, {"atlas": "sky", "width": VIEW_W, "height": VIEW_H,
                             "layer": LAYERS["sky"], "unlit": True}, {"x": 0, "y": 0}),
        entity("sun", *at_screen(420, 470, 0.05), {
            "atlas": "glow", "width": 900, "height": 700, "red": 255, "green": 196, "blue": 128,
            "alpha": round(200 * sun), "additive": True, "layer": LAYERS["sun"], "unlit": True},
            {"x": 0.05, "y": 0.05}),
        # The low sun as a light: it lights the near planes on its side of the view.
        entity("sunlight", *at_screen(380, 470, 0.3), {
            "atlas": "glow", "width": 1500, "height": 1100, "red": 255, "green": 186,
            "blue": 120, "alpha": round(210 * sun), "light": True, "layer": LAYERS["haze"]},
            {"x": 0.3, "y": 0.3}),
        plane("mist_far", "mist_far", 0.2, 440, 2048, 220, (230, 214, 186, 120)),
        plane("mist_mid", "mist_mid", 0.42, 490, 2048, 240, (196, 206, 198, 110)),
        plane("mist_near", "mist_near", 0.9, 560, 2048, 200, (150, 164, 162, 80)),
        # The low sun again, in front of the far planes: its light bleeds through the trees.
        entity("haze", *at_screen(420, 450, 0.3), {
            "atlas": "glow", "width": 1400, "height": 900, "red": 255, "green": 180,
            "blue": 110, "alpha": round(90 * sun), "additive": True, "layer": LAYERS["haze"],
            "unlit": True},
            {"x": 0.3, "y": 0.3}),
        plane("foreground", "foreground", 1.35, VIEW_H / 2, 2048, VIEW_H),
    ]
    for plane_name, _, depth, ground, *_ in PLANES:
        centre = ground - plane_ground_in_picture(ground) + PLANE_H / 2
        entities.append(plane(plane_name, plane_name, depth, centre, 2048, PLANE_H))
    # Shafts of warm light through the trees, added on top of the far planes.
    for index, (x, angle, alpha) in enumerate([(300, -28, 70), (520, -24, 50), (760, -31, 40)]):
        entities.append(entity(f"ray_{index}", *at_screen(x, -60, 0.6), {
            "atlas": "ray", "width": 180, "height": 900, "pivot_y": 0.0, "red": 255,
            "green": 214, "blue": 150, "alpha": round(alpha * sun), "additive": True,
            "layer": LAYERS["rays"],
            "unlit": True,
        }, {"x": 0.6, "y": 0.6}, {"transform2d.Rotation": {"angle": angle}}))
    entities += ground_of(level)
    entities += obstacles_of(level)
    # The hero: its Position is the middle of its box (HERO_H tall), so the picture hangs from a
    # pivot that puts its feet on the bottom of the box.
    entities.append(hero_entity(level))
    # His animations, the first that holds from the top: going up, falling (no ground under him),
    # walking (slowly, either way), running, standing.
    for clip, priority, condition, first, count, ticks in [
            ("rises", 4, {"field": "transform2d.Velocity.y", "max": -60}, 20, 1, 6),
            ("falls", 3, {"field": "collision2d.Blocked.below", "max": 0.5}, 21, 1, 6),
            ("walks", 2, {"field": "transform2d.Velocity.x", "min": 30, "max": 150,
                          "absolute": True}, 8, 12, 7),
            ("runs", 1, {"field": "transform2d.Velocity.x", "min": 30, "absolute": True}, 8, 12,
             4),
            ("stands", 0, {}, 0, 8, 9)]:
        entities.append({"name": f"hero_{clip}", "components": {"sprite2d.AnimationClip": {
            "set": "hero", "priority": priority, **condition, "first_frame": first,
            "frame_count": count, "ticks_per_frame": ticks}}})
    # The spirit: the glimmer the game is named after. A small being of light that floats after
    # the hero, behind him, and lights the forest around them with a cool, wavering light (it
    # stands out against the warm sun).
    # In the opening it starts high in the trees ahead, and comes down to him.
    spirit_at = (1150, 120) if level.get("opening") else (470, GROUND_Y - 150)
    entities.append(entity("spirit", *spirit_at, {
        "atlas": "spirit", "width": 88, "height": 88,
        "unlit": True, "layer": LAYERS["hero"] + 1,
    }, None, {
        "sprite2d.SpriteAnimation": {"first_frame": 0, "frame_count": SPIRIT_FRAMES,
                                     "ticks_per_frame": 8},
        "follow2d.Follow": {"target": "hero", "offset_x": -56, "offset_y": -86,
                            "stiffness": 0.06, "turn": True, "wave_x": 14, "wave_y": 10,
                            "wave_period": 5},
        "light2d.Light": {"radius": 340, "red": 190, "green": 222, "blue": 255,
                          "intensity": 0.85, "flicker": 0.15, "flicker_speed": 1.5},
        # A trail of sparks left behind as it floats.
        "particles2d.Emitter": {
            "rate": 16, "lifetime": 0.9, "lifetime_spread": 0.4, "speed": 14, "speed_spread": 8,
            "angle": -90, "spread": 360, "gravity_y": -10, "wander": 10, "area_width": 14,
            "area_height": 14, "size": 9, "end_size": 2, "red": 170, "green": 225,
            "blue": 255, "alpha": 230, "fade": True, "additive": True, "unlit": True,
            "layer": LAYERS["hero"] + 1, "max": 30},
    }))
    # Fireflies drifting over the grass, and leaves falling from the trees in front. Particles
    # are not entities: a few emitters spread along the level let them out.
    for index in range(LEVEL_W // 1536):
        x = index * 1536 + 768
        entities.append({"name": f"fireflies_{index}", "components": {
            "transform2d.Position": {"x": x, "y": GROUND_Y - 110},
            "particles2d.Emitter": {
                "rate": 2.5, "lifetime": 6, "lifetime_spread": 2, "speed": 6, "speed_spread": 4,
                "wander": 22, "area_width": 1536, "area_height": 200, "size": 15, "end_size": 11,
                "red": 214, "green": 255, "blue": 150, "alpha": 230, "additive": True,
                "unlit": True, "layer": LAYERS["hero"] + 1, "max": 40},
        }})
        entities.append({"name": f"leaves_{index}", "components": {
            "transform2d.Position": {"x": x, "y": -40},
            "particles2d.Emitter": {
                "rate": 1.2, "lifetime": 11, "lifetime_spread": 3, "speed": 30, "speed_spread": 10,
                "angle": 100, "spread": 40, "gravity_y": 4, "drag": 0.05, "wander": 45,
                "area_width": 1536, "size": 24, "end_size": 22, "red": 255, "green": 255,
                "blue": 255, "additive": False, "atlas": "leaves", "layer": LAYERS["hero"] + 2,
                "max": 30},
        }})
    # The camera follows the hero loosely: small steps and hops leave the view still, it looks
    # ahead of where he runs, and it never shows past the ends of the level or below the ground.
    entities.append(camera(name, level))
    entities += story(level)
    os.makedirs(SCENES, exist_ok=True)
    with open(os.path.join(SCENES, name + ".json"), "w", encoding="utf-8") as out:
        out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
        json.dump({"schema_version": 1, "name": name, "entities": entities}, out, indent=2)
        out.write("\n")


def hero_entity(level):
    """The hero: its Position is the middle of its box (HERO_H tall), so the picture hangs from a
    pivot that puts its feet on the bottom of the box. In the ending nobody steers him: he walks
    on by himself."""
    components = {
        # What he does chooses his animation (the clips), and he looks where he goes.
        "sprite2d.Animator": {"set": "hero"},
        "sprite2d.Facing": {},
        "tag.Tag": {"tag": "hero"},
        "transform2d.Velocity": {"x": 80} if level.get("walker") else {},
        "collision2d.Collider": {"width": HERO_W, "height": HERO_H, "layer": ["hero"],
                                 "mask": ["ground", "crate", "exit"]},
        "collision2d.Body": {},
        "collision2d.Blocked": {},
        "gravity2d.Gravity": {"y": 1800, "terminal_speed": 1100},
        "camera2d.CameraTarget": {},
        # A faint warm light of his own, so that he is never lost in the dark.
        "light2d.Light": {"radius": 190, "red": 255, "green": 212, "blue": 160,
                          "intensity": 0.45, "offset_y": -20},
    }
    if not level.get("walker"):
        components.update({
            "collision2d.Contacts": {},
            # One touch of thorns or of a pod is enough: the level starts over (see story).
            "health.Health": {"amount": 1, "max": 1, "on_zero": "died", "despawn_on_zero": False},
            # Reaching the exit at the end of the level leads to the next one.
            "scene_switch.GoToScene": {"scene": level["next"], "touch_layers": ["exit"]},
            "action_control.Controlled": {"speed": 260, "vertical": False,
                                          "acceleration": 1800, "braking": 2600},
            "jump.Jumper": {"speed": 720, "coyote_time": 0.1, "buffer_time": 0.12,
                            "cut": 0.45},
        })
    if level.get("opening"):
        components["phase.ActiveIn"] = {"phases": ["play"]}  # still during the opening
    return entity("hero", 520, GROUND_Y - HERO_H / 2, {
        "atlas": "hero", "width": CELL, "height": CELL, "pivot_y": HERO_PIVOT_Y,
        "layer": LAYERS["hero"]}, None, components)


LETTERBOX = 0.13  # the share of the height under each black bar, in the opening and the ending


def camera(name, level):
    """The camera follows the hero loosely: small steps and hops leave the view still, it looks
    ahead of where he runs, and it never shows past the ends of the level or below the ground."""
    look = level["look"]
    resting_y = GROUND_Y - HERO_H / 2
    effects = {"bloom": 0.35, "vignette": 0.65, "vignette_size": 0.4, "grain": 0.12,
               "tint_red": look["tint"][0], "tint_green": look["tint"][1],
               "tint_blue": look["tint"][2], "lift_red": 3, "lift_green": 5, "lift_blue": 12}
    components = {
        "transform2d.Position": {"x": VIEW_W / 2, "y": VIEW_H / 2},
        "camera2d.Camera": {"zoom": 1},
        # The dusk under the trees: what the near planes look like where no light reaches.
        "light2d.Ambient": dict(zip(("red", "green", "blue"), look["ambient"])),
        # What the lens adds: a soft glow around the light, darker corners, a little grain, and
        # shadows lifted towards blue.
        "postfx.Effects": effects,
        "camera2d.CameraFollow": {
            "stiffness": 0.08, "offset_y": VIEW_H / 2 - resting_y, "dead_x": 40, "dead_y": 110,
            "look_ahead": 140, "bounded": True, "bound_left": 0, "bound_right": LEVEL_W,
            "bound_top": -2000, "bound_bottom": VIEW_H},
    }
    if level.get("opening"):
        # The bars are on, then slide away; when they are gone the game begins.
        effects["letterbox"] = LETTERBOX
        components["tween2d.Tween"] = {
            "field": "postfx.Effects.letterbox", "from": LETTERBOX, "to": 0, "ticks": 70,
            "delay": 110, "easing": 3, "use_action": True, "action": "opening_done"}
    elif level.get("walker"):
        components["tween2d.Tween"] = {  # the bars come in
            "field": "postfx.Effects.letterbox", "from": 0, "to": LETTERBOX, "ticks": 90,
            "delay": 30, "easing": 3}
    else:
        # A death lets out the black veil that starts the level over.
        components["spawner2d.Shooter"] = {"prefab": "fade_" + level.get("restart", name),
                                           "action": "died", "speed": 0}
    return {"name": "camera", "components": components}


ENDING_TICKS = 720  # how long the ending lasts before the game starts again


def veil(name, extra):
    """A black picture over the whole window, drawn above everything (the interface too), clear
    at first: its tween darkens it. Its size covers any window wherever its Position lands."""
    components = {
        "transform2d.Position": {"x": VIEW_W / 2, "y": VIEW_H / 2},
        "sprite2d.Sprite": {"atlas": "black", "width": 20000, "height": 20000, "screen": True,
                            "alpha": 0, "unlit": True, "layer": 1000},
    }
    components.update(extra)
    return {"name": name, "components": components}


def story(level):
    """What tells the story around the level: the phases of the opening, the end that fades out
    and starts the game again."""
    if level.get("opening"):
        return [{"name": "story", "components": {
            "phase.Phase": {"current": "opening"},
            "phase.Transition": {"action": "opening_done", "from": ["opening"], "to": "play"}}}]
    if level.get("walker"):
        return [veil("the_end", {
            "tween2d.Tween": {"field": "sprite2d.Sprite.alpha", "from": 0, "to": 255,
                              "ticks": 120, "delay": ENDING_TICKS - 140},
            "scene_switch.GoToScene": {"scene": "prologue", "after_ticks": ENDING_TICKS}})]
    return []


def fades():
    """The black veils that start a level over after a death: a prefab for each level, let out
    by the camera on the action died."""
    folder = os.path.join(GAME, "prefabs")
    os.makedirs(folder, exist_ok=True)
    for name in ("forest", "hollow"):
        prefab = veil("fade_" + name, {
            "tween2d.Tween": {"field": "sprite2d.Sprite.alpha", "from": 0, "to": 255,
                              "ticks": FADE_TICKS, "easing": 2},
            "scene_switch.GoToScene": {"scene": name, "after_ticks": FADE_TICKS + 10}})
        with open(os.path.join(folder, prefab["name"] + ".json"), "w", encoding="utf-8") as out:
            out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
            json.dump({"schema_version": 1, "name": prefab["name"],
                       "components": prefab["components"]}, out, indent=2)
            out.write("\n")


def black():
    picture = Image.new("RGBA", (8, 8), INK + (255,))
    save(picture, "black")


def solid(name, x, y, width, height, layer="ground"):
    return {"name": name, "components": {
        "transform2d.Position": {"x": x, "y": y},
        "collision2d.Collider": {"width": width, "height": height, "layer": [layer],
                                 "solid": True},
    }}


def tiles(prefix, start, end, top):
    """Ground blocks (512 wide) from start to end, their grass at `top`: side by side, the last
    one moved back so that it ends at `end` (black earth over black earth does not show)."""
    xs = list(range(start + 256, end - 255, 512))
    if not xs or xs[-1] + 256 < end:
        xs.append(end - 256)
    return [entity(f"{prefix}_{index}", x, top + 128 - 14, {
        "atlas": "ground", "width": 512, "height": 256, "layer": LAYERS["ground"]})
        for index, x in enumerate(xs)]


def ground_of(level):
    """The ground in segments, and the pits between them: a floor at their bottom, thorns on it.
    What stops the hero is a solid box under each segment, and a wall at each end of the level."""
    entities = []
    for index, (start, end) in enumerate(level["segments"]):
        entities += tiles(f"ground_{index}", start, end, GROUND_Y)
        entities.append(solid(f"floor_{index}", (start + end) / 2, GROUND_Y + 150, end - start,
                              300))
    for index, (start, end, depth) in enumerate(level["pits"]):
        bottom = GROUND_Y + depth
        entities += tiles(f"pit_{index}", start - 128, end + 128, bottom)
        entities.append(entity(f"pit_shade_{index}", (start + end) / 2,
                               GROUND_Y + depth / 2 - 7, {
            "atlas": "shade", "width": end - start, "height": depth + 14,
            "layer": LAYERS["ground"] - 1}))
        entities.append(solid(f"pit_floor_{index}", (start + end) / 2, bottom + 100,
                              end - start, 200))
        entities.append(entity(f"thorns_{index}", (start + end) / 2, bottom - THORNS_H / 2 + 6, {
            "atlas": "thorns", "width": end - start, "height": THORNS_H,
            "layer": LAYERS["ground"] + 1}, None, {
            "collision2d.Collider": {"width": end - start, "height": 36, "layer": ["hazard"]},
            "combat2d.ContactDamage": {"amount": 1, "layers": ["hero"]}}))
    entities.append(solid("wall_left", -100, GROUND_Y - 1000, 200, 2400))
    entities.append(solid("wall_right", LEVEL_W + 100, GROUND_Y - 1000, 200, 2400))
    return entities


def obstacles_of(level):
    entities = []
    for name, x, width, height in level["rocks"]:
        # The Position is the middle of the part above the ground (the collider); the picture
        # hangs from a pivot that puts its grass (14 from its top) on the collider's top.
        entities.append(entity(name, x, GROUND_Y - height / 2, {
            "atlas": name, "width": width, "height": height + 60,
            "pivot_y": round((14 + height / 2) / (height + 60), 4),
            "layer": LAYERS["ground"] + 1}, None, {
            "collision2d.Collider": {"width": width - 8, "height": height, "layer": ["ground"],
                                     "solid": True}}))
    for index, x in enumerate(level["crates"]):
        entities.append(entity(f"crate_{index}", x, GROUND_Y - CRATE_H / 2, {
            "atlas": "crate", "width": CRATE_W, "height": CRATE_H,
            "layer": LAYERS["hero"] - 1}, None, {
            "transform2d.Velocity": {},
            "collision2d.Collider": {"width": CRATE_W, "height": CRATE_H, "layer": ["crate"],
                                     "mask": ["ground", "crate"], "solid": True},
            "collision2d.Body": {},
            "collision2d.Blocked": {},
            "collision2d.Pushable": {"speed": 110},
            "gravity2d.Gravity": {"y": 1800, "terminal_speed": 1100}}))
    # Pods go up and down on their thread, between a little above the hero's head and the
    # ground; `phase` (0 to 1) says where on its way each one starts, going down.
    high, low = GROUND_Y - 300, GROUND_Y - 36
    for index, (x, phase) in enumerate(level["pods"]):
        entities.append(entity(f"pod_{index}", x, round(high + (low - high) * phase, 2), {
            "atlas": "pod", "width": POD_W, "height": POD_H, "pivot_y": POD_PIVOT_Y,
            "layer": LAYERS["hero"] + 1}, None, {
            "transform2d.Velocity": {"x": 0, "y": 120},
            "patrol2d.Patrol": {"patrol_x": False, "patrol_y": True, "min_y": high,
                                "max_y": low},
            "collision2d.Collider": {"width": 34, "height": 40, "layer": ["hazard"]},
            "combat2d.ContactDamage": {"amount": 1, "layers": ["hero"]}}))
    if level.get("walker"):
        return entities
    # The exit: past the last obstacle, where the light comes through the trees.
    entities.append(entity("exit", LEVEL_W - 240, GROUND_Y - 160, {
        "atlas": "glow", "width": 420, "height": 520, "red": 255, "green": 220, "blue": 170,
        "alpha": 150, "additive": True, "unlit": True, "layer": LAYERS["hero"] - 2}, None, {
        "collision2d.Collider": {"width": 60, "height": 320, "layer": ["exit"]}}))
    return entities


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
    spirit()
    leaves()
    crate()
    for name, seed in ROCKS.items():
        _, _, width, height = next(rock for level in LEVELS.values() for rock in level["rocks"]
                                   if rock[0] == name)
        rock(name, width, height, seed)
    thorns()
    pod()
    shade()
    black()
    fades()
    for name, level in LEVELS.items():
        scene(name, level)
    print("lueur: pictures, atlases and scene written in", GAME)


if __name__ == "__main__":
    main()
