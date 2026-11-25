#!/usr/bin/env python3
"""Rebuilds the generated files of Complete Universe: every picture, their atlases, the manifest,
the prefabs, the texts and the scenes.

    python3 games/complete_universe/tools/make_assets.py      (numpy and Pillow)

The look (docs/complete_universe): Limbo at the start, Ori as the colours come back. A side view,
as in Ori: the top of the ground shows a little; the hero, a being of white and black ovals with
glowing eyes, turns his chest a little towards us. Everything is drawn here from fixed seeds, so the
same script always gives the same pictures and scenes. The game is drawn for 1920 by 1080 pixels:
the camera's zoom is 1.5, so the world shows 1280 by 720 units, and the pictures are drawn with two
pixels for each unit, sharp at that size.
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, ".."))
ART = os.path.join(GAME, "art")
SCENES = os.path.join(GAME, "scenes")
TEXT = os.path.join(GAME, "text")
PREFABS = os.path.join(GAME, "prefabs")

SCREEN_W, SCREEN_H = 1920, 1080  # the window, in pixels
ZOOM = 1.5  # the camera's zoom at rest: the world shows VIEW_W by VIEW_H units
VIEW_W, VIEW_H = 1280, 720
SUPER = 3  # shapes are drawn this many times bigger, then reduced: smooth edges

HERO_W, HERO_H = 34, 84  # the hero's box: his Position is its middle
CELL = 128  # one picture of the hero, in world units
CELL_PX = 256  # ... and in pixels

SANDBOX_W = 4096  # the test ground runs from x 0 to here
GROUND_Y = 600  # top of the ground

BLACK = (10, 10, 12)
WHITE = (236, 236, 232)
GREY = (120, 120, 124)

# The drawing order of the scenes: lower is further back.
LAYERS = {"sky": -100, "far": -60, "near": -30, "ground": 0, "hero": 10, "dust": 30}


# ---------------------------------------------------------------------------------------------
# Small tools


def save(picture, name, cell=None, extra=None):
    """Writes the picture and its atlas: one cell the size of the picture, or `cell` (w, h)."""
    os.makedirs(ART, exist_ok=True)
    picture.save(os.path.join(ART, name + ".png"), optimize=True)
    width, height = cell or picture.size
    atlas = {"schema_version": 1, "name": name, "image": name + ".png", "cell_width": width,
             "cell_height": height, "filter": "linear"}
    atlas.update(extra or {})
    with open(os.path.join(ART, name + ".json"), "w", encoding="utf-8") as out:
        json.dump(atlas, out, indent=2, ensure_ascii=False)
        out.write("\n")


def canvas(width, height):
    """A transparent picture SUPER times bigger than wanted, and a drawing tool for it."""
    picture = Image.new("RGBA", (width * SUPER, height * SUPER), (0, 0, 0, 0))
    return picture, ImageDraw.Draw(picture)


def reduce(picture, width, height):
    return picture.resize((width, height), Image.LANCZOS)


def oval(draw, cx, cy, rx, ry, color, angle=0.0, scale=SUPER):
    """An ellipse centred on (cx, cy), turned by `angle` degrees, drawn `scale` times bigger."""
    s = scale
    if angle == 0.0:
        draw.ellipse([(cx - rx) * s, (cy - ry) * s, (cx + rx) * s, (cy + ry) * s], fill=color)
        return
    points = []
    a = math.radians(angle)
    for i in range(64):
        t = 2 * math.pi * i / 64
        x, y = rx * math.cos(t), ry * math.sin(t)
        points.append(((cx + x * math.cos(a) - y * math.sin(a)) * s,
                       (cy + x * math.sin(a) + y * math.cos(a)) * s))
    draw.polygon(points, fill=color)


def box(array, half, axis):
    """The mean over 2 * half + 1 pixels along an axis (nothing beyond the edges)."""
    pad = [(0, 0)] * array.ndim
    pad[axis] = (half + 1, half)
    total = np.cumsum(np.pad(array, pad), axis=axis)
    upper = np.take(total, np.arange(2 * half + 1, total.shape[axis]), axis=axis)
    lower = np.take(total, np.arange(0, total.shape[axis] - 2 * half - 1), axis=axis)
    return (upper - lower) / (2 * half + 1)


def blur(array, radius):
    """A float picture blurred by about a Gaussian of that radius, in pixels (three boxes)."""
    if radius <= 0.3:
        return array
    half = max(1, int(round((math.sqrt(4 * radius * radius + 1) - 1) / 2)))
    out = array
    for _ in range(3):
        out = box(box(out, half, 0), half, 1)
    return out


def to_image(rgb, alpha):
    """A picture from colours (0 to 255) and an opacity (0 to 1)."""
    rgba = np.zeros(alpha.shape + (4,), np.uint8)
    rgba[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    rgba[..., 3] = (np.clip(alpha, 0, 1) * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def shrink(rgb, alpha, width, height):
    """Reduces a big picture without dark edges: its colours weighted by their opacity."""
    premultiplied = np.dstack([rgb * alpha[..., None] / 255.0, alpha])
    channels = []
    for index in range(4):
        layer = Image.fromarray(premultiplied[..., index].astype(np.float32), "F")
        channels.append(np.asarray(layer.resize((width, height), Image.LANCZOS),
                                   dtype=np.float64))
    a = np.clip(channels[3], 0, 1)
    colour = np.dstack(channels[:3]) / np.maximum(a[..., None], 1e-4) * 255.0
    return to_image(colour, a)


def noise(width, height, seed, scales=((128, 1.0), (64, 0.5), (32, 0.25), (16, 0.12),
                                        (8, 0.06))):
    """A smooth cloudy field, 0 to about 1."""
    rng = np.random.default_rng(seed)
    field = np.zeros((height, width))
    total = 0.0
    for scale, weight in scales:
        small = rng.uniform(0, 1, (height // scale + 3, width // scale + 3))
        big = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize(
            ((width // scale + 3) * scale, (height // scale + 3) * scale), Image.BICUBIC),
            dtype=np.float64)[:height, :width] / 255.0
        field += big * weight
        total += weight
    return field / total


def sheet_of(frames, columns, cell_w, cell_h):
    rows = (len(frames) + columns - 1) // columns
    sheet = Image.new("RGBA", (cell_w * columns, cell_h * rows), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.paste(frame, ((index % columns) * cell_w, (index // columns) * cell_h))
    return sheet


# ---------------------------------------------------------------------------------------------
# The hero: ovals, the half on the left of the screen white and glowing, the other black, two
# glowing eyes (docs/complete_universe/images). He never turns over: the white hand is always on
# the left of the screen, the black one on the right, as the mouse buttons. Walking, he turns his
# face and chest towards where he goes; at rest, he looks at us.

HS = CELL_PX * SUPER // CELL  # pixels of the big drawing for one world unit (6)
HN = CELL * HS  # side of the big drawing
FEET = CELL / 2 + HERO_H / 2  # where the feet are in a cell, in world units
LEG, ARM = 25.0, 23.0  # lengths of the limbs


class Pose:
    """Where the parts of the hero are in one picture."""

    def __init__(self, **values):
        self.turn = 0.0  # -1 looks left, 1 right
        self.lean = 0.0  # degrees, the top towards the right when positive
        self.leg_a = -3.0  # the white leg, degrees from straight down, positive towards the right
        self.leg_b = 3.0
        self.leg_len_a = LEG
        self.leg_len_b = LEG
        self.arm_a = -9.0  # the white arm
        self.arm_b = 9.0
        self.stretch = 1.0  # of the chest, up and down
        self.lift = 0.0  # the hips above where the legs would put them
        self.drop = 0.0  # the whole body lower (a crouch)
        self.blink = 1.0  # how open the eyes are
        self.look_y = 0.0  # the eyes up (negative) or down
        self.hole = None  # a small black hole pulling a foot: (x, y, size)
        self.__dict__.update(values)


def ellipse_mask(cx, cy, rx, ry, angle=0.0):
    mask = Image.new("L", (HN, HN), 0)
    oval(ImageDraw.Draw(mask), cx, cy, rx, ry, 255, angle, HS)
    return np.asarray(mask, dtype=np.float64) / 255.0


def limb(joint_x, joint_y, angle, length, width):
    """An oval hanging from a joint, turned `angle` degrees from straight down."""
    a = math.radians(angle)
    cx = joint_x + math.sin(a) * length / 2
    cy = joint_y + math.cos(a) * length / 2
    return ellipse_mask(cx, cy, width, length / 2 + 2.5, -angle)


def hero_frame(pose):
    """One picture of the hero, CELL_PX square, from a Pose."""
    cx = CELL / 2 + pose.lean * 0.12
    reach = max(math.cos(math.radians(pose.leg_a)) * pose.leg_len_a,
                math.cos(math.radians(pose.leg_b)) * pose.leg_len_b)
    hip_y = FEET - reach - pose.lift + pose.drop
    chest_y = hip_y - 13 * pose.stretch
    lean = math.radians(pose.lean)
    shoulder_y = chest_y - 11 * pose.stretch
    head_x = cx + math.sin(lean) * 30 + pose.turn * 2.2
    head_y = chest_y - 31.5 * pose.stretch + pose.drop * 0.2
    split = cx + pose.turn * 1.6 + math.sin(lean) * 14

    xs = np.arange(HN)[None, :] / HS
    left_side = np.clip((split - xs) * 2 + 0.5, 0, 1) * np.ones((HN, 1))
    # The parts, from the back to the front: (mask, side) with side "w", "b" or "split".
    parts = [
        (limb(cx - 5.5, hip_y, pose.leg_a, pose.leg_len_a, 6.4), "w"),
        (limb(cx + 5.5, hip_y, pose.leg_b, pose.leg_len_b, 6.4), "b"),
        (ellipse_mask(cx + math.sin(lean) * 13, chest_y, 12.5, 17.5 * pose.stretch, -pose.lean),
         "split"),
        (ellipse_mask(head_x, head_y, 17.0, 16.0), "split"),
        (limb(cx - 10.5 + math.sin(lean) * 22, shoulder_y, pose.arm_a, ARM, 5.3), "w"),
        (limb(cx + 10.5 + math.sin(lean) * 22, shoulder_y, pose.arm_b, ARM, 5.3), "b"),
    ]
    rgb = np.zeros((HN, HN, 3))
    alpha = np.zeros((HN, HN))
    whiteness = np.zeros((HN, HN))
    for mask, side in parts:
        inner = np.clip(blur(mask, 3.2 * HS) * 1.6 - 0.25, 0, 1)
        rim = np.clip((mask - blur(mask, 1.3 * HS)) * 2.4, 0, 1)
        white_tone = 196 + 52 * inner
        black_tone = 9 + 78 * rim
        share = {"w": np.ones((HN, HN)), "b": np.zeros((HN, HN)), "split": left_side}[side]
        tone = white_tone * share + black_tone * (1 - share)
        colour = np.dstack([tone, tone, tone * 1.02 + (1 - share) * 3])
        rgb = rgb * (1 - mask[..., None]) + colour * mask[..., None]
        alpha = np.maximum(alpha, mask)
        whiteness = whiteness * (1 - mask) + share * mask
    # The white half glows (docs: "Le personnage").
    glow = blur(whiteness * alpha, 7 * HS) * 0.55
    # The eyes: white and glowing on both halves, ringed with grey on the white one.
    eye_y = head_y - 0.5 + pose.look_y
    eyes = np.zeros((HN, HN))
    for side in (-1, 1):
        far = 1 - 0.28 * max(0.0, -side * pose.turn)
        eyes = np.maximum(eyes, ellipse_mask(head_x + side * 6.8 + pose.turn * 4.2, eye_y,
                                             4.1 * far, 5.1 * pose.blink + 0.25))
    ring = np.clip(blur(eyes, 1.5 * HS) * 2.6 - eyes, 0, 1) * whiteness
    rgb = rgb * (1 - ring[..., None] * 0.7)
    eye_glow = blur(eyes, 2.6 * HS) * 0.9
    rgb = rgb * (1 - eyes[..., None]) + 255 * eyes[..., None]
    # The small black hole that pulls a foot as he walks.
    if pose.hole is not None:
        hx, hy, size = pose.hole
        if size > 0.05:
            disc = ellipse_mask(hx, hy, 3.4 * size, 3.4 * size)
            ring_mask = np.clip(ellipse_mask(hx, hy, 4.4 * size, 4.4 * size) - disc, 0, 1)
            halo = blur(ellipse_mask(hx, hy, 4.4 * size, 4.4 * size), 3 * HS * size) * 0.6
            rgb = rgb * (1 - ring_mask[..., None]) + np.array([225, 232, 245]) * ring_mask[..., None]
            rgb = rgb * (1 - disc[..., None])
            alpha = np.maximum(alpha, np.maximum(disc, ring_mask))
            glow = np.maximum(glow, halo)
    # Behind the body, the glow; the eyes' glow over everything around them.
    body = alpha
    out_alpha = body + glow * (1 - body)
    out_rgb = (rgb * body[..., None] + 255 * (glow * (1 - body))[..., None]) / np.maximum(
        out_alpha[..., None], 1e-4)
    shine = eye_glow * (1 - eyes)
    out_rgb = out_rgb + (255 - out_rgb) * shine[..., None]
    out_alpha = np.maximum(out_alpha, shine)
    return shrink(out_rgb, out_alpha, CELL_PX, CELL_PX)


def idle_poses(count=32):
    poses = []
    for index in range(count):
        breath = math.sin(2 * math.pi * index / count)
        blink = {26: 0.45, 27: 0.08, 28: 0.45}.get(index, 1.0)
        poses.append(Pose(stretch=1 + 0.025 * breath, drop=-0.5 * breath,
                          arm_a=-8 - 2.5 * breath, arm_b=8 + 2.5 * breath, blink=blink))
    return poses


def walk_poses(facing, count=16):
    """A step after the other: before each foot lands, a small black hole opens where it will
    land and pulls it there (GDD §5, docs: "le trou noir attire la jambe")."""
    poses = []
    swing = 28.0
    for index in range(count):
        phase = index / count
        s = math.sin(2 * math.pi * phase)
        c = math.cos(2 * math.pi * phase)
        leg_a, leg_b = swing * s, -swing * s
        # Which leg moves forward now, and how far it is in its swing (0 to 1).
        a_forward = facing * c > 0
        local = (phase * 2 + (0.25 if facing > 0 else 0.75) * 2) % 1.0
        # The hole opens as the leg leaves, is biggest as the foot lands in it, then closes.
        size = local ** 0.8 if local < 0.85 else (1 - local) / 0.15 * 0.85 ** 0.8
        hip_x = CELL / 2 + (-5.5 if a_forward else 5.5)
        reach = LEG * 1.18
        hole = (hip_x + facing * math.sin(math.radians(swing)) * reach + facing * 6,
                FEET - 3, size)
        pull = 2.5 * size
        poses.append(Pose(turn=0.85 * facing, lean=5 * facing, leg_a=leg_a, leg_b=leg_b,
                          leg_len_a=LEG + (pull if a_forward else 0),
                          leg_len_b=LEG + (0 if a_forward else pull),
                          arm_a=-8 - 0.55 * swing * s, arm_b=8 + 0.55 * swing * s,
                          hole=hole))
    return poses


def charge_poses(count=8):
    """Space held: he crouches and lifts his arms towards the black hole over his head."""
    poses = []
    for index in range(count):
        k = index / (count - 1)
        poses.append(Pose(leg_a=-(4 + 14 * k), leg_b=4 + 14 * k, leg_len_a=LEG * (1 - 0.22 * k),
                          leg_len_b=LEG * (1 - 0.22 * k), stretch=1 - 0.07 * k,
                          arm_a=-(10 + 145 * k), arm_b=10 + 145 * k, look_y=-1.6 * k))
    return poses


def rise_poses(count=6):
    poses = []
    for index in range(count):
        sway = math.sin(2 * math.pi * index / count)
        poses.append(Pose(stretch=1.05, leg_a=-2 - sway, leg_b=2 - sway, leg_len_a=LEG + 1,
                          leg_len_b=LEG + 1, arm_a=-(150 + 6 * sway), arm_b=150 - 6 * sway,
                          look_y=-1.2))
    return poses


def fall_poses(count=6):
    poses = []
    for index in range(count):
        sway = math.sin(2 * math.pi * index / count)
        poses.append(Pose(leg_a=-12 - 3 * sway, leg_b=12 - 3 * sway, arm_a=-(95 + 8 * sway),
                          arm_b=95 - 8 * sway, look_y=1.0))
    return poses


# The animations of the hero: (name, frames, ticks per frame), in the order of the atlas.
HERO_CLIPS = [("idle", idle_poses(), 4), ("walk_right", walk_poses(1), 3),
              ("walk_left", walk_poses(-1), 3), ("charge", charge_poses(), 4),
              ("rise", rise_poses(), 5), ("fall", fall_poses(), 5)]
HERO_COLUMNS = 16


def hero_sheet():
    frames = []
    for _, poses, _ in HERO_CLIPS:
        frames += [hero_frame(pose) for pose in poses]
    return sheet_of(frames, HERO_COLUMNS, CELL_PX, CELL_PX)


def hero_curled():
    """The hero curled up asleep, as he drifts in his barrier: knees up, head down."""
    pose = Pose(leg_a=-115, leg_b=-100, leg_len_a=LEG * 0.85, leg_len_b=LEG * 0.85, arm_a=-60,
                arm_b=-35, stretch=0.82, drop=-14, blink=0.08, look_y=2)
    return hero_frame(pose)


# ---------------------------------------------------------------------------------------------
# The powers

# The powers (GDD §6), in the order of the game's power list; the last ones are what two hands make
# together. A hand that uses its power glows in its colour (GDD §3).
POWERS = ["black", "white", "blue", "green", "red", "yellow", "orange", "violet", "pink",
          "harvest"]
POWER_COLOURS = {"black": (14, 14, 16), "white": (240, 240, 236), "blue": (70, 130, 230),
                 "green": (80, 190, 110), "red": (220, 60, 50), "yellow": (240, 210, 80),
                 "orange": (240, 140, 50), "violet": (150, 90, 210), "pink": (240, 130, 190)}
ORB = 64  # one picture of a hand's glow, in pixels


def orbs():
    """One glow for each power a hand can hold (the black one a tiny black hole), then nothing
    for an empty hand: the cells of the orb atlas, chosen by the power of the hand."""
    y, x = np.mgrid[0:ORB, 0:ORB].astype(np.float64)
    r = np.hypot(x - ORB / 2 + 0.5, y - ORB / 2 + 0.5) / (ORB / 2)
    frames = []
    for name, colour in POWER_COLOURS.items():
        glow = np.clip(1 - r, 0, 1) ** 2.2
        core = np.clip((0.32 - r) / 0.08, 0, 1)
        if name == "black":
            ring = np.exp(-((r - 0.34) / 0.06) ** 2)
            alpha = np.clip(core + ring + glow * 0.5, 0, 1)
            tone = np.clip(ring * 255 + glow * 120, 0, 255) * (1 - core)
            rgb = np.dstack([tone * 0.92, tone * 0.95, tone])
        else:
            alpha = np.clip(core + glow * 0.8, 0, 1)
            rgb = np.dstack([np.full_like(r, c) for c in colour]) * 0.7 + 255 * 0.3 * core[
                ..., None]
        frames.append(to_image(rgb, alpha))
    frames.append(Image.new("RGBA", (ORB, ORB), (0, 0, 0, 0)))
    return sheet_of(frames, len(frames), ORB, ORB)


HOLE = 512  # the picture of a black hole, drawn for a radius of 1 (a sprite 2 units wide)
HEART = 0.4  # the share of the radius that swallows (attractor2d inner)


def swirl(r, theta, seed, arms=40):
    """Streaks turning around a centre, 0 to 1, as matter falling into a black hole."""
    rng = np.random.default_rng(seed)
    total = np.zeros_like(r)
    log_r = np.log(np.maximum(r, 1e-3))
    for k in range(3, arms):
        total += rng.uniform(0.2, 1.0) / math.sqrt(k) * np.sin(
            k * theta + 2.2 * k * log_r * 0.35 + rng.uniform(0, 2 * math.pi))
    total /= np.abs(total).max()
    return np.clip(0.5 + 0.5 * total, 0, 1)


def black_hole():
    """A black hole (docs: "Les trous noirs"): a black heart, a thin ring of light hugging it,
    a disc of bright streaks turning in, and a faint halo out to the edge of its pull. It is
    turned as it plays, so the streaks spin."""
    size = HOLE
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    dx, dy = x - size / 2 + 0.5, y - size / 2 + 0.5
    r = np.hypot(dx, dy) / (size / 2)
    theta = np.arctan2(dy, dx)
    streaks = swirl(r, theta, 5)
    core = np.clip((HEART - r) / 0.012, 0, 1)
    photon = np.exp(-((r - HEART - 0.012) / 0.012) ** 2)
    disc = np.exp(-((r - HEART - 0.07) / 0.075) ** 2) * (0.25 + 0.95 * streaks ** 2.2)
    outer = np.exp(-((r - HEART - 0.2) / 0.12) ** 2) * streaks ** 4 * 0.45
    halo = np.clip(1 - (r - HEART) / (1 - HEART), 0, 1) ** 2.6 * 0.42
    # One side of the disc a little brighter, as it comes towards us.
    side = 1 + 0.25 * np.cos(theta + 2.3)
    light = np.clip((photon * 1.1 + disc * side + outer) * (1 - core), 0, 1.4)
    alpha = np.clip(core + light + halo, 0, 1) * np.clip((1 - r) * 12, 0, 1)
    cold = np.array([200, 222, 255])
    warm = np.array([210, 200, 185])
    rgb = (cold[None, None, :] * np.clip(light, 0, 1)[..., None] +
           warm[None, None, :] * (halo * (1 - np.clip(light, 0, 1)))[..., None])
    rgb = rgb / np.maximum(np.clip(light, 0, 1) + halo * (1 - np.clip(light, 0, 1)), 1e-4)[
        ..., None]
    rgb = rgb * np.clip(light + halo * 0.6, 0, 1)[..., None] / np.maximum(alpha, 1e-4)[..., None]
    rgb = np.minimum(rgb + 255 * np.clip(photon - 0.6, 0, 1)[..., None], 255)
    rgb = rgb * (1 - core[..., None])
    return to_image(rgb, alpha)


BARRIER = 512  # a cell of the barrier atlas


def bubble(strength=1.0, size=BARRIER):
    """A bubble of the white (docs: "La barrière"): clear glass, a bright rim thicker at the
    edge, a light on its top, the reflection of the ground in its bottom, faint hexagons."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    nx, ny = (x - size / 2 + 0.5) / (size / 2), (y - size / 2 + 0.5) / (size / 2)
    edge = 0.93
    r = np.hypot(nx, ny) / edge
    inside = np.clip((1 - r) * size * 0.5, 0, 1)
    fresnel = np.clip((r - 0.45) / 0.55, 0, 1) ** 2.4
    rim = np.exp(-((r - 0.985) / 0.022) ** 2)
    # The glow outside fades out before the edge of the cell, so that no square shows.
    border = np.clip((1 - np.maximum(np.abs(nx), np.abs(ny))) / 0.06, 0, 1)
    outer = np.exp(-np.clip(r - 1, 0, None) / 0.035) * (r > 1) * 0.55 * border
    top = np.exp(-(((nx + 0.3) / 0.38) ** 2 + ((ny + 0.62) / 0.13) ** 2)) * 0.45
    bottom = np.exp(-((nx / 0.6) ** 2 + ((ny - 0.55) / 0.16) ** 2)) * 0.22
    scale = 22.0
    px, py = x / scale, y / scale
    q = px * math.sqrt(3) / 3 - py / 3
    hexes = np.abs(((q + py * 2 / 3 / 2) % 1) - 0.5) + np.abs(((py * 2 / 3) % 1) - 0.5)
    lines = np.clip(1 - np.abs(hexes - 0.5) * 9, 0, 1) * 0.07 * fresnel
    light = inside * (0.07 + 0.62 * fresnel + top + bottom + lines) + rim * 1.0 + outer
    alpha = np.clip(light * strength, 0, 1)
    tint = np.clip(fresnel * 0.6 + rim, 0, 1)
    rgb = np.dstack([175 + 65 * tint, 215 + 40 * tint, 212 + 38 * tint])
    return to_image(rgb, alpha)


BUTTON = (512, 128)  # a cell of the button atlas


def button(focused):
    """A button of the menus: a long capsule of glass, like a flattened bubble; focused, its rim
    is bright and light fills it."""
    width, height = BUTTON
    y, x = np.mgrid[0:height, 0:width].astype(np.float64)
    half = height / 2 - 6
    # The distance to the capsule's middle segment, as a share of its half height.
    cx = np.clip(x, half + 6, width - half - 6)
    r = np.hypot(x - cx, y - height / 2) / half
    inside = np.clip((1 - r) * half * 0.6, 0, 1)
    rim = np.exp(-((r - 0.97) / (0.05 if focused else 0.035)) ** 2)
    border = np.clip(np.minimum(np.minimum(x, width - 1 - x), np.minimum(y, height - 1 - y))
                     / 22.0, 0, 1)
    outer = (np.exp(-np.clip(r - 1, 0, None) / 0.07) * (r > 1) * (0.5 if focused else 0.15)
             * border ** 2)
    sheen = np.exp(-((y - height * 0.3) / (height * 0.12)) ** 2) * inside * 0.12
    fill = inside * ((0.22 + 0.25 * r ** 3) if focused else (0.06 + 0.1 * r ** 3))
    alpha = np.clip(fill + sheen + rim * (1.0 if focused else 0.55) + outer, 0, 1)
    rgb = np.dstack([np.full_like(r, 236), np.full_like(r, 244), np.full_like(r, 248)])
    return to_image(rgb, alpha)


def dome(size=2048):
    """The great barrier over the planet, seen from inside: only its rim, a thin arc of light
    with a soft glow on both sides."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    r = np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) / (size / 2)
    rim = np.exp(-((r - 0.96) / 0.0018) ** 2) * 0.9
    glow = np.exp(-((r - 0.96) / 0.009) ** 2) * 0.3
    alpha = np.clip(rim + glow, 0, 1)
    return to_image(np.dstack([np.full_like(r, 215), np.full_like(r, 240), np.full_like(r, 238)]),
                    alpha)


def wall_pane(size=BARRIER):
    """A wall of the white: a flat pane of glass seen edge on, its two faces bright and the
    inside clear. Stretched along its length, it stays the same across."""
    y = np.mgrid[0:size, 0:size][0].astype(np.float64)
    v = np.abs(y - size / 2 + 0.5) / (size / 2)  # 0 in the middle, 1 at the faces
    faces = np.exp(-((v - 0.86) / 0.07) ** 2)
    core = np.exp(-(v / 0.12) ** 2) * 0.35
    inside = np.clip((0.92 - v) * 20, 0, 1) * 0.22
    alpha = np.clip(faces + core + inside, 0, 1) * np.clip((1 - v) * 30, 0, 1)
    tint = np.clip(faces + core, 0, 1)
    rgb = np.dstack([180 + 70 * tint, 220 + 35 * tint, 218 + 37 * tint])
    return to_image(rgb, alpha)


def barriers():
    """The cells of the barrier atlas: a bubble, a wall, what a hand is about to raise."""
    return sheet_of([bubble(), wall_pane(), bubble(0.45)], 3, BARRIER, BARRIER)


def harvest():
    """The harvest: a bubble with a small black hole turning in its middle, which swallows
    everything inside and sorts it (GDD §7)."""
    glass = np.asarray(bubble(0.8, HOLE), dtype=np.float64) / 255.0
    small = np.asarray(black_hole().resize((HOLE // 3, HOLE // 3), Image.LANCZOS),
                       dtype=np.float64) / 255.0
    core = np.zeros_like(glass)
    o = HOLE // 2 - HOLE // 6
    core[o:o + small.shape[0], o:o + small.shape[1]] = small
    alpha = np.clip(glass[..., 3] + core[..., 3] * (1 - glass[..., 3]), 0, 1)
    rgb = (glass[..., :3] * glass[..., 3:4] * (1 - core[..., 3:4]) +
           core[..., :3] * core[..., 3:4])
    rgb = rgb / np.maximum(alpha[..., None], 1e-6)
    return to_image(rgb * 255, alpha)


ROCK_PX = 96  # pixels of a rock picture, for a 40-unit sprite
ROCK = 40


def rock(seed, colour=(46, 46, 50), light=(150, 152, 158), spiral=False):
    """A rock (matter) or a fossil (carbon): a lumpy stone, lit from above."""
    rng = np.random.default_rng(seed)
    n = ROCK_PX * SUPER
    mask_picture = Image.new("L", (n, n), 0)
    points = []
    for i in range(14):
        t = 2 * math.pi * i / 14
        radius = 0.36 * rng.uniform(0.78, 1.05)
        points.append(((0.5 + radius * math.cos(t)) * n, (0.56 + radius * 0.78 * math.sin(t)) * n))
    ImageDraw.Draw(mask_picture).polygon(points, fill=255)
    mask = blur(np.asarray(mask_picture, dtype=np.float64) / 255.0, n * 0.012)
    mask = np.clip((mask - 0.5) * 6 + 0.5, 0, 1)
    shade = np.clip(blur(mask, n * 0.05) * 1.8 - 0.5, 0, 1)
    up = np.clip(mask - np.roll(blur(mask, n * 0.02), int(n * 0.05), axis=0), 0, 1) * 2.5
    base = np.array(colour, dtype=np.float64)
    lit = np.array(light, dtype=np.float64)
    tone = np.clip(up + shade * 0.25, 0, 1)[..., None]
    rgb = base * (1 - tone) + lit * tone
    grain = noise(n, n, seed + 3, ((24, 1.0), (8, 0.6), (3, 0.4)))
    rgb = rgb * (0.85 + 0.3 * grain[..., None])
    if spiral:
        spiral_picture = Image.new("L", (n, n), 0)
        draw = ImageDraw.Draw(spiral_picture)
        cx, cy = 0.5 * n, 0.57 * n
        line = []
        for i in range(120):
            t = i / 119 * 3.4 * math.pi
            radius = (0.012 + t * 0.022) * n
            line.append((cx + radius * math.cos(t), cy + radius * 0.8 * math.sin(t)))
        draw.line(line, fill=255, width=max(2, n // 120))
        groove = blur(np.asarray(spiral_picture, dtype=np.float64) / 255.0, 1.5) * mask
        rgb = rgb * (1 - 0.45 * groove[..., None])
    return shrink(rgb, mask, ROCK_PX, ROCK_PX)


INVENTORY = 128


def inventory_bubbles():
    """What the body keeps (GDD §7, inventory): a bubble for each resource, drawn full; its size
    shows how much there is. Matter is a black hole, carbon a white light, each in glass."""
    frames = []
    glass = np.asarray(bubble(0.9, INVENTORY), dtype=np.float64) / 255.0
    y, x = np.mgrid[0:INVENTORY, 0:INVENTORY].astype(np.float64)
    r = np.hypot(x - INVENTORY / 2 + 0.5, y - INVENTORY / 2 + 0.5) / (INVENTORY / 2 * 0.93)
    for fill in (14, 236):
        inside = np.clip((0.9 - r) * 40, 0, 1)
        alpha = np.clip(glass[..., 3] + inside * 0.92, 0, 1)
        rgb = glass[..., :3] * 255 * glass[..., 3:4] + fill * inside[..., None] * (
            1 - glass[..., 3:4])
        rgb = rgb / np.maximum(alpha[..., None], 1e-4)
        frames.append(to_image(rgb, alpha))
    return sheet_of(frames, 2, INVENTORY, INVENTORY)


FONT_GLYPHS = (" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
               "abcdefghijklmnopqrstuvwxyz{|}~àâäçéèêëîïôöùûüÿœÀÉÈÊÇ«»…’←→↑↓·")
FONT_W, FONT_H = 32, 64


def save_font():
    """The letters of the texts, each in a cell of FONT_W by FONT_H, white on clear."""
    face = ImageFont.truetype("/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf", 56 * SUPER)
    sheet = Image.new("RGBA", (FONT_W * len(FONT_GLYPHS), FONT_H), (0, 0, 0, 0))
    for index, glyph in enumerate(FONT_GLYPHS):
        cell = Image.new("L", (FONT_W * SUPER, FONT_H * SUPER), 0)
        ImageDraw.Draw(cell).text((FONT_W * SUPER / 2, FONT_H * SUPER * 0.56), glyph, fill=255,
                                  font=face, anchor="mm")
        alpha = cell.resize((FONT_W, FONT_H), Image.LANCZOS)
        letter = Image.new("RGBA", (FONT_W, FONT_H), (255, 255, 255, 0))
        letter.putalpha(alpha)
        sheet.paste(letter, (index * FONT_W, 0))
    save(sheet, "font", cell=(FONT_W, FONT_H), extra={"glyphs": FONT_GLYPHS})


def glow():
    """A soft white glow, bright in the middle."""
    size = 256
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    r = np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) / (size / 2)
    return to_image(np.full((size, size, 3), 255.0), np.clip(1 - r, 0, 1) ** 2.2)


def planet():
    """The planet of the tutorial seen from space: a grey disc lit from the upper left, with
    craters, its night side lost in the dark, a thin bright edge of air."""
    size = 768
    rng = np.random.default_rng(21)
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    cx = cy = size / 2 - 0.5
    r = np.hypot(x - cx, y - cy) / (size / 2 - 6)
    inside = r <= 1
    nz = np.sqrt(np.clip(1 - r ** 2, 0, 1))
    nx, ny = (x - cx) / (size / 2), (y - cy) / (size / 2)
    light = np.clip(-0.55 * nx - 0.6 * ny + 0.58 * nz, 0, 1)
    value = 26 + 175 * light ** 0.9 * (0.85 + 0.3 * noise(size, size, 4))
    for _ in range(36):
        px, py = rng.uniform(-0.8, 0.8, 2)
        pr = rng.uniform(0.025, 0.11)
        d = np.hypot(nx - px, ny - py) / pr
        value -= np.where(d < 1, 34 * (1 - d) * light, 0)
        value += np.where((d > 0.85) & (d < 1.1), 20 * light, 0)
    rim = np.exp(-((r - 1) / 0.015) ** 2) * np.clip(light * 2 + 0.15, 0, 1)
    alpha = np.clip(np.where(inside, np.clip((1 - r) * 200, 0, 1), 0) + rim, 0, 1)
    value = np.where(inside, value, 255)
    rgb = np.dstack([value, value, value * 1.04])
    return to_image(rgb, alpha)


BAND_SLOPE = -0.42  # the milky band of the skies rises to the right, as in the docs' pictures


def band_of(width, height, thickness=0.28):
    """How much of the band each pixel is in, 0 to 1."""
    y = np.linspace(0, 1, height)[:, None]
    x = np.linspace(0, 1, width)[None, :]
    middle = 0.62 + BAND_SLOPE * (x - 0.5)
    return np.exp(-((y - middle) / thickness) ** 2)


def streaks(width, height, seed, scales, stretch):
    """Noise stretched along x, then slid down its columns so that its streaks follow the band
    (drawn taller than needed: no seam)."""
    slope = -BAND_SLOPE * height / width  # rows to slide down for each column
    tall = height + int(math.ceil(slope * width)) + 2
    small = noise(max(8, width // stretch), tall, seed, scales)
    long = np.asarray(Image.fromarray((small * 65535).astype(np.uint16), "I;16").convert(
        "I").resize((width, tall), Image.BICUBIC), dtype=np.float64) / 65535.0
    rows = (np.arange(height)[:, None] + (slope * np.arange(width))[None, :]).astype(int)
    return long[rows, np.arange(width)[None, :]]


def nebula(width, height, seed, palette, density=1.0):
    """Clouds of dust along a band across the sky: long filaments, darker lanes of dust, coloured
    along a palette (dark to bright)."""
    big = streaks(width, height, seed, ((128, 1.0), (64, 0.6), (32, 0.3)), 3)
    fine = streaks(width, height, seed + 1, ((16, 1.0), (8, 0.6), (4, 0.35), (2, 0.2)), 4)
    lanes = streaks(width, height, seed + 2, ((48, 1.0), (24, 0.5), (12, 0.25)), 5)
    band = band_of(width, height)
    body = np.clip((big - 0.35) * 2.2, 0, 1) * band * density
    wisps = body * (0.35 + 1.1 * fine ** 1.6)
    wisps *= 1 - 0.75 * np.clip((lanes - 0.55) * 5, 0, 1)
    wisps = np.clip(wisps, 0, 1)
    colours = np.array(palette, dtype=np.float64)
    t = np.clip(big * 0.9 + fine * 0.5 - 0.35, 0, 0.999) * (len(colours) - 1)
    low = np.floor(t).astype(int)
    frac = (t - low)[..., None]
    rgb = colours[low] * (1 - frac) + colours[np.minimum(low + 1, len(colours) - 1)] * frac
    return rgb, wisps


def stars(width, height, seed, count, coloured=False):
    """Stars: many small and faint, crowded in the band; a few big with a soft halo and a cross
    of light."""
    rng = np.random.default_rng(seed)
    band = band_of(width, height, 0.3)
    xs = rng.uniform(0, width, count * 4).astype(int)
    ys = rng.uniform(0, height, count * 4).astype(int)
    keep = rng.uniform(0, 1, count * 4) < 0.25 + 0.75 * band[ys, xs]
    xs, ys = xs[keep][:count * 2], ys[keep][:count * 2]
    brightness = rng.uniform(0.15, 1.0, xs.size) ** 3
    light = np.zeros((height, width, 3))
    palette = np.array([[255, 210, 180], [190, 210, 255], [255, 255, 255], [255, 160, 200],
                        [170, 255, 230]], dtype=np.float64)
    tones = palette[rng.integers(0, 5, xs.size)] if coloured else np.full((xs.size, 3), 255.0)
    np.add.at(light, (ys, xs), tones * brightness[:, None])
    flares = np.zeros((height, width))
    big = rng.choice(np.arange(width * height), size=max(1, count // 80))
    flares[big // width, big % width] = rng.uniform(0.6, 1.0, big.size)
    halo = blur(flares, 3.0) * 16
    cross = np.zeros_like(flares)
    for shift in range(1, 12):
        fade = (1 - shift / 12) ** 2
        cross += (np.roll(flares, shift, 1) + np.roll(flares, -shift, 1) +
                  np.roll(flares, shift, 0) + np.roll(flares, -shift, 0)) * fade * 0.45
    soft = blur(light.mean(axis=2) / 255.0, 0.6)
    total = light / 255.0 + (soft * 0.5 + halo + cross + flares)[..., None]
    return np.clip(total, 0, 1)


def sky(coloured=False):
    """The sky behind everything, the size of the screen: black, grey clouds of dust and stars
    (docs: the character's picture); in colour for the opening (docs: "le ciel")."""
    w, h = SCREEN_W, SCREEN_H
    if coloured:
        rgb, dust = nebula(w, h, 31, [(14, 50, 80), (30, 130, 150), (70, 200, 205),
                                      (120, 150, 240), (235, 180, 245)], 1.5)
        star = stars(w, h, 7, 6000, coloured=True)
    else:
        rgb, dust = nebula(w, h, 31, [(60, 60, 68), (105, 105, 115), (150, 150, 160),
                                      (200, 200, 210)], 0.75)
        star = stars(w, h, 7, 5000)
    base = np.dstack([np.full((h, w), 4.0), np.full((h, w), 4.0), np.full((h, w), 7.0)])
    picture = base * (1 - dust[..., None] * 0.9) + rgb * dust[..., None] * 0.9
    picture = picture + (255 - picture) * star * (1 - dust[..., None] * 0.4)
    return to_image(picture, np.ones((h, w)))


def nebula_veil(coloured=False):
    """A veil of dust that drifts before the sky (a little parallax): more depth."""
    w, h = 1600, 900
    palette = ([(30, 80, 120), (60, 150, 170), (150, 120, 220)] if coloured else
               [(110, 110, 120), (150, 150, 160), (190, 190, 200)])
    rgb, dust = nebula(w, h, 58 if coloured else 59, palette, 0.9 if coloured else 0.6)
    # Its edges fade out: no frame shows, whatever the camera does.
    y = np.linspace(-1, 1, h)[:, None]
    x = np.linspace(-1, 1, w)[None, :]
    edges = np.clip((1 - np.abs(x)) * 4, 0, 1) * np.clip((1 - np.abs(y)) * 4, 0, 1)
    return to_image(rgb, dust * (0.55 if coloured else 0.4) * edges).filter(ImageFilter.GaussianBlur(1.2))


HILLS_W, HILLS_H = 2048, 400  # world units of a band of hills (drawn at one pixel each)


def hills(seed, height, tone, trees, fog):
    """A band of hills in silhouette, as in Limbo, endless side by side: soft tops, thin dead
    trees, lighter towards their foot (the fog)."""
    rng = np.random.default_rng(seed)
    w, h = HILLS_W, HILLS_H
    xs = np.arange(w)
    top = np.zeros(w)
    for k, amp in ((1, 0.5), (2, 0.25), (3, 0.15), (5, 0.07), (9, 0.03)):
        top += amp * np.sin(2 * math.pi * k * xs / w + rng.uniform(0, 2 * math.pi))
    top = h - height * (0.55 + 0.45 * top)
    ys = np.arange(h)[:, None]
    mask = np.clip(ys - top[None, :], 0, 1)
    picture = Image.fromarray((mask * 255).astype(np.uint8), "L")
    draw = ImageDraw.Draw(picture)
    for _ in range(trees):
        x = rng.uniform(0, w)
        base = top[int(x) % w] + 4
        tall = rng.uniform(60, 150) * height / 200
        lean = rng.uniform(-0.15, 0.15)
        draw.line([(x, base), (x + lean * tall, base - tall)], fill=255, width=3)
        for _ in range(rng.integers(2, 5)):
            t = rng.uniform(0.35, 0.9)
            bx, by = x + lean * tall * t, base - tall * t
            length = rng.uniform(12, 34) * height / 200
            side = rng.choice([-1, 1])
            draw.line([(bx, by), (bx + side * length, by - length * rng.uniform(0.3, 0.9))],
                      fill=255, width=2)
    alpha = np.asarray(picture.filter(ImageFilter.GaussianBlur(0.7)), dtype=np.float64) / 255.0
    depth = np.clip((ys - top.min()) / (h - top.min()), 0, 1)
    value = tone + fog * depth ** 1.5 * np.ones((1, w))
    return to_image(np.dstack([value, value, value * 1.05]), alpha)


MOUNTAINS_W, MOUNTAINS_H = 2048, 240  # world units of the far mountains (one pixel each)


def mountains():
    """Far mountains, sharp and pale, half lost in the haze: the first plane of depth."""
    rng = np.random.default_rng(41)
    w, h = MOUNTAINS_W, MOUNTAINS_H
    xs = np.arange(w)
    top = np.zeros(w)
    for k in (1, 2, 3, 5, 7, 11, 17, 29):
        top += (1.0 / k ** 0.9) * np.abs(np.sin(math.pi * k * xs / w + rng.uniform(0, math.pi)))
    top = top / top.max()
    top = h - 40 - (h - 80) * top ** 1.6
    ys = np.arange(h)[:, None]
    alpha = np.clip(ys - top[None, :], 0, 1)
    depth = np.clip((ys - top.min()) / (h - top.min()), 0, 1)
    # The sides lit from the left: a lighter tone on slopes that rise to the right.
    slope = np.convolve(np.gradient(top), np.ones(25) / 25, mode="same")[None, :]
    lit = np.clip(-slope * 0.6, 0, 1) * np.clip(1 - (ys - top[None, :]) / 120, 0, 1)
    value = 20 + 16 * lit + 12 * depth ** 1.4
    picture = to_image(np.dstack([value, value, value * 1.07]), alpha)
    return picture.filter(ImageFilter.GaussianBlur(1.0))


MIST_W, MIST_H = 2048, 240


def mist(seed):
    """A band of mist lying between two planes, thicker in the middle, in slow wisps."""
    w, h = MIST_W // 2, MIST_H // 2
    field = noise(w, h, seed, ((128, 1.0), (64, 0.6), (32, 0.3), (16, 0.15)))
    fade = np.linspace(0, 1, w)[None, :]
    field = field * (1 - fade) + field[:, ::-1] * fade  # its ends meet, side by side
    y = np.linspace(-1, 1, h)[:, None]
    band = np.exp(-(y / 0.45) ** 2)
    alpha = np.clip(band * (0.25 + 0.75 * field) - 0.1, 0, 1) * 0.7
    picture = to_image(np.full((h, w, 3), 120.0), alpha)
    return picture.resize((MIST_W, MIST_H), Image.BICUBIC)


GROUND_FAR_W, GROUND_FAR_H = 2048, 160


def ground_far():
    """The top of the ground going away from us, as in Ori: a band a little behind the ground
    we walk on, its lit edge broken by pebbles and tufts, darker as it comes closer."""
    rng = np.random.default_rng(23)
    w, h = GROUND_FAR_W, GROUND_FAR_H
    xs = np.arange(w)
    edge = 30 + 6 * np.sin(2 * math.pi * 3 * xs / w) + 3 * np.sin(2 * math.pi * 11 * xs / w + 1)
    mask_picture = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask_picture)
    draw.polygon([(0, h)] + [(x, edge[x]) for x in range(0, w, 4)] + [(w, edge[-1]), (w, h)],
                 fill=255)
    for _ in range(160):
        x = rng.uniform(0, w)
        base = edge[int(x) % w] + 2
        if rng.uniform() < 0.6:
            for _ in range(rng.integers(3, 7)):
                lean = rng.uniform(-6, 6)
                tall = rng.uniform(6, 18)
                draw.line([(x, base), (x + lean, base - tall)], fill=255, width=2)
                x += rng.uniform(1, 4)
        else:
            r = rng.uniform(3, 8)
            draw.ellipse([x - r, base - r * 1.2, x + r, base + r * 0.4], fill=255)
    alpha = np.asarray(mask_picture.filter(ImageFilter.GaussianBlur(0.6)), dtype=np.float64) / 255
    ys = np.arange(h)[:, None]
    below = ys - edge[None, :]
    rim = np.exp(-np.clip(below, 0, None) / 4.0) * (below > -20)
    value = 12 + 46 * rim + 10 * np.clip(1 - below / 80, 0, 1)
    return to_image(np.dstack([value, value, value * 1.05]), alpha)


FRONT_W, FRONT_H = 2048, 300


def foreground():
    """Black shapes close to the camera, blurred as out of focus: tall grass, a rock, a broken
    branch. They pass in front of the hero, faster than the ground (Limbo's depth)."""
    rng = np.random.default_rng(29)
    w, h = FRONT_W, FRONT_H
    picture = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(picture)
    for cluster in range(5):
        cx = (cluster + rng.uniform(0.1, 0.6)) * w / 5
        if cluster % 2 == 0:
            for _ in range(rng.integers(14, 26)):
                x = cx + rng.uniform(-60, 60)
                tall = rng.uniform(90, 230)
                bend = rng.uniform(-40, 40)
                points = [(x + bend * (t / 8) ** 2, h - tall * t / 8) for t in range(9)]
                for (x1, y1), (x2, y2) in zip(points, points[1:]):
                    draw.line([(x1, y1), (x2, y2)], fill=255, width=7)
        else:
            r = rng.uniform(50, 80)
            draw.ellipse([cx - r * 1.4, h - r * 1.3, cx + r * 1.4, h + r], fill=255)
            draw.line([(cx - 20, h - r), (cx + 90, h - r - 140)], fill=255, width=9)
            draw.line([(cx + 50, h - r - 80), (cx + 110, h - r - 100)], fill=255, width=6)
    alpha = np.asarray(picture.filter(ImageFilter.GaussianBlur(3.5)), dtype=np.float64) / 255
    return to_image(np.full((h, w, 3), 5.0), alpha)


CAVE_W, CAVE_H = 1024, 1536  # world units of the inside of the ground, behind what is dug


def cave():
    """What is seen in a hole: the dark inside of the planet, a little lighter where it is dug
    fresh, with grains of carbon catching the light (docs: "il y a du carbone dans la planète").
    Endless side by side."""
    w, h = CAVE_W // 2, CAVE_H // 2  # one pixel for two units: it is far and dark
    rng = np.random.default_rng(17)
    rock_field = noise(w, h, 17, ((64, 1.0), (32, 0.6), (16, 0.4), (8, 0.25), (4, 0.15)))
    # The left and right edges must meet: blend the field with itself turned around.
    fade = np.linspace(0, 1, w)[None, :]
    rock_field = rock_field * (1 - fade) + rock_field[:, ::-1] * fade
    rock_field = np.minimum(rock_field, rock_field[:, ::-1])
    depth = np.linspace(0, 1, h)[:, None]
    value = 30 + 26 * rock_field - 12 * depth
    sparks = np.zeros((h, w))
    xs = rng.integers(0, w, 900)
    ys = rng.integers(0, h, 900)
    sparks[ys, xs] = rng.uniform(0.3, 1.0, 900) ** 2
    sparks = np.maximum(sparks, blur(sparks, 1.2) * 3)
    value = value + 150 * np.clip(sparks, 0, 1)
    rgb = np.dstack([value, value, value * 1.06])
    return to_image(rgb, np.ones((h, w)))


# ---------------------------------------------------------------------------------------------
# Scenes

def entity(name, x, y, components):
    return {"name": name, "components": {"transform2d.Position": {"x": x, "y": y}, **components}}


def bind(name, source_tag, source, target, low, high, out_low, out_high, power,
         onto="camera"):
    """A number of an entity (the camera by default) that follows a number of another one."""
    return {"name": name, "components": {
        "bind.Bind": {"source": source, "from": source_tag, "target": target, "onto": onto,
                      "low": low, "high": high, "out_low": out_low, "out_high": out_high,
                      "power": power}}}


def track(name, onto, field, keys, start=None, end_action=None, smooth=True, loop=False):
    """A number of the tagged entity played along (tick, value) keys (module tween2d)."""
    made = {"field": field, "onto": onto, "smooth": smooth, "keys": len(keys)}
    for index, (tick, value) in enumerate(keys):
        made[f"t{index}"] = tick
        made[f"v{index}"] = value
    if start:
        made["start"] = start
    if end_action:
        made["end_action"] = end_action
    if loop:
        made["loop"] = True
    return {"name": name, "components": {"tween2d.Track": made}}


def spin(name, onto, ticks):
    """Turns the tagged entity round and round, once every `ticks`."""
    return track(name, onto, "transform2d.Rotation.angle", [(0, 0), (ticks, 360)], smooth=False,
                 loop=True)


def hero_entity(x, y, locked=False):
    hero = entity("hero", x, y, {
        "sprite2d.Sprite": {"atlas": "hero", "width": CELL, "height": CELL,
                            "layer": LAYERS["hero"], "unlit": True},
        "sprite2d.Animator": {"set": "hero"},
        # His white half lights what is around him (Ori's glow in Limbo's dark).
        "light2d.Light": {"radius": 340, "red": 232, "green": 236, "blue": 246,
                          "intensity": 0.95, "offset_y": -24, "flicker": 0.05,
                          "flicker_speed": 2},
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
        # Walking (GDD §5): small black holes pull his legs where his steps fall, at a small cost.
        "reserve.Drain": {"kind": "matter", "field": "transform2d.Velocity.x",
                          "per_unit": WALK_COST},
    })
    if locked:
        # He does not know how to walk yet (D4): the tutorial gives him move_x and jump.
        hero["components"]["action_control.Controlled"]["action_x"] = "locked"
        hero["components"]["jump.Jumper"]["action"] = "locked"
    return hero


def hero_trail():
    """Dust lifted at his feet as he walks (as much as he goes fast, either way), and small
    motes of light rising from his white half."""
    dust = bind("hero_dust_flies", "hero", "transform2d.Velocity.x", "particles2d.Emitter.rate",
                20, 240, 0, 16, 1, onto="hero_dust")
    dust["components"]["bind.Bind"]["absolute"] = True
    return [
        entity("hero_dust", 0, 0, {
            "tag.Tag": {"tag": "hero_dust"},
            "follow2d.Follow": {"target": "hero", "offset_y": HERO_H / 2 - 2, "stiffness": 1},
            "particles2d.Emitter": {
                "rate": 0, "lifetime": 0.9, "lifetime_spread": 0.3, "speed": 22,
                "speed_spread": 14, "angle": -90, "spread": 120, "gravity_y": -10, "drag": 1.5,
                "size": 3, "end_size": 9, "red": 150, "green": 150, "blue": 156, "alpha": 70,
                "fade": True, "additive": False, "layer": LAYERS["hero"] - 1, "area_width": 24,
                "max": 60}}),
        dust,
        entity("hero_motes", 0, 0, {
            "follow2d.Follow": {"target": "hero", "offset_x": -8, "stiffness": 1},
            "particles2d.Emitter": {
                "rate": 5, "lifetime": 1.8, "lifetime_spread": 0.6, "speed": 18,
                "speed_spread": 8, "angle": -90, "spread": 40, "wander": 12, "size": 4,
                "end_size": 1, "red": 245, "green": 248, "blue": 255, "alpha": 200,
                "fade": True, "additive": True, "unlit": True, "layer": LAYERS["hero"] + 1,
                "area_width": 22, "area_height": 50, "max": 30}}),
    ]


def hero_clips():
    """The animations of the hero (sprite2d.AnimationClip), chosen by what he does."""
    conditions = {
        "idle": (0, None, None, None),
        "walk_right": (20, "transform2d.Velocity.x", 20, None),
        "walk_left": (20, "transform2d.Velocity.x", None, -20),
        "charge": (50, "jump.Jumper.charge", 0.02, None),
        "rise": (40, "transform2d.Velocity.y", None, -140),
        "fall": (39, "transform2d.Velocity.y", 180, None),
    }
    made = []
    first = 0
    for name, poses, ticks in HERO_CLIPS:
        priority, field, low, high = conditions[name]
        clip = {"set": "hero", "priority": priority, "first_frame": first,
                "frame_count": len(poses), "ticks_per_frame": ticks,
                "looping": name != "charge"}
        if field:
            clip["field"] = field
        if low is not None:
            clip["min"] = low
        if high is not None:
            clip["max"] = high
        made.append({"name": f"clip_{name}", "components": {"sprite2d.AnimationClip": clip}})
        first += len(poses)
    return made


def jump_hole():
    """The small black hole above the hero's head while he charges a jump: its size follows the
    charge, and it costs matter while it is there."""
    return [
        entity("jump_hole", 0, 0, {
            "tag.Tag": {"tag": "jump_hole"},
            "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                                "layer": LAYERS["hero"] + 5, "unlit": True},
            "transform2d.Scale": {"factor": 0},
            "transform2d.Rotation": {},
            "follow2d.Follow": {"target": "hero", "offset_y": -78, "stiffness": 1},
            "reserve.Drain": {"kind": "matter", "field": "transform2d.Scale.factor",
                              "per_unit": 0.05},
            "light2d.Light": {"radius": 0, "red": 190, "green": 210, "blue": 255,
                              "intensity": 0.7},
            "particles2d.Emitter": dict(VORTEX_EMITTER, pull=1200, swirl=400, size=3.5, max=120),
        }),
        bind("jump_hole_lights", "jump_hole", "transform2d.Scale.factor", "light2d.Light.radius",
             0, 24, 0, 150, 1, onto="jump_hole"),
        bind("jump_hole_draws", "jump_hole", "transform2d.Scale.factor",
             "particles2d.Emitter.rate", 0, 24, 0, 60, 1, onto="jump_hole"),
        bind("jump_hole_draws_wide", "jump_hole", "transform2d.Scale.factor",
             "particles2d.Emitter.area_width", 0, 24, 0, 110, 1, onto="jump_hole"),
        bind("jump_hole_draws_high", "jump_hole", "transform2d.Scale.factor",
             "particles2d.Emitter.area_height", 0, 24, 0, 110, 1, onto="jump_hole"),
        bind("jump_hole_grows", "hero", "jump.Jumper.charge", "transform2d.Scale.factor",
             0, 1, 0, 24, 1, onto="jump_hole"),
        spin("jump_hole_turns", "jump_hole", 150),
    ]


# What the lens adds at rest: a dark vignette, as in Limbo, a little glow and grain.
BASE_VIGNETTE = 0.42
BASE_VIGNETTE_SIZE = 0.48

# Costs and gains (docs: the resources must last and come back well).
WALK_COST = 0.0008  # matter for each unit of speed, each second: 0.19 a second walking
LIFE_DRAIN = 100 / 720  # full, the body lasts twelve minutes
DIG_MATTER = 0.25  # for each cell of ground dug
DIG_CARBON = 0.15  # ... and the carbon of the ground, for the harvest


def hands(fixed=True):
    """The two hands (module loadout): the left one answers to the left button, the right one to
    the right button. In the tutorial they are fixed: white on the left, black on the right, as
    on the hero; later the colours come to either. A hand that uses its power glows by the hero's
    hand, in its colour. The harvest is the harvest key only: the two buttons together use each
    hand on its own."""
    kit = {"tag.Tag": {"tag": "kit"}, "loadout.Kit": {"unlocked": ["black", "white"],
                                                       "together": 0.12}}
    if fixed:
        kit["loadout.Kit"].update({"unlocked": [], "change": "locked"})
    made = [entity("kit", 0, 0, kit),
            {"name": "harvest_combo", "components": {"loadout.Combo": {
                "a": "black", "b": "white", "result": "harvest", "by_hands": False}}}]
    for name, slot, side, item in (("hand_left", 0, -1, "white"), ("hand_right", 1, 1, "black")):
        hand = {"slot": slot, "trigger": name}
        if fixed:
            hand["item"] = item
        made.append(entity(name, 0, 0, {
            "tag.Tag": {"tag": name},
            "loadout.Hand": hand,
            "sprite2d.Sprite": {"atlas": "orb", "width": 30, "height": 30,
                                "layer": LAYERS["hero"] + 2, "unlit": True, "alpha": 0},
            "sprite2d.FrameBind": {"field": "loadout.Hand.item",
                                   "frame_count": len(POWER_COLOURS) + 1},
            "follow2d.Follow": {"target": "hero", "offset_x": side * 15, "offset_y": 14,
                                "stiffness": 0.6},
        }))
        made.append(bind(f"{name}_glows", name, "loadout.Hand.acting", "sprite2d.Sprite.alpha",
                         0, 1, 0, 240, 1, onto=name))
    return made


def powers(harvest_locked=False):
    """The effects of the hands (loadout.Ability), each switched on while a hand uses it.

    The black (GDD, Noir): a black hole where the pointer is, within reach, that grows while the
    button is held and costs more matter the bigger it is. It pulls loose things, swallows what
    reaches its heart and digs the ground there: alone, it turns everything into matter."""
    hole = entity("black_hole", 0, 0, {
        "tag.Tag": {"tag": "black_hole"},
        "transform2d.Disabled": {},
        "loadout.Ability": {"power": "black", "kind": "matter", "per_use": 0.5},
        "aim2d.Aim": {"from": "hero", "range": 240},
        "attractor2d.Attractor": {"start_radius": 12, "grow": 45, "max_radius": 90,
                                  "shrink": 260, "pull": 1500, "pulls": ["rock"],
                                  "inner": HEART, "convert": "matter"},
        # Matter a second for each unit of radius: about a unit a second at full size.
        "reserve.Drain": {"kind": "matter", "field": "attractor2d.Attractor.radius",
                          "per_unit": 0.012},
        "terrain2d.Digger": {"radius": 0, "layers": ["ground"]},
        "reserve.Gain": {"kind": "matter", "field": "terrain2d.Digger.last",
                         "per_unit": DIG_MATTER},
        "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                            "layer": LAYERS["hero"] + 5, "unlit": True},
        "transform2d.Scale": {"factor": 0},
        "transform2d.Rotation": {},
        # Its ring lights the ground; dust spirals into it.
        "light2d.Light": {"radius": 0, "red": 190, "green": 210, "blue": 255, "intensity": 0},
        "particles2d.Emitter": dict(VORTEX_EMITTER, pull=1800, swirl=520),
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
                             "wall_drag": 50, "thickness": 14, "kind": "carbon",
                             "per_unit": 0.025, "atlas": "barrier", "preview": 2,
                             "layer": LAYERS["hero"] + 4},
    })
    # The harvest (GDD §7), the harvest key: a bubble grows where the pointer is; everything inside
    # it is swallowed and sorted, and comes into the reserves when the harvest ends; the ground it
    # digs gives matter and carbon. It is free to start (it is how one comes back from too little),
    # and wears a little matter while it lasts.
    gather = entity("harvest_bubble", 0, 0, {
        "tag.Tag": {"tag": "harvest_bubble"},
        "transform2d.Disabled": {},
        "loadout.Ability": {"power": "harvest"},
        "aim2d.Aim": {"from": "hero", "range": 240},
        "attractor2d.Attractor": {"start_radius": 14, "grow": 40, "max_radius": 80,
                                  "shrink": 300, "pull": 300, "pulls": ["rock"], "inner": 1,
                                  "deferred": True},
        "reserve.Drain": {"kind": "matter", "per_second": 0.1,
                          "field": "attractor2d.Attractor.radius", "per_unit": 0.004},
        "terrain2d.Digger": {"radius": 0, "layers": ["ground"]},
        "reserve.Gain": {"kind": "matter", "field": "terrain2d.Digger.last",
                         "per_unit": DIG_MATTER},
        "sprite2d.Sprite": {"atlas": "harvest", "width": 2, "height": 2,
                            "layer": LAYERS["hero"] + 5, "unlit": True},
        "transform2d.Scale": {"factor": 0},
        "light2d.Light": {"radius": 0, "red": 200, "green": 245, "blue": 240, "intensity": 0},
        "particles2d.Emitter": dict(VORTEX_EMITTER, pull=700, swirl=260, red=215, green=250,
                                    blue=245),
    })
    # The carbon of the dug ground: a second gain that follows the harvest's digging.
    carbon_of_ground = entity("ground_carbon", 0, 0, {
        "tag.Tag": {"tag": "ground_carbon"},
        "transform2d.Scale": {"factor": 0},
        "reserve.Gain": {"kind": "carbon", "field": "transform2d.Scale.factor",
                         "per_unit": DIG_CARBON},
    })
    shortcut = {"name": "harvest_key", "components": {
        "tag.Tag": {"tag": "harvest_key"},
        # Locked, the key does nothing until the tutorial gives it.
        "loadout.Shortcut": {"action": "locked" if harvest_locked else "harvest",
                             "first": "black", "second": "white"}}}
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
        gather, shortcut, carbon_of_ground,
        # The harvest leaves a hole, most of its bubble wide.
        bind("harvest_digs", "harvest_bubble", "attractor2d.Attractor.heart",
             "terrain2d.Digger.radius", 0, 1000, 0, 700, 1, onto="harvest_bubble"),
        bind("harvest_shows", "harvest_bubble", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 1000, 0, 1000, 1, onto="harvest_bubble"),
        bind("ground_gives_carbon", "harvest_bubble", "terrain2d.Digger.last",
             "transform2d.Scale.factor", 0, 100000, 0, 100000, 1, onto="ground_carbon"),
        hole,
        bind("black_hole_digs", "black_hole", "attractor2d.Attractor.heart",
             "terrain2d.Digger.radius", 0, 1000, 0, 1000, 1, onto="black_hole"),
        bind("black_hole_shows", "black_hole", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 1000, 0, 1000, 1, onto="black_hole"),
        spin("black_hole_turns", "black_hole", 180),
    ] + power_glow("black_hole", "black_hole", "attractor2d.Attractor.radius", 90, 260, 110,
                   320) + power_glow("harvest", "harvest_bubble", "attractor2d.Attractor.radius",
                                     80, 220, 70, 220) + digging_dust("black_hole") + digging_dust("harvest_bubble") + [
        # The white: sparks of glass where a barrier is being raised.
        entity("white_sparks", 0, 0, {
            "tag.Tag": {"tag": "white_sparks"},
            "follow2d.Follow": {"target": "white_caster", "stiffness": 1},
            "particles2d.Emitter": {
                "rate": 0, "lifetime": 0.9, "lifetime_spread": 0.3, "speed": 40,
                "speed_spread": 30, "wander": 30, "size": 4, "end_size": 1, "red": 210,
                "green": 250, "blue": 245, "alpha": 230, "fade": True, "additive": True,
                "unlit": True, "light": False, "layer": LAYERS["hero"] + 4, "area_width": 60,
                "area_height": 60, "max": 120}}),
        bind("white_sparkles", "white_caster", "barrier2d.Caster.radius",
             "particles2d.Emitter.rate", 0, 140, 0, 90, 1, onto="white_sparks"),
        bind("white_sparkles_wide", "white_caster", "barrier2d.Caster.radius",
             "particles2d.Emitter.area_width", 0, 140, 0, 260, 1, onto="white_sparks"),
        bind("white_sparkles_high", "white_caster", "barrier2d.Caster.radius",
             "particles2d.Emitter.area_height", 0, 140, 0, 260, 1, onto="white_sparks"),
    ]


def power_glow(name, holder, size_field, size_high, radius, rate, area):
    """A power that lights around it and draws dust in, as much as it is big."""
    return [
        bind(f"{name}_lights", holder, size_field, "light2d.Light.intensity", 0, size_high,
             0, 0.8, 1, onto=holder),
        bind(f"{name}_lights_far", holder, size_field, "light2d.Light.radius", 0, size_high,
             0, radius, 1, onto=holder),
        bind(f"{name}_draws", holder, size_field, "particles2d.Emitter.rate", 0, size_high,
             0, rate, 1, onto=holder),
        bind(f"{name}_draws_wide", holder, size_field, "particles2d.Emitter.area_width", 0,
             size_high, 0, area, 1, onto=holder),
        bind(f"{name}_draws_high", holder, size_field, "particles2d.Emitter.area_height", 0,
             size_high, 0, area, 1, onto=holder),
    ]


VORTEX_EMITTER = {"rate": 0, "lifetime": 1.6, "lifetime_spread": 0.4, "speed": 0,
                  "speed_spread": 20, "drag": 1.6, "size": 5, "end_size": 1.5, "red": 205, "green": 220,
                  "blue": 255, "alpha": 230, "fade": True, "additive": True, "unlit": True,
                  "layer": LAYERS["hero"] + 4, "max": 260}


def digging_dust(holder):
    """Chips of ground thrown up where a power digs: dark ones and a few bright grains of
    carbon, as many as the cells dug."""
    name = f"{holder}_chips"
    return [
        entity(name, 0, 0, {
            "tag.Tag": {"tag": name},
            "follow2d.Follow": {"target": holder, "stiffness": 1},
            "particles2d.Emitter": {
                "rate": 0, "lifetime": 1.1, "lifetime_spread": 0.4, "speed": 160,
                "speed_spread": 110, "angle": -90, "spread": 150, "gravity_y": 700,
                "drag": 0.6, "size": 5, "end_size": 2.5, "red": 150, "green": 152,
                "blue": 160, "alpha": 255, "fade": True, "additive": False, "unlit": False,
                "layer": LAYERS["hero"] + 3, "area_width": 50, "area_height": 30,
                "max": 220}}),
        bind(f"{name}_fly", holder, "terrain2d.Digger.last", "particles2d.Emitter.rate", 0,
             40, 0, 600, 1, onto=name),
    ]


def rock_entity(name, x, y, kind="matter", amount=25, atlas="rock"):
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


def fossil_entity(name, x, y, amount=30):
    """A fossil (carbon): a pale stone that glows a little in the dark, to be found."""
    made = rock_entity(name, x, y, kind="carbon", amount=amount, atlas="fossil")
    made["components"]["light2d.Light"] = {"radius": 70, "red": 220, "green": 232,
                                           "blue": 236, "intensity": 0.45, "flicker": 0.25,
                                           "flicker_speed": 1.5}
    return made


def inventory():
    """The inventory (GDD §7): the inventory key shows, above the body, a bubble for the matter
    and one for the carbon, as big as there is of it."""
    made = [entity("inventory", 0, 0, {"tag.Tag": {"tag": "inventory"},
                                       "toggle.Toggle": {"action": "inventory"}})]
    for index, (kind, side) in enumerate((("matter", 1), ("carbon", -1))):
        name = f"{kind}_bubble"
        made.append(entity(name, 0, 0, {
            "tag.Tag": {"tag": name},
            "transform2d.Hidden": {},
            "toggle.ShownBy": {"toggle": "inventory"},
            "sprite2d.Sprite": {"atlas": "inventory", "frame": index, "width": 64,
                                "height": 64, "layer": LAYERS["hero"] + 6, "unlit": True},
            "transform2d.Scale": {"factor": 1},
            "follow2d.Follow": {"target": "hero", "offset_x": side * 36, "offset_y": -84,
                                "stiffness": 0.25, "wave_y": 3, "wave_period": 2.2},
        }))
        made.append(bind(f"{kind}_fills", kind, "reserve.Reserve.fill",
                         "transform2d.Scale.factor", 0, 1, 0.15, 1, 0.5, onto=name))
    return made


def camera(bound_right, bound_bottom):
    return entity("camera", VIEW_W / 2, VIEW_H / 2, {
        "tag.Tag": {"tag": "camera"},
        "camera2d.Camera": {"zoom": ZOOM},
        "camera2d.CameraShake": {"strength": 0, "decay": 3, "speed": 30},
        # Brought back to life, the picture comes back from black.
        "spawner2d.Shooter": {"prefab": "fade_back", "action": "revive"},
        "postfx.Effects": {"vignette": BASE_VIGNETTE, "vignette_size": BASE_VIGNETTE_SIZE,
                           "edge_glow": 0, "edge_glow_size": 0.5, "bloom": 0.45,
                           "grain": 0.05, "rays": 0.22, "rays_x": 0.5, "rays_y": 0.08,
                           "rays_length": 0.22, "paused_blur": 0.8},
        # The dark between the lights: what the hero's glow does not reach stays dim.
        "light2d.Ambient": {"red": 92, "green": 94, "blue": 106},
        # Ori's camera: soft, a little ahead, the hero a little below the middle.
        "camera2d.CameraFollow": {"stiffness": 0.06, "dead_x": 30, "dead_y": 60,
                                  "offset_y": -40, "look_ahead": 0.25,
                                  "bounded": True, "bound_left": 0, "bound_right": bound_right,
                                  "bound_top": -2000, "bound_bottom": bound_bottom},
    })


def sky_entity(coloured=False):
    return [
        entity("sky", 0, 0, {
            "sprite2d.Sprite": {"atlas": "sky_colour" if coloured else "sky", "width": VIEW_W,
                                "height": VIEW_H, "layer": LAYERS["sky"], "unlit": True},
            "sprite2d.Parallax": {"x": 0, "y": 0}}),
        entity("veil", 0, 0, {
            "sprite2d.Sprite": {"atlas": "veil_colour" if coloured else "veil",
                                "width": 1600 * 1.6, "height": 900 * 1.6,
                                "layer": LAYERS["sky"] + 5, "unlit": True, "alpha": 200},
            "sprite2d.Parallax": {"x": 0.06, "y": 0.06}}),
    ]


def layer_entity(name, atlas, size, foot_y, parallax, layer, camera_y, alpha=255, lit=False):
    """A band of scenery repeated along x, its bottom at foot_y as seen from a camera at
    camera_y: farther planes (parallax below 1) move less, nearer ones (above 1) more."""
    width, height = size
    y = foot_y - height / 2 - camera_y * (1 - parallax)
    sprite = {"atlas": atlas, "width": width, "height": height, "layer": layer, "alpha": alpha}
    if not lit:
        sprite["unlit"] = True
    return entity(name, 0, y, {"sprite2d.Sprite": sprite,
                               "sprite2d.Parallax": {"x": parallax, "y": parallax,
                                                     "repeat_x": True}})


def scenery(ground_y):
    """Planes of depth, from the far mountains to black grass in front of the camera, with mist
    between them; dust and ash floating in the light."""
    camera_y = ground_y - 100  # where the camera usually is: the planes' feet at the ground there
    made = [
        layer_entity("mountains", "mountains", (MOUNTAINS_W, MOUNTAINS_H), ground_y + 30, 0.12,
                     LAYERS["far"] - 20, camera_y),
        layer_entity("mist_far", "mist", (MIST_W, MIST_H), ground_y + 110, 0.18,
                     LAYERS["far"] - 10, camera_y, alpha=70),
        layer_entity("mist_near", "mist", (MIST_W, MIST_H), ground_y + 120, 0.45,
                     LAYERS["near"] - 5, camera_y, alpha=60),
        layer_entity("ground_far", "ground_far", (GROUND_FAR_W, GROUND_FAR_H), ground_y + 110,
                     0.88, LAYERS["ground"] - 8, camera_y),
        layer_entity("front", "front", (FRONT_W, FRONT_H), ground_y + 230, 1.35, 60, camera_y),
    ]
    for name, atlas, parallax, layer, alpha in (("hills_far", "hills_far", 0.25, LAYERS["far"],
                                                 255),
                                                ("hills_near", "hills_near", 0.55,
                                                 LAYERS["near"], 255)):
        made.append(layer_entity(name, atlas, (HILLS_W, HILLS_H), ground_y + 40, parallax,
                                 layer, camera_y, alpha=alpha))
    # The inside of the planet, behind the ground: what is seen in what is dug.
    made.append(entity("inside", 0, ground_y + 24 + CAVE_H / 2, {
        "sprite2d.Sprite": {"atlas": "cave", "width": CAVE_W, "height": CAVE_H,
                            "layer": LAYERS["ground"] - 5},
        "sprite2d.Parallax": {"x": 1, "y": 1, "repeat_x": True}}))
    made.append(entity("dust", 0, 0, {
        "follow2d.Follow": {"target": "camera", "stiffness": 1},
        "particles2d.Emitter": {
            "rate": 7, "lifetime": 9, "lifetime_spread": 3, "speed": 8, "speed_spread": 6,
            "spread": 360, "wander": 10, "size": 4, "end_size": 2, "red": 220,
            "green": 225, "blue": 235, "alpha": 170, "fade": True, "additive": True,
            "unlit": True, "layer": LAYERS["dust"], "area_width": 1500, "area_height": 900,
            "max": 90}}))
    # Ash falling slowly through the light, some near the camera (big and soft).
    made.append(entity("ash", 0, -500, {
        "follow2d.Follow": {"target": "camera", "offset_y": -500, "stiffness": 1},
        "particles2d.Emitter": {
            "rate": 16, "lifetime": 14, "lifetime_spread": 4, "speed": 10, "speed_spread": 8,
            "angle": 100, "spread": 40, "gravity_y": 6, "wander": 14, "size": 3,
            "end_size": 5, "red": 200, "green": 200, "blue": 208, "alpha": 110,
            "fade": True, "unlit": True, "layer": 55, "area_width": 1700, "area_height": 200,
            "max": 260}}))
    return made


TERRAIN_CELL = 8  # side of a cell of the ground, in world units


def terrain_entity(name, left, top, width, height, surface_y, extra=None):
    """A ground that can be dug (terrain2d): its grid covers width by height from (left, top),
    solid under surface_y. Black, its top lit as in Ori."""
    components = {
        "terrain2d.Terrain": {"columns": int(width // TERRAIN_CELL),
                              "rows": int(height // TERRAIN_CELL), "cell": TERRAIN_CELL,
                              "layer": ["ground"],
                              "surface": (surface_y - top) / TERRAIN_CELL},
        "terrain2d.Look": {"layer": LAYERS["ground"], "pixels": 4, "red": 12, "green": 12,
                           "blue": 14, "top_red": 176, "top_green": 180, "top_blue": 190,
                           "lip": 10, "grain": 0.08},
    }
    if extra:
        components["terrain2d.Terrain"].update(extra)
    return entity(name, left, top, components)


def shape(name, x, y, width, height, fill, hard=False):
    made = {"width": width, "height": height, "fill": fill}
    if hard:
        made["hard"] = True
    return entity(name, x, y, {"terrain2d.Shape": made})


def reserves(matter=100, carbon=100, floor=0):
    """Life (GDD §8): black (matter) and white (carbon) wear away to keep the body whole. With
    less than half of either, the edges of the screen turn: white when the matter runs out, black
    when the carbon does, until the whole screen is gone."""
    made = []
    for kind, amount, fade_prefab in (("matter", matter, "fade_white"),
                                      ("carbon", carbon, "fade_black")):
        made.append(entity(kind, 0, 0, {
            "tag.Tag": {"tag": kind}, "checkpoint.Saved": {},
            "reserve.Reserve": {"kind": kind, "amount": amount, "max": 100, "drain": LIFE_DRAIN,
                                "floor": floor, "empty_action": f"out_of_{kind}"},
            "spawner2d.Shooter": {"prefab": fade_prefab, "action": f"out_of_{kind}"}}))
    return made + [
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
        "transform2d.Position": {"x": SCREEN_W / 2, "y": SCREEN_H / 2},
        "sprite2d.Sprite": {"atlas": "plain", "width": SCREEN_W + 64, "height": SCREEN_H + 64,
                            "screen": True, "red": colour[0], "green": colour[1],
                            "blue": colour[2], "alpha": start, "unlit": True, "layer": 1000},
        "tween2d.Tween": tween,
    }}


PREFAB_LIST = [fade("fade_white", (240, 240, 236), 60, 0, 255, "revive"),
               fade("fade_black", (0, 0, 0), 60, 0, 255, "revive"),
               fade("fade_back", (0, 0, 0), 50, 255, 0)]


def cover(name, alpha=0, colour=(255, 255, 255)):
    """A screen-wide plain colour over everything (a flash, a fade)."""
    return entity(name, SCREEN_W / 2, SCREEN_H / 2, {"tag.Tag": {"tag": name},
                                                     "sprite2d.Sprite": {
        "atlas": "plain", "width": SCREEN_W + 64, "height": SCREEN_H + 64, "screen": True,
        "red": colour[0], "green": colour[1], "blue": colour[2], "alpha": alpha,
        "unlit": True, "layer": 1000}})


MENU_LAYER = 900  # the menus, over the interface of the game


def menu_buttons(menu, labels, top, shown_by=None):
    """The buttons of a menu (ui), one under the other from top, centred on the screen. Each
    label is (string, action). The keyboard, the pad and the mouse all choose."""
    made = [entity(f"{menu}_menu", 0, 0, {"ui.Menu": {
        "group": menu, "up": "menu_up", "down": "menu_down", "accept": "accept",
        "use_pointer": True, "pointer_x": "pointer_x", "pointer_y": "pointer_y",
        "click": "menu_click"}})]
    for order, (string, action) in enumerate(labels):
        y = top + order * 118
        hidden = {"toggle.ShownBy": {"toggle": shown_by}, "transform2d.Hidden": {}} if (
            shown_by) else {}
        made.append(entity(f"{menu}_{action}", SCREEN_W / 2, y, {
            "ui.Button": {"menu": menu, "order": order, "action": action, "width": 440,
                          "height": 104, "normal_frame": 0, "focus_frame": 1},
            "sprite2d.Sprite": {"atlas": "button", "width": 440, "height": 110, "screen": True,
                                "layer": MENU_LAYER + 20, "unlit": True},
            **hidden}))
        made.append(entity(f"{menu}_{action}_label", SCREEN_W / 2, y + 2, {
            "text2d.Text": {"string": string, "font": "font", "size": 38, "pivot_x": 0.5,
                            "pivot_y": 0.5, "screen": True, "alpha": 240},
            **hidden}))
    return made


def pause_menu():
    """The pause (D6): Escape shuts him in a bubble, everything stops (toggle.Toggle.pauses), the
    world goes soft behind it (postfx paused_blur), and the menu is in the bubble."""
    shown = {"toggle.ShownBy": {"toggle": "pause"}, "transform2d.Hidden": {}}
    middle = (SCREEN_W / 2, SCREEN_H / 2)
    return [
        entity("pause", 0, 0, {"tag.Tag": {"tag": "pause"}, "toggle.Toggle": {
            "action": "pause", "off": "resume", "pauses": True}}),
        entity("pause_dim", *middle, {"sprite2d.Sprite": {
            "atlas": "plain", "width": SCREEN_W + 64, "height": SCREEN_H + 64, "screen": True,
            "red": 6, "green": 7, "blue": 10, "alpha": 120, "unlit": True,
            "layer": MENU_LAYER}, **shown}),
        entity("pause_bubble", *middle, {"sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 760, "height": 760, "screen": True,
            "layer": MENU_LAYER + 5, "unlit": True}, **shown}),
        entity("pause_sleeper", middle[0], middle[1] - 205, {"sprite2d.Sprite": {
            "atlas": "hero_curled", "width": 128, "height": 128, "screen": True,
            "layer": MENU_LAYER + 10, "unlit": True}, **shown}),
        entity("pause_title", middle[0], middle[1] - 82, {"text2d.Text": {
            "string": "menu.pause", "font": "font", "size": 56, "pivot_x": 0.5,
            "pivot_y": 0.5, "screen": True, "alpha": 235}, **shown}),
        entity("to_title", 0, 0, {"scene_switch.GoToScene": {
            "scene": "title", "use_action": True, "action": "to_title"}}),
    ] + menu_buttons("pause", [("menu.resume", "resume"), ("menu.leave", "to_title")],
                     middle[1] + 40, shown_by="pause")


def title():
    """The title: the sky of the beginning, still in colours, the hero asleep in his bubble,
    and the menu."""
    return sky_entity(coloured=True) + [
        entity("camera", VIEW_W / 2, VIEW_H / 2, {
            "tag.Tag": {"tag": "camera"}, "camera2d.Camera": {"zoom": ZOOM},
            "postfx.Effects": {"bloom": 0.6, "vignette": 0.55, "vignette_size": 0.35,
                               "grain": 0.05, "rays": 0.18, "rays_x": 0.5, "rays_y": 0.45,
                               "rays_length": 0.25}}),
        entity("sleeper", VIEW_W / 2, 318, {"tag.Tag": {"tag": "sleeper"},
                                            "sprite2d.Sprite": {
            "atlas": "hero_curled", "width": 64, "height": 64, "layer": 30, "unlit": True},
            "transform2d.Rotation": {}}),
        entity("sleeper_bubble", VIEW_W / 2, 318, {"sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 96, "height": 96, "layer": 31,
            "unlit": True}, "follow2d.Follow": {"target": "sleeper", "stiffness": 1}}),
        entity("sleeper_glow", VIEW_W / 2, 318, {"sprite2d.Sprite": {
            "atlas": "glow", "width": 260, "height": 260, "layer": 29, "alpha": 90,
            "additive": True, "unlit": True},
            "follow2d.Follow": {"target": "sleeper", "stiffness": 1}}),
        track("sleeper_floats", "sleeper", "transform2d.Position.y",
              [(0, 318), (150, 308), (300, 318)], loop=True),
        track("sleeper_turns", "sleeper", "transform2d.Rotation.angle",
              [(0, -6), (240, 6), (480, -6)], loop=True),
        # Dust of light drifting across the colours.
        entity("title_dust", VIEW_W / 2, VIEW_H / 2, {"particles2d.Emitter": {
            "rate": 14, "lifetime": 9, "speed": 14, "speed_spread": 10, "angle": -90,
            "spread": 360, "wander": 10, "size": 3, "end_size": 1, "red": 236, "green": 232,
            "blue": 255, "alpha": 200, "fade": True, "additive": True, "unlit": True,
            "layer": 20, "area_width": VIEW_W, "area_height": VIEW_H, "max": 140}}),
        entity("title_name", SCREEN_W / 2, 230, {"text2d.Text": {
            "string": "menu.title", "font": "font", "size": 92, "spacing": 10,
            "pivot_x": 0.5, "pivot_y": 0.5, "screen": True, "alpha": 245}}),
        entity("to_opening", 0, 0, {"scene_switch.GoToScene": {
            "scene": "opening", "use_action": True, "action": "play"}}),
        entity("quits", 0, 0, {"host.Quit": {"action": "quit"}}),
    ] + menu_buttons("title", [("menu.play", "play"), ("menu.quit", "quit")], 700)


def sandbox():
    """The test ground of the modules: flat, wide, nothing on it."""
    return (sky_entity() + scenery(GROUND_Y) +
            [terrain_entity("ground", 0, 0, SANDBOX_W, GROUND_Y + 200, GROUND_Y),
             # A pit on the left of the hero, a low step and a wall on his right: what the
             # modules are tried on.
             shape("pit", 232, GROUND_Y + 80, 64, 160, False),
             shape("step", 800, GROUND_Y - 4, 200, 8, True),
             shape("wall", 1250, GROUND_Y - 60, 32, 120, True),
             rock_entity("rock_1", 560, GROUND_Y - 11, amount=12),
             rock_entity("rock_2", 640, GROUND_Y - 11, amount=12),
             # A rock high up, that falls as the scene starts.
             rock_entity("falling_rock", 480, 200, amount=12),
             fossil_entity("fossil_1", 330, GROUND_Y - 11, amount=15),
             hero_entity(400, GROUND_Y - HERO_H / 2),
             camera(SANDBOX_W, GROUND_Y + 120)] + hero_clips() + hero_trail() +
            hands(fixed=False) + powers()
            + inventory() + jump_hole() + death() + pause_menu())


def sandbox_scene(matter=100, carbon=100):
    return sandbox() + reserves(matter, carbon)


# ---------------------------------------------------------------------------------------------
# The tutorial (docs/complete_universe/TUTORIEL.md): one planet in the great barrier, played flat.

TUTORIAL_W = 5600  # the planet's ground runs from x 0 to here
TUTORIAL_TOP = -1000  # the ground's grid starts this high (walls at both ends go up to it)
TUTORIAL_DEPTH = 1400  # how deep the ground goes under its surface: it can be dug into
LANDING_X = 600
LANDING_ROCK = (300, 900)  # the hard rock under the landing: the hero cannot dig a trap there
WALK_TO = 1600  # walking ends here
GAP = (1800, 1900)  # jumping: a pit to jump over...
LEDGE = (2100, 2600, 110)  # ...then a ledge this high
BASIN = (2700, 4000, 80)  # the basin with fossils, then rocks

# The steps of the tutorial, in order. A step "x_practice" follows the step x once it is done: a
# few seconds to try it again and see what it does, before the next one.
STEPS = ["landing", "black", "black_practice", "white", "white_practice", "harvest",
         "harvest_practice", "inventory", "inventory_practice", "walk", "jump", "jump_practice",
         "lack_white", "lack_black", "death", "after_death", "departure", "end"]

# The texts of each step (GDD §12: text is only for the controls): a title and a line.
TEXTS = {
    "black": ("Le noir",
              "Clic droit maintenu : un trou noir qui attire et avale. Avale deux pierres."),
    "white": ("Le blanc",
              "Clic gauche : une bulle. Clic gauche glissé : un mur. Fais-en deux."),
    "harvest": ("La récolte",
                "F maintenu sur un fossile : il rejoint tes réserves, avec le sol autour."),
    "inventory": ("Tes réserves",
                  "Tab : la matière (noire) et le carbone (blanc) que tu portes."),
    "walk": ("Marcher",
             "Q / D ou ← → : à chaque pas, un petit trou noir tire ta jambe."),
    "jump": ("Sauter",
             "Espace maintenu puis relâché : plus le trou noir grandit, plus tu sautes haut."),
    "lack_white": ("Le carbone s'épuise",
                   "F sur les fossiles, ou dans le sol : il contient aussi du carbone."),
    "lack_black": ("La matière s'épuise",
                   "F sur les pierres, ou creuse le sol : il est fait de matière."),
    "death": ("Sans matière, ton corps se défait",
              "Tout devient blanc…"),
    "after_death": ("Sans carbone, ce serait le noir",
                    "Garde toujours les deux : tu reprends au début de l'étape."),
    "departure": ("Le départ",
                  "Clic droit maintenu vers le ciel : le plus grand trou noir possible."),
}
PRACTICE_TEXT = "Bien. Essaie encore un peu…"
PRACTICE_SECONDS = {"black": 6, "white": 6, "harvest": 5, "inventory": 3, "jump": 2}


def step_texts():
    made = []
    for step, (title, line) in TEXTS.items():
        phases = [step] + ([f"{step}_practice"] if step in PRACTICE_SECONDS else [])
        for part, y, size, alpha in (("title", SCREEN_H - 168, 50, 240),
                                     ("line", SCREEN_H - 104, 32, 215)):
            made.append(entity(f"text_{step}_{part}", SCREEN_W / 2, y, {
                "text2d.Text": {"string": f"tutorial.{step}.{part}", "font": "font",
                                "size": size, "pivot_x": 0.5, "pivot_y": 0.5, "screen": True,
                                "alpha": alpha},
                "phase.ShowIn": {"phases": phases},
                "transform2d.Hidden": {},
            }))
    made.append(entity("text_practice", SCREEN_W / 2, SCREEN_H - 52, {
        "text2d.Text": {"string": "tutorial.practice", "font": "font", "size": 26,
                        "pivot_x": 0.5, "pivot_y": 0.5, "screen": True, "alpha": 170},
        "phase.ShowIn": {"phases": [f"{step}_practice" for step in PRACTICE_SECONDS]},
        "transform2d.Hidden": {},
    }))
    return made


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
    """The steps of the tutorial (module phase), what ends each one (rules.Watch) and what each
    one gives (rules.OnAction). The clock counts the seconds of the steps of practice."""
    made = [entity("story", 0, 0, {"tag.Tag": {"tag": "story"},
                                   "phase.Phase": {"current": "landing"}}),
            entity("clock", 0, 0, {"tag.Tag": {"tag": "clock"},
                                   "transform2d.Velocity": {"x": 1}})]
    ends = {}
    for index, step in enumerate(STEPS[:-1]):
        action = {"death": "revive"}.get(step, f"{step}_done" if not step.endswith(
            "_practice") else f"{step[:-len('_practice')]}_next")
        ends[step] = action
        made.append({"name": f"after_{step}", "components": {"phase.Transition": {
            "action": action, "from": [step], "from_any": False, "to": STEPS[index + 1]}}})
    # A step of practice: the clock starts again, and the next step comes after some seconds.
    for step, seconds in PRACTICE_SECONDS.items():
        made.append(on(f"{step}_clock", ends[step], "clock", "transform2d.Position.x", 0))
        made.append(watch(f"{step}_practice_watch", f"{step}_practice", "clock",
                          "transform2d.Position.x", ends[f"{step}_practice"], seconds,
                          inside=True))
    made += [
        # The landing: he wakes, a moment, then the black (D4: he does not move yet).
        entity("landing_timer", 0, 0, {"tween2d.Tween": {
            "field": "transform2d.Position.x", "from": 0, "to": 1, "ticks": 170,
            "use_action": True, "action": "landing_done"}}),
        on("gives_black", "landing_done", "kit", "loadout.Kit.unlocked", 1),
        # The black swallows two rocks.
        watch("black_watch", "black", "black_hole", "attractor2d.Attractor.swallowed",
              "black_done", 2),
        on("gives_white", "black_next", "kit", "loadout.Kit.unlocked", 3),
        # The white raises two barriers.
        watch("white_watch", "white", "white_caster", "barrier2d.Caster.raised", "white_done",
              2),
        on("gives_harvest", "white_next", "harvest_key", "loadout.Shortcut.action",
           ACTIONS.index("harvest")),
        # The harvest brings carbon in.
        watch("harvest_watch", "harvest", "carbon", "reserve.Reserve.gained", "harvest_done",
              15),
        # The inventory opened.
        watch("inventory_watch", "inventory", "inventory", "toggle.Toggle.times",
              "inventory_done", 1),
        # Walking, and jumping at once (a hole dug in front of him must not trap him).
        on("learns_walking", "inventory_next", "hero", "action_control.Controlled.action_x",
           ACTIONS.index("move_x")),
        on("learns_jumping", "inventory_next", "hero", "jump.Jumper.action",
           ACTIONS.index("jump")),
        watch("walk_watch", "walk", "hero", "transform2d.Position.x", "walk_done", WALK_TO),
        watch("jump_watch", "jump", "hero", "transform2d.Position.x", "jump_done",
              LEDGE[0] + 60),
        # From now on the reserves can run out.
        on("matter_unguarded", "jump_next", "matter", "reserve.Reserve.floor", 0),
        on("carbon_unguarded", "jump_next", "carbon", "reserve.Reserve.floor", 0),
        # The white runs low and fast: harvest carbon back above half.
        on("white_falls", "jump_next", "carbon", "reserve.Reserve.amount", 25),
        on("white_drains", "jump_next", "carbon", "reserve.Reserve.drain", 1.2),
        watch("white_watch_back", "lack_white", "carbon", "reserve.Reserve.amount",
              "lack_white_done", 50, inside=True),
        on("white_steadies", "lack_white_done", "carbon", "reserve.Reserve.drain", LIFE_DRAIN),
        # Then the black.
        on("black_falls", "lack_white_done", "matter", "reserve.Reserve.amount", 25),
        on("black_drains", "lack_white_done", "matter", "reserve.Reserve.drain", 1.2),
        watch("black_watch_back", "lack_black", "matter", "reserve.Reserve.amount",
              "lack_black_done", 50, inside=True),
        on("black_steadies", "lack_black_done", "matter", "reserve.Reserve.drain", LIFE_DRAIN),
        # Death, on purpose: the matter runs out whatever he does, everything goes white, and he
        # comes back. Then he is given his reserves back.
        entity("death_drain", 0, 0, {
            "reserve.Drain": {"kind": "matter", "per_second": 14},
            "phase.ActiveIn": {"phases": ["death"]}, "transform2d.Disabled": {}}),
        on("after_death_clock", "revive", "clock", "transform2d.Position.x", 0),
        watch("refill_watch", "after_death", "clock", "transform2d.Position.x", "refill", 0.6,
              inside=True),
        on("matter_back", "refill", "matter", "reserve.Reserve.amount", 75),
        on("carbon_back", "refill", "carbon", "reserve.Reserve.amount", 75),
        watch("after_death_watch", "after_death", "clock", "transform2d.Position.x",
              "after_death_done", 6, inside=True),
        # A black hole as big as it gets, towards the sky: the departure.
        watch("departure_watch", "departure", "black_hole", "attractor2d.Attractor.radius",
              "departure_done", 85),
    ]
    return made + step_texts()


def tutorial():
    """The planet of the tutorial, from left to right: the landing on hard rock, the path, the
    pit and the ledge, the basin, the edge under the top of the dome. Its ground goes deep: what
    is dug opens the way down, and the camera follows."""
    bottom = GROUND_Y + TUTORIAL_DEPTH
    height = bottom - TUTORIAL_TOP
    ground = terrain_entity("ground", 0, TUTORIAL_TOP, TUTORIAL_W, height, GROUND_Y,
                            {"amplitude": 1.2, "wavelength": 40, "seed": 3, "climb": 20})
    rock_w = LANDING_ROCK[1] - LANDING_ROCK[0]
    shapes = [
        # Walls at both ends, up to the top of the grid: the barrier holds the planet.
        shape("wall_left", 8, (TUTORIAL_TOP + bottom) / 2, 16, height, True, hard=True),
        shape("wall_right", TUTORIAL_W - 8, (TUTORIAL_TOP + bottom) / 2, 16, height, True,
              hard=True),
        shape("bedrock", TUTORIAL_W / 2, bottom - 32, TUTORIAL_W, 64, True, hard=True),
        # The landing is flat, on hard rock.
        shape("landing_air", (LANDING_ROCK[0] + LANDING_ROCK[1]) / 2, GROUND_Y - 100,
              rock_w + 200, 200, False),
        shape("landing_rock", (LANDING_ROCK[0] + LANDING_ROCK[1]) / 2, GROUND_Y + 200, rock_w,
              400, True, hard=True),
        # The pit, then the ledge.
        shape("pit", (GAP[0] + GAP[1]) / 2, GROUND_Y + 90, GAP[1] - GAP[0], 180, False),
        shape("ledge", (LEDGE[0] + LEDGE[1]) / 2, GROUND_Y - LEDGE[2] / 2 - 4,
              LEDGE[1] - LEDGE[0], LEDGE[2] + 8, True),
        # The basin.
        shape("basin", (BASIN[0] + BASIN[1]) / 2, GROUND_Y + BASIN[2] / 2 - 8,
              BASIN[1] - BASIN[0], BASIN[2] + 16, False),
    ]
    basin_floor = GROUND_Y + BASIN[2]
    things = [rock_entity(f"rock_{name}", x, GROUND_Y - 11)
              for name, x in (("a", 700), ("b", 760), ("c", 470), ("d", 830))]
    things += [fossil_entity(f"fossil_{name}", x, GROUND_Y - 11)
               for name, x in (("a", 420), ("b", 530))]
    for index, x in enumerate((2850, 3000, 3150, 3300)):
        things.append(fossil_entity(f"fossil_{index + 1}", x, basin_floor - 11))
    for index, x in enumerate((3500, 3650, 3800, 3950)):
        things.append(rock_entity(f"rock_{index + 1}", x, basin_floor - 11))
    # Early on the reserves cannot fall below a quarter: nothing to lose before learning.
    return (sky_entity() + scenery(GROUND_Y) + [ground] + shapes + things +
            [hero_entity(LANDING_X, 220, locked=True), camera(TUTORIAL_W, bottom)] +
            hero_clips() + hero_trail() + hands() + powers(harvest_locked=True) +
            inventory() + jump_hole() +
            death(watch_story=True) + reserves(matter=80, carbon=80, floor=25) + story() +
            landing() + departure() + pause_menu())


def landing():
    """The start of the tutorial: out of the white of the opening, he falls to the ground."""
    return [
        cover("whitein", 255),
        track("whitein_fades", "whitein", "sprite2d.Sprite.alpha", [(0, 255), (80, 0)]),
        # The great barrier, high above: a dome over the whole planet.
        entity("dome", 0, 2560, {"tag.Tag": {"tag": "dome"}, "sprite2d.Sprite": {
            "atlas": "dome", "width": 5600, "height": 5600, "layer": -90,
            "alpha": 230, "unlit": True}, "sprite2d.Parallax": {"x": 0.04, "y": 0.04}}),
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
                                      "pull": 2600, "pulls": ["rock"], "inner": HEART},
            "terrain2d.Digger": {"radius": 0, "layers": ["ground"]},
            "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                                "layer": LAYERS["hero"] + 7, "unlit": True},
            "transform2d.Scale": {"factor": 0},
            "transform2d.Rotation": {},
        }),
        bind("runaway_digs", "runaway", "attractor2d.Attractor.heart",
             "terrain2d.Digger.radius", 0, 5000, 0, 5000, 1, onto="runaway"),
        bind("runaway_shows", "runaway", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 5000, 0, 5000, 1, onto="runaway"),
        spin("runaway_turns", "runaway", 240),
        on("ground_shakes", "departure_done", "camera", "camera2d.CameraShake.strength", 14),
        track("dome_melts", "dome", "sprite2d.Sprite.alpha", [(60, 230), (300, 0)],
              start="departure_done"),
        cover("blast"),
        track("blast_comes", "blast", "sprite2d.Sprite.alpha", [(240, 0), (330, 255)],
              start="departure_done", end_action="exploded"),
        entity("to_drift", 0, 0, {"scene_switch.GoToScene": {
            "scene": "drift", "use_action": True, "action": "exploded"}}),
    ]


# The colours of the seven fragments, and the order they go out after the explosion (GDD §2:
# "plein de couleurs, puis de moins en moins").
FRAGMENT_COLOURS = [("pink", (240, 130, 190)), ("violet", (150, 90, 210)),
                    ("orange", (240, 140, 50)), ("red", (220, 60, 50)),
                    ("yellow", (240, 210, 80)), ("green", (80, 190, 110)),
                    ("blue", (70, 130, 230))]
PLANET_AT = (700, -120)
OPENING_TICKS = 900
OPENING_SKY = (2400, 1350)  # the opening's camera goes back to 0.55 of its zoom at rest


def opening():
    """The opening (GDD §2): an explosion, the universe spreads out full of colours, fewer and
    fewer, down to black and white and a sky like ours. The camera finds the hero curled up in an
    immense barrier, far, then drifting down to a planet, and comes close: white, and the game."""
    made = [
        # The skies stay on the screen; big enough for the camera's widest view.
        entity("stars", 0, 0, {"tag.Tag": {"tag": "stars"}, "sprite2d.Sprite": {
            "atlas": "sky", "width": OPENING_SKY[0], "height": OPENING_SKY[1], "layer": -110, "alpha": 0,
            "unlit": True}, "sprite2d.Parallax": {"x": 0, "y": 0}}),
        entity("colours", 0, 0, {"tag.Tag": {"tag": "colours"}, "sprite2d.Sprite": {
            "atlas": "sky_colour", "width": OPENING_SKY[0], "height": OPENING_SKY[1],
            "layer": -108,
            "alpha": 0, "unlit": True}, "sprite2d.Parallax": {"x": 0, "y": 0}}),
        entity("nebula", 200, 0, {"tag.Tag": {"tag": "nebula"}, "sprite2d.Sprite": {
            "atlas": "veil", "width": 2600, "height": 1460, "layer": -105, "alpha": 0,
            "unlit": True}, "sprite2d.Parallax": {"x": 0.25, "y": 0.25}}),
        entity("nebula_colour", 200, 0, {"tag.Tag": {"tag": "nebula_colour"},
                                         "sprite2d.Sprite": {
            "atlas": "veil_colour", "width": 2600, "height": 1460, "layer": -104, "alpha": 0,
            "unlit": True}, "sprite2d.Parallax": {"x": 0.25, "y": 0.25}}),
        entity("flash", 0, 0, {"tag.Tag": {"tag": "flash"}, "sprite2d.Sprite": {
            "atlas": "glow", "width": 300, "height": 300, "layer": 50, "additive": True,
            "unlit": True}, "transform2d.Scale": {"factor": 0.2}}),
        entity("camera", 0, 0, {"tag.Tag": {"tag": "camera"},
                                "camera2d.Camera": {"zoom": 1.3 * ZOOM},
                                "postfx.Effects": {"bloom": 0.8, "vignette": 0.5,
                                                   "vignette_size": 0.4, "grain": 0.05}}),
        entity("great_barrier", *PLANET_AT, {"tag.Tag": {"tag": "great_barrier"},
                                             "sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 820, "height": 820, "layer": 20,
            "alpha": 0, "unlit": True}}),
        entity("planet", PLANET_AT[0], PLANET_AT[1] + 70, {"tag.Tag": {"tag": "planet"},
                                                          "sprite2d.Sprite": {
            "atlas": "planet", "width": 300, "height": 300, "layer": 10, "alpha": 0,
            "unlit": True}}),
        entity("sleeper", 470, -330, {"tag.Tag": {"tag": "sleeper"}, "sprite2d.Sprite": {
            "atlas": "hero_curled", "width": 56, "height": 56, "layer": 30, "alpha": 0,
            "unlit": True}, "transform2d.Rotation": {}}),
        entity("sleeper_bubble", 470, -330, {"tag.Tag": {"tag": "sleeper_bubble"},
                                             "sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 64, "height": 64, "layer": 31, "alpha": 0,
            "unlit": True}, "follow2d.Follow": {"target": "sleeper", "stiffness": 1}}),
        cover("whiteout"),
        # The end, or a press of Escape: the planet.
        entity("to_tutorial", 0, 0, {"scene_switch.GoToScene": {
            "scene": "tutorial", "after_ticks": OPENING_TICKS, "use_action": True,
            "action": "skip"}}),
    ]
    # The explosion: every colour flies out, and goes out in turn; the grey stays longest.
    for index, (name, colour) in enumerate(FRAGMENT_COLOURS + [("grey", (200, 200, 200))]):
        made.append(entity(f"burst_{name}", 0, 0, {"particles2d.Emitter": {
            "rate": 0, "burst": 160, "lifetime": 2.2 + 0.6 * index if name != "grey" else 9,
            "lifetime_spread": 0.5, "speed": 440, "speed_spread": 320, "spread": 360,
            "drag": 0.45, "wander": 30, "size": 7, "end_size": 2.5, "red": colour[0],
            "green": colour[1], "blue": colour[2], "alpha": 255, "fade": True,
            "additive": True, "unlit": True, "layer": 40, "max": 180}}))
    made += [
        track("flash_grows", "flash", "transform2d.Scale.factor", [(0, 0.2), (30, 9)]),
        track("flash_fades", "flash", "sprite2d.Sprite.alpha", [(0, 255), (80, 0)]),
        # The colours of the universe come, then go: black and white is left.
        track("colours_come", "colours", "sprite2d.Sprite.alpha",
              [(40, 0), (160, 255), (300, 255), (470, 0)]),
        track("nebula_colour_comes", "nebula_colour", "sprite2d.Sprite.alpha",
              [(60, 0), (200, 230), (320, 230), (480, 0)]),
        track("stars_come", "stars", "sprite2d.Sprite.alpha", [(260, 0), (470, 255)]),
        track("dust_comes", "nebula", "sprite2d.Sprite.alpha", [(300, 0), (520, 210)]),
        # The camera: back from the explosion, still, then over to the barrier and down to it.
        track("camera_zoom", "camera", "camera2d.Camera.zoom",
              [(0, 1.3 * ZOOM), (240, 0.55 * ZOOM), (440, 0.55 * ZOOM), (580, 0.75 * ZOOM),
               (800, 2.6 * ZOOM)]),
        track("camera_x", "camera", "transform2d.Position.x",
              [(440, 0), (620, PLANET_AT[0]), (800, PLANET_AT[0] - 10)]),
        track("camera_y", "camera", "transform2d.Position.y",
              [(440, 0), (620, PLANET_AT[1]), (800, PLANET_AT[1] - 90)]),
        track("barrier_shows", "great_barrier", "sprite2d.Sprite.alpha", [(450, 0), (560, 220)]),
        track("planet_shows", "planet", "sprite2d.Sprite.alpha", [(450, 0), (540, 255)]),
        track("sleeper_shows", "sleeper", "sprite2d.Sprite.alpha", [(490, 0), (560, 255)]),
        track("sleeper_bubble_shows", "sleeper_bubble", "sprite2d.Sprite.alpha",
              [(490, 0), (560, 200)]),
        track("sleeper_x", "sleeper", "transform2d.Position.x",
              [(500, 470), (820, PLANET_AT[0] - 12)]),
        track("sleeper_y", "sleeper", "transform2d.Position.y",
              [(500, -330), (820, PLANET_AT[1] - 82)]),
        track("sleeper_turns", "sleeper", "transform2d.Rotation.angle", [(500, -30), (820, 20)]),
        track("white_comes", "whiteout", "sprite2d.Sprite.alpha", [(780, 0), (880, 255)]),
    ]
    return made


def drift():
    """After the tutorial (its end, until the space of the next steps): the hero curled up again,
    drifting among what is left of the planet; 'À suivre'."""
    return sky_entity() + [
        entity("camera", 0, 0, {"tag.Tag": {"tag": "camera"},
                                "camera2d.Camera": {"zoom": 1.6 * ZOOM},
                                "postfx.Effects": {"bloom": 0.5, "vignette": 0.55,
                                                   "vignette_size": 0.35, "grain": 0.05}}),
        entity("sleeper", -40, 10, {"tag.Tag": {"tag": "sleeper"}, "sprite2d.Sprite": {
            "atlas": "hero_curled", "width": 72, "height": 72, "layer": 30, "unlit": True},
            "transform2d.Velocity": {"x": 6, "y": -2}, "transform2d.Rotation": {}}),
        track("sleeper_turns", "sleeper", "transform2d.Rotation.angle", [(0, 0), (1200, 40)],
              smooth=False),
        # What is left of the planet: grey dust flying out.
        entity("debris", -300, 120, {"particles2d.Emitter": {
            "rate": 40, "lifetime": 7, "speed": 60, "speed_spread": 40, "angle": -160,
            "spread": 60, "wander": 8, "size": 3.5, "end_size": 1.5, "red": 190, "green": 190,
            "blue": 196, "alpha": 220, "fade": True, "unlit": True, "layer": 20,
            "area_width": 160, "area_height": 160, "max": 300}}),
        entity("to_follow", SCREEN_W / 2, SCREEN_H - 150, {"tag.Tag": {"tag": "to_follow"},
                                                          "text2d.Text": {
            "string": "tutorial.next", "font": "font", "size": 44, "pivot_x": 0.5,
            "pivot_y": 0.5, "screen": True, "alpha": 0}}),
        track("to_follow_shows", "to_follow", "text2d.Text.alpha", [(120, 0), (240, 230)]),
    ]


# ---------------------------------------------------------------------------------------------
# The manifest

ACTIONS = ["move_x", "jump", "out_of_matter", "out_of_carbon", "hand_left", "hand_right",
           "change", "pointer_x", "pointer_y", "harvest", "inventory", "take_off", "revive",
           "locked", "skip", "exploded", "refill", "pause", "resume", "to_title", "menu_up",
           "menu_down", "accept", "menu_click", "play", "quit"]
for _step in STEPS[:-1]:
    for _action in (f"{_step}_done", f"{_step}_next"):
        if _action not in ACTIONS and not _step.endswith("_practice"):
            ACTIONS.append(_action)
TAGS = ["hero", "hero_dust", "white_sparks", "black_hole_chips", "harvest_bubble_chips", "camera", "matter", "carbon", "black_hole", "white_caster", "harvest_bubble",
        "harvest_key", "ground_carbon", "inventory", "matter_bubble", "carbon_bubble",
        "jump_hole", "dead", "kit", "hand_left", "hand_right", "story", "clock", "stars",
        "colours", "nebula", "nebula_colour", "flash", "great_barrier", "planet", "sleeper",
        "sleeper_bubble", "whiteout", "whitein", "dome", "runaway", "blast", "to_follow", "pause"]
ATLASES = ["sky", "sky_colour", "veil", "veil_colour", "hills_far", "hills_near", "cave", "dome", "mountains", "mist", "ground_far", "front", "hero",
           "hero_curled", "orb", "black_hole", "rock", "fossil", "barrier", "harvest",
           "inventory", "plain", "font", "glow", "planet", "button"]
SCENE_NAMES = ["title", "opening", "tutorial", "drift", "sandbox", "sandbox_low"]
SCENARIOS = ["walks", "jumps", "terrain", "life_drains", "edges_turn", "assigns_hands", "wheel",
             "hands_apart", "black_hole", "barrier_catches_rock", "harvest_sorts",
             "inventory_opens", "walking_costs", "charged_jump", "dies_and_restarts",
             "tutorial_through", "cannot_walk_early", "powers_in_order",
             "opening", "opening_skips", "pause_stops_everything", "pause_resumes",
             "title_starts"]


def write_manifest():
    manifest = {
        "schema_version": 1,
        "name": "complete_universe",
        "title": "Complete Universe",
        "description": "A being of black and white ovals crosses a grey universe with two hands "
                       "of power: a black hole and a barrier. Harvest, survive, and give the "
                       "universe its colours back.",
        "modules": [
            "transform2d", "sprite2d", "light2d", "camera2d", "collision2d", "gravity2d",
            "action_control",
            "jump", "tag", "terrain2d", "reserve", "bind", "postfx", "aim2d", "loadout",
            "follow2d", "attractor2d", "barrier2d", "toggle", "rules", "checkpoint",
            "spawner2d", "bounds2d", "tween2d", "phase", "text2d", "particles2d",
            "scene_switch", "ui", "host"],
        "lists": {
            "action": ACTIONS,
            "layer": ["hero", "ground", "rock", "barrier"],
            "tag": TAGS,
            "resource": ["matter", "carbon"],
            "phase": STEPS,
            "power": POWERS,
            "animation_set": ["hero"],
            "menu": ["title", "pause"],
        },
        "simulation": {"tick_rate": 60, "seed": 11},
        "window": {"width": SCREEN_W, "height": SCREEN_H},
        "input": "input.json",
        "strings": ["text/fr.json"],
        "locale": "fr",
        "atlases": [f"art/{name}.json" for name in ATLASES],
        "prefabs": [f"prefabs/{prefab['name']}.json" for prefab in PREFAB_LIST],
        "scenes": [f"scenes/{name}.json" for name in SCENE_NAMES],
        "scenarios": [f"scenarios/{name}.json" for name in SCENARIOS],
        "start_scene": "title",
    }
    with open(os.path.join(GAME, "game.json"), "w", encoding="utf-8") as out:
        out.write("// Complete Universe: a being made of ovals gathers the fragments of power its "
                  "own explosion\n// scattered, and gives an universe of black and white its "
                  "colours back. The design is in\n// docs/complete_universe. Generated by "
                  "tools/make_assets.py: edit the script, not this file.\n")
        json.dump(manifest, out, indent=2, ensure_ascii=False)
        out.write("\n")


def write_prefabs():
    os.makedirs(PREFABS, exist_ok=True)
    for prefab in PREFAB_LIST:
        with open(os.path.join(PREFABS, prefab["name"] + ".json"), "w", encoding="utf-8") as out:
            out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
            json.dump(prefab, out, indent=2)
            out.write("\n")


def write_strings():
    os.makedirs(TEXT, exist_ok=True)
    entries = {}
    for step, (title, line) in TEXTS.items():
        entries[f"tutorial.{step}.title"] = title
        entries[f"tutorial.{step}.line"] = line
    entries["tutorial.practice"] = PRACTICE_TEXT
    entries["tutorial.next"] = "À suivre…"
    entries.update({"menu.title": "COMPLETE UNIVERSE", "menu.play": "Jouer",
                    "menu.quit": "Quitter", "menu.pause": "Pause", "menu.resume": "Reprendre",
                    "menu.leave": "Retour au titre"})
    with open(os.path.join(TEXT, "fr.json"), "w", encoding="utf-8") as out:
        json.dump({"schema_version": 1, "locale": "fr", "entries": entries}, out, indent=2,
                  ensure_ascii=False)
        out.write("\n")


def write_scene(name, entities):
    os.makedirs(SCENES, exist_ok=True)
    with open(os.path.join(SCENES, name + ".json"), "w", encoding="utf-8") as out:
        out.write("// Generated by tools/make_assets.py: edit the script, not this file.\n")
        json.dump({"schema_version": 1, "name": name, "entities": entities}, out, indent=2,
                  ensure_ascii=False)
        out.write("\n")


def write_scenes():
    write_scene("sandbox", sandbox_scene())
    write_scene("sandbox_low", sandbox_scene(matter=20, carbon=8))
    write_scene("tutorial", tutorial())
    write_scene("opening", opening())
    write_scene("title", title())
    write_scene("drift", drift())


def write_pictures():
    save(hero_sheet(), "hero", cell=(CELL_PX, CELL_PX))
    save(hero_curled(), "hero_curled")
    save(sky(), "sky")
    save(sky(coloured=True), "sky_colour")
    save(nebula_veil(), "veil")
    save(nebula_veil(coloured=True), "veil_colour")
    save(hills(12, 170, 30, 9, 30), "hills_far")
    save(hills(13, 120, 13, 14, 8), "hills_near")
    save(orbs(), "orb", cell=(ORB, ORB))
    save(cave(), "cave")
    save(mountains(), "mountains")
    save(mist(51), "mist")
    save(ground_far(), "ground_far")
    save(foreground(), "front")
    save(black_hole(), "black_hole")
    save(rock(5), "rock")
    save(rock(9, colour=(120, 118, 110), light=(226, 224, 214), spiral=True), "fossil")
    save(barriers(), "barrier", cell=(BARRIER, BARRIER))
    save(dome(), "dome")
    save(sheet_of([button(False), button(True)], 2, *BUTTON), "button", cell=BUTTON)
    save(harvest(), "harvest")
    save(inventory_bubbles(), "inventory", cell=(INVENTORY, INVENTORY))
    save(Image.new("RGBA", (4, 4), (255, 255, 255, 255)), "plain")
    save(glow(), "glow")
    save(planet(), "planet")
    save_font()


def main():
    import sys
    only_scenes = "--scenes" in sys.argv
    if not only_scenes:
        write_pictures()
    write_manifest()
    write_prefabs()
    write_strings()
    write_scenes()


if __name__ == "__main__":
    main()
