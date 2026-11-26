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
CELL_PX = 160  # ... and in pixels (about its size on the screen)

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
# The proportions, after Martin's picture (docs/complete_universe): the head is nearly as wide as
# the body is tall; the arms and legs are plump drops. World units.
HEAD_R = 17.5
BODY_A, BODY_B = 11.2, 18.0  # half width and half height of the body
LEG_L, LEG_W = 36.0, 6.2
ARM_L, ARM_W = 28.0, 5.7
HERO_LIGHT = np.array([-0.5, -0.62, 0.6]) / np.linalg.norm([-0.5, -0.62, 0.6])


class Pose:
    """Where the parts of the hero are in one picture. Angles in degrees."""

    def __init__(self, **values):
        self.turn = 0.0  # -1: the face towards the left of the screen, 1 the right
        self.x = 0.0  # the whole body, sideways
        self.bob = 0.0  # the body up (negative) or down, beyond what the legs give
        self.squash = 1.0  # the body taller (above 1) or flatter
        self.lean = 0.0  # the top towards the right when positive
        self.tilt = 0.0  # the head
        self.head_drop = 0.0  # the head lower on the body (curled up)
        self.leg_w = -6.0  # the white leg, from straight down, positive towards the right
        self.leg_b = 6.0
        self.leg_len_w = LEG_L
        self.leg_len_b = LEG_L
        self.arm_w = -26.0  # the white arm
        self.arm_b = 26.0
        self.limbs_front = False  # the legs and arms before the body (curled up)
        self.blink = 1.0  # how open the eyes are
        self.look_x = 0.0
        self.look_y = 0.0
        self.hole = None  # (x, y, size): a small black hole pulling a foot
        self.ground = True  # the lower foot stands on the ground
        # How the soft shapes give with the motion (set by follow_through): each limb bends, its
        # tip lagging behind where it goes, and stretches with its speed; the head squashes.
        self.bend = {"arm_w": 0.0, "arm_b": 0.0, "leg_w": 0.0, "leg_b": 0.0}
        self.stretch = {"arm_w": 1.0, "arm_b": 1.0, "leg_w": 1.0, "leg_b": 1.0}
        self.head_squash = 1.0  # below 1: wider and flatter
        self.__dict__.update(values)


def hero_grid():
    ys, xs = np.mgrid[0:HN, 0:HN].astype(np.float64) / HS
    return xs, ys


HERO_X, HERO_Y = hero_grid()


def ovoid(cx, cy, a, b, angle=0.0, egg=0.0, bend=0.0):
    """An ovoid around (cx, cy) in a hero cell, half axes a (across) and b (along), turned angle
    degrees; egg widens its +v end, bend curves it (its +v end goes sideways by bend times a,
    its -v end stays). Its coverage (smooth edged), the normal of its round surface (nx, ny,
    nz), as if it were a lit pebble, and about how far each point is outside it (negative
    inside), in world units."""
    t = math.radians(angle)
    dx, dy = HERO_X - cx, HERO_Y - cy
    u = dx * math.cos(t) + dy * math.sin(t)
    v = -dx * math.sin(t) + dy * math.cos(t)
    if bend:
        u = u - bend * a * (np.clip(v / b, -1, 1) + 1) ** 2 / 4
    widen = 1 + egg * np.clip(v / b, -1, 1)
    un, vn = u / (a * widen), v / b
    r = np.sqrt(un * un + vn * vn)
    distance = (r - 1) * min(a, b)
    cover = np.clip(-distance * HS * 0.9, 0, 1)
    nz = np.sqrt(np.clip(1 - r * r, 0, 1))
    nx = un * math.cos(t) - vn * math.sin(t)
    ny = un * math.sin(t) + vn * math.cos(t)
    return cover, nx, ny, nz, distance


def hero_shade(nx, ny, nz, white):
    """White: bright, lit from the top left, greyer towards its edges. Black: a soft highlight,
    and a grey rim on the side of the white half's glow."""
    diffuse = np.clip(nx * HERO_LIGHT[0] + ny * HERO_LIGHT[1] + nz * HERO_LIGHT[2], 0, 1)
    edge = (1 - nz) ** 2.2
    if white:
        tone = 200 + 52 * diffuse ** 0.7 - 70 * edge
        return np.dstack([tone * 0.98, tone * 0.99, tone * 1.02])
    spec = np.clip(nx * -0.35 + ny * -0.55 + nz * 0.76, 0, 1) ** 18
    tone = 8 + 22 * diffuse ** 2 + 58 * edge * np.clip(0.4 - nx * 0.8, 0, 1) + 60 * spec
    return np.dstack([tone, tone, tone * 1.06])


def hero_frame(pose):
    """One picture of the hero, CELL_PX square, from a Pose: ovoids laid from the back to the
    front, split white and black along a curved meridian, then the eyes and the glow."""
    cx = CELL / 2 + pose.x
    reach = max(math.cos(math.radians(pose.leg_w)) * pose.leg_len_w,
                math.cos(math.radians(pose.leg_b)) * pose.leg_len_b)
    hip_y = (FEET - reach + 3 if pose.ground else FEET - LEG_L + 2) + pose.bob
    body_b = BODY_B * pose.squash
    body_a = BODY_A / math.sqrt(pose.squash)
    lean = math.radians(pose.lean)
    body_cx = cx + math.sin(lean) * body_b * 0.6
    body_cy = hip_y - body_b * 0.62
    head_cx = body_cx + math.sin(lean) * 26 + pose.turn * 1.5
    head_cy = body_cy - body_b - HEAD_R * 0.78 + pose.head_drop
    shoulder_y = body_cy - body_b * 0.56
    turn = math.radians(pose.turn * 38)

    layers = {"rgb": np.zeros((HN, HN, 3)), "alpha": np.zeros((HN, HN)),
              "white": np.zeros((HN, HN))}

    def lay(cover, colour, whiteness):
        layers["rgb"] = layers["rgb"] * (1 - cover[..., None]) + colour * cover[..., None]
        layers["alpha"] = layers["alpha"] + cover * (1 - layers["alpha"])
        layers["white"] = layers["white"] * (1 - cover) + whiteness * cover

    def limb(jx, jy, angle, length, width, white, egg, part):
        a = math.radians(angle)
        length = length * pose.stretch[part]
        width = width / math.sqrt(pose.stretch[part])
        shape = ovoid(jx + math.sin(a) * length * 0.42, jy + math.cos(a) * length * 0.42, width,
                      length / 2, -angle, egg, pose.bend[part])
        return shape, np.full((HN, HN), 1.0 if white else 0.0)

    def halves(shape, middle_y, half_h, middle_x, half_w):
        # The meridian between the halves, curved over the round shape as it turns.
        lat = np.sqrt(np.clip(1 - ((HERO_Y - middle_y) / half_h) ** 2, 0, 1))
        boundary = middle_x + half_w * math.sin(turn) * lat
        return shape, np.clip((boundary - HERO_X) * HS * 0.5 + 0.5, 0, 1)

    def paint(shape, whiteness):
        cover, nx, ny, nz = shape[:4]
        share = whiteness[..., None]
        colour = hero_shade(nx, ny, nz, True) * share + hero_shade(nx, ny, nz, False) * (
            1 - share)
        lay(cover, colour, whiteness)

    def melt(parts, k=3.5):
        """One soft shape out of several (a smooth union): where the arm meets the body, a
        round fillet, as if it grew out of it."""
        (shape, white) = parts[0]
        _, nx, ny, nz, dist = shape
        for (other, other_white) in parts[1:]:
            _, ox, oy, oz, od = other
            h = np.clip(0.5 + 0.5 * (od - dist) / k, 0, 1)  # the share of the first
            dist = od * (1 - h) + dist * h - k * h * (1 - h)
            nx, ny, nz = nx * h + ox * (1 - h), ny * h + oy * (1 - h), nz * h + oz * (1 - h)
            white = white * h + other_white * (1 - h)
        # The fillet is inside both rounds' edges: keep it lit, not dark as an edge.
        nz = np.maximum(nz, np.sqrt(np.clip(-dist / 4.0, 0, 1)) * 0.75)
        norm = np.sqrt(nx * nx + ny * ny + nz * nz) + 1e-6
        cover = np.clip(-dist * HS * 0.9, 0, 1)
        paint((cover, nx / norm, ny / norm, nz / norm, dist), white)

    hip_dx = 5.2 * math.cos(turn)
    shoulder_dx = body_a * 0.62 * math.cos(turn)  # inside the body: the arm grows out of it
    legs = [(body_cx - hip_dx, pose.leg_w, pose.leg_len_w, True),
            (body_cx + hip_dx, pose.leg_b, pose.leg_len_b, False)]
    arms = [(body_cx - shoulder_dx + math.sin(lean) * 8, pose.arm_w, True),
            (body_cx + shoulder_dx + math.sin(lean) * 8, pose.arm_b, False)]

    def behind(white):
        # Turned to the right, the black arm and leg go behind the body; to the left, the white.
        return (white and pose.turn < -0.15) or (not white and pose.turn > 0.15)

    def leg_shapes(back):
        return [limb(jx, hip_y - 4, angle, length, LEG_W, white, 0.28,
                     "leg_w" if white else "leg_b")
                for jx, angle, length, white in legs if behind(white) == back]

    def arm_shapes(back):
        return [limb(jx, shoulder_y, angle, ARM_L, ARM_W, white, 0.3,
                     "arm_w" if white else "arm_b")
                for jx, angle, white in arms if behind(white) == back]

    for part in leg_shapes(True) + arm_shapes(True):
        paint(*part)
    if not pose.limbs_front:
        for part in leg_shapes(False):
            paint(*part)
    body = halves(ovoid(body_cx, body_cy, body_a, body_b, -pose.lean, egg=0.22), body_cy,
                  body_b, body_cx, body_a)
    if pose.limbs_front:
        paint(*body)
        for part in leg_shapes(False) + arm_shapes(False):
            paint(*part)
    else:
        melt([body] + arm_shapes(False))
    head_a = HEAD_R / math.sqrt(pose.head_squash)
    head_b = HEAD_R * 0.97 * pose.head_squash
    head_cy += HEAD_R * (1 - pose.head_squash) * 0.8  # it squashes onto the body
    paint(*halves(ovoid(head_cx, head_cy, head_a, head_b, -pose.tilt), head_cy, head_b,
                  head_cx, head_a))

    # The eyes, on the round of the head: big ovals a little tilted towards each other, glowing.
    eyes = np.zeros((HN, HN))
    for side in (-1, 1):
        longitude = turn + side * math.radians(31)
        if math.cos(longitude) < 0.15:
            continue
        squeeze = math.cos(longitude) ** 0.8
        ex = head_cx + HEAD_R * math.sin(longitude) + pose.look_x
        ey = head_cy + 2.2 + pose.look_y + math.sin(math.radians(pose.tilt)) * side * 4
        eyes = np.maximum(eyes, ovoid(ex, ey, 4.1 * squeeze, 5.3 * max(pose.blink, 0.06),
                                      -side * 14 * squeeze - pose.tilt)[0])
    # On the white half, a soft grey hollow around each eye, so that it shows.
    hollow = np.clip(blur(eyes, 2.0 * HS) * 2.4 - eyes, 0, 1) * layers["white"]
    rgb = layers["rgb"] * (1 - hollow[..., None] * 0.62)
    eye_glow = blur(eyes, 2.2 * HS)
    rgb = rgb * (1 - eyes[..., None]) + np.array([252, 253, 255]) * eyes[..., None]
    alpha = layers["alpha"]

    # The small black hole that pulls a foot as he walks (D15).
    hole_glow = np.zeros((HN, HN))
    if pose.hole is not None and pose.hole[2] > 0.05:
        hx, hy, size = pose.hole
        disc = ovoid(hx, hy, 3.0 * size, 1.4 * size)[0]
        ring = np.clip(ovoid(hx, hy, 4.2 * size, 2.0 * size)[0] - disc, 0, 1)
        rgb = rgb * (1 - ring[..., None]) + np.array([200, 222, 255]) * ring[..., None]
        rgb = rgb * (1 - disc[..., None])
        alpha = np.maximum(alpha, np.maximum(disc, ring))
        hole_glow = blur(ring, 2 * HS) * 0.8

    # The glow: strong around the white half, none around the black; and around the eyes.
    white_body = layers["white"] * layers["alpha"]
    halo = np.clip(np.maximum(blur(white_body, 5.5 * HS) * 0.85 + blur(white_body, 1.6 * HS)
                              * 0.35, hole_glow), 0, 1)
    out_alpha = alpha + halo * (1 - alpha)
    out_rgb = (rgb * alpha[..., None] + np.array([236, 240, 255]) * (halo * (1 - alpha))[
        ..., None]) / np.maximum(out_alpha[..., None], 1e-4)
    shine = np.clip(eye_glow * 1.1 - eyes, 0, 1)
    out_rgb = out_rgb + (255 - out_rgb) * shine[..., None] * 0.85
    out_alpha = np.maximum(out_alpha, shine)
    return shrink(out_rgb, out_alpha, CELL_PX, CELL_PX)


def smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


def follow_through(poses, looping=True):
    """Makes the soft shapes give with the motion: each limb bends against the way it turns
    (its tip lags behind), and stretches a little with its speed; the head squashes as the body
    squashes. Speeds are taken between the frames before and after (around the loop)."""
    count = len(poses)
    for index, pose in enumerate(poses):
        before = poses[(index - 1) % count if looping else max(index - 1, 0)]
        after = poses[(index + 1) % count if looping else min(index + 1, count - 1)]
        for part in pose.bend:
            speed = (getattr(after, part) - getattr(before, part)) / 2  # degrees a frame
            # Speeds in degrees a frame, at sixty frames a second.
            pose.bend[part] = -max(-0.9, min(0.9, speed * 0.18))
            pose.stretch[part] = 1 + min(0.12, abs(speed) * 0.024)
        pose.head_squash = 1 - (1 - pose.squash) * 0.9
    return poses


def idle_poses(count=160):
    """At rest, looking at us: he breathes, sways his head, his arms follow a little late, and
    he blinks once. Every motion is a whole number of waves over the loop: no jump."""
    poses = []
    for index in range(count):
        t = index / count
        breath = math.sin(2 * math.pi * t)
        late = math.sin(2 * math.pi * t - 0.7)
        blink = 1 - math.exp(-((t - 0.72) / 0.024) ** 2) * 0.96
        poses.append(Pose(squash=1 + 0.028 * breath, bob=-0.6 * breath,
                          tilt=3.2 * math.sin(2 * math.pi * t + 1.1),
                          turn=0.08 * math.sin(2 * math.pi * t + 2.0),
                          look_x=0.6 * math.sin(2 * math.pi * t + 2.0),
                          arm_w=-26 - 3.5 * late, arm_b=26 + 3.5 * late,
                          leg_w=-6 + 0.4 * breath, leg_b=6 - 0.4 * breath, blink=blink))
    return poses


def walk_poses(facing, count=48):
    """Two steps, the face and chest turned towards where he goes. Before each foot lands, a
    small black hole opens where it will land and pulls it in (D15)."""
    poses = []
    swing = 32.0
    for index in range(count):
        p = index / count
        s, c = math.sin(2 * math.pi * p), math.cos(2 * math.pi * p)
        leg_w, leg_b = facing * swing * s, -facing * swing * s
        # The leg that swings forward is in the air: a little shorter.
        lift_w, lift_b = max(0.0, c) ** 1.5, max(0.0, -c) ** 1.5
        # The hole of the foot that is about to land: it opens during the swing.
        air_w = ((p + 0.25) % 1.0) / 0.5
        air_b = ((p + 0.75) % 1.0) / 0.5
        hole = None
        for air, white in ((air_w, True), (air_b, False)):
            if air <= 1.3:
                size = smooth(air / 0.9) if air <= 1.0 else 1 - smooth((air - 1.0) / 0.3)
                hip = CELL / 2 + (-5.2 if white else 5.2) * 0.6
                land = hip + facing * math.sin(math.radians(swing)) * LEG_L + facing * 4
                hole = (land, FEET + 1.5, 1.7 * size)
        poses.append(Pose(turn=0.72 * facing, lean=6 * facing, leg_w=leg_w, leg_b=leg_b,
                          leg_len_w=LEG_L * (1 - 0.16 * lift_w),
                          leg_len_b=LEG_L * (1 - 0.16 * lift_b),
                          arm_w=-22 - facing * 24 * s, arm_b=22 - facing * 24 * s,
                          squash=1 + 0.045 * math.cos(4 * math.pi * p),
                          tilt=2.5 * facing + 1.5 * math.sin(4 * math.pi * p),
                          look_x=1.2 * facing, hole=hole))
    return poses


def charge_poses(count=32):
    """Space held: he crouches and lifts his arms towards the black hole over his head."""
    poses = []
    for index in range(count):
        k = smooth(index / (count - 1))
        poses.append(Pose(leg_w=-(6 + 16 * k), leg_b=6 + 16 * k,
                          leg_len_w=LEG_L * (1 - 0.24 * k), leg_len_b=LEG_L * (1 - 0.24 * k),
                          squash=1 - 0.1 * k, arm_w=-(26 + 128 * k), arm_b=26 + 128 * k,
                          look_y=-2.2 * k, tilt=0))
    return poses


def rise_poses(count=24):
    poses = []
    for index in range(count):
        sway = math.sin(2 * math.pi * index / count)
        poses.append(Pose(ground=False, squash=1.07, leg_w=-3 + 3 * sway, leg_b=3 + 3 * sway,
                          arm_w=-(148 + 6 * sway), arm_b=148 - 6 * sway, look_y=-1.6,
                          tilt=1.5 * sway))
    return poses


def fall_poses(count=24):
    poses = []
    for index in range(count):
        sway = math.sin(2 * math.pi * index / count)
        poses.append(Pose(ground=False, squash=0.97, leg_w=-14 - 4 * sway, leg_b=14 - 4 * sway,
                          arm_w=-(100 + 10 * sway), arm_b=100 - 10 * sway, look_y=1.6,
                          tilt=-1.5 * sway))
    return poses


def float_poses(count=96):
    """In space, nothing pulls him: he floats, arms spread, legs apart, breathing slowly, his
    limbs drifting a little (D26). Every motion is a whole number of waves over the loop."""
    poses = []
    for index in range(count):
        t = index / count
        breath = math.sin(2 * math.pi * t)
        late = math.sin(2 * math.pi * t - 0.8)
        poses.append(Pose(ground=False, squash=1 + 0.02 * breath, bob=-0.8 * breath,
                          tilt=2.5 * math.sin(2 * math.pi * t + 0.6),
                          leg_w=-23 - 2.5 * late, leg_b=23 + 2.5 * late,
                          arm_w=-(62 + 5 * late), arm_b=62 + 5 * breath, look_y=0.4))
    return poses


POSE_NUMBERS = ["turn", "x", "bob", "squash", "lean", "tilt", "head_drop", "leg_w", "leg_b",
                "leg_len_w", "leg_len_b", "arm_w", "arm_b", "blink", "look_x", "look_y"]


def between(a, b, count, extra=None):
    """count poses from a to b (both kept), eased in and out; extra(k) adds to each, k from 0 to
    1, what the passage itself does (a dip, a hop, a stretch)."""
    poses = []
    for index in range(count):
        k = smooth(index / (count - 1))
        values = {name: getattr(a, name) * (1 - k) + getattr(b, name) * k
                  for name in POSE_NUMBERS}
        values["ground"] = a.ground if k < 0.5 else b.ground
        if extra is not None:
            for name, value in extra(index / (count - 1)).items():
                values[name] = values[name] + value
        poses.append(Pose(**values))
    return poses


def bump(k):
    """0 at both ends, 1 in the middle."""
    return math.sin(math.pi * k)


def start_poses(facing):
    """From rest to walking: he turns his face and chest, leans in, and the first leg goes."""
    return between(idle_poses()[0], walk_poses(facing)[0], 16,
                   lambda k: {"squash": -0.05 * bump(k), "lean": 3 * facing * bump(k)})


def stop_poses(facing):
    """From walking to rest: he leans back a little to stop, then turns to face us."""
    return between(walk_poses(facing)[0], idle_poses()[0], 16,
                   lambda k: {"lean": -5 * facing * bump(k), "squash": 0.04 * bump(k),
                              "arm_w": 12 * facing * bump(k), "arm_b": 12 * facing * bump(k)})


def turn_poses(facing):
    """Walking one way, then the other: a little hop as he turns through facing us."""
    return between(walk_poses(-facing)[0], walk_poses(facing)[0], 16,
                   lambda k: {"bob": -2.5 * bump(k), "squash": 0.05 * bump(k),
                              "arm_w": -10 * bump(k), "arm_b": 10 * bump(k)})


def land_poses():
    """Landing: the body gives (legs bent, body squashed, arms out), comes up a little too far,
    then settles."""
    crouch = Pose(squash=0.8, bob=3, leg_w=-24, leg_b=24, leg_len_w=LEG_L * 0.78,
                  leg_len_b=LEG_L * 0.78, arm_w=-62, arm_b=62, look_y=1.5, tilt=0)
    rebound = Pose(squash=1.05, arm_w=-20, arm_b=20)
    return (between(fall_poses()[0], crouch, 6) + between(crouch, rebound, 8)[1:] +
            between(rebound, idle_poses()[0], 8)[1:])


def apex_poses():
    """At the top of a jump: the arms come down, the legs part, he looks down."""
    return between(rise_poses()[0], fall_poses()[0], 12)


def leap_poses():
    """Letting the charge go: he stretches up out of his crouch."""
    return between(charge_poses()[-1], rise_poses()[0], 8, lambda k: {"squash": 0.12 * bump(k)})


def side_legs(facing, front, back, front_len=1.0, back_len=1.0):
    """The leg angles and lengths of a leap to one side: front is the angle of the leg ahead
    (towards where he goes), back of the one behind. The leg ahead is the one on that side."""
    ahead, behind = facing * front, facing * back
    if facing > 0:  # the black leg is on the right
        return {"leg_b": ahead, "leg_w": behind, "leg_len_b": LEG_L * front_len,
                "leg_len_w": LEG_L * back_len}
    return {"leg_w": ahead, "leg_b": behind, "leg_len_w": LEG_L * front_len,
            "leg_len_b": LEG_L * back_len}


def side_rise_poses(facing, count=24):
    """A leap to one side: the face and chest turned that way, the leg ahead drawn up, the one
    behind stretched back, the arm ahead reaching up and forward."""
    poses = []
    for index in range(count):
        sway = math.sin(2 * math.pi * index / count)
        legs = side_legs(facing, 40 + 4 * sway, -24 + 3 * sway, 0.78, 1.0)
        ahead_arm = facing * (125 + 8 * sway)
        behind_arm = -facing * (55 + 6 * sway)
        arms = ({"arm_b": ahead_arm, "arm_w": behind_arm} if facing > 0 else
                {"arm_w": ahead_arm, "arm_b": behind_arm})
        poses.append(Pose(ground=False, squash=1.06, turn=0.72 * facing, lean=9 * facing,
                          look_x=1.4 * facing, look_y=-1.0, tilt=3 * facing, **legs, **arms))
    return poses


def side_fall_poses(facing, count=24):
    """Coming down to one side: the legs reach ahead for the ground, the arms open."""
    poses = []
    for index in range(count):
        sway = math.sin(2 * math.pi * index / count)
        legs = side_legs(facing, 24 + 3 * sway, 8 - 3 * sway)
        poses.append(Pose(ground=False, squash=0.98, turn=0.72 * facing, lean=-3 * facing,
                          arm_w=-(100 + 10 * sway), arm_b=100 - 10 * sway, look_x=1.2 * facing,
                          look_y=1.6, tilt=-2 * facing, **legs))
    return poses


def side_charge_poses(facing, count=32):
    """Space held while going one way: he crouches turned that way, leaning in, his arms swung
    back to throw himself forward."""
    poses = []
    for index in range(count):
        k = smooth(index / (count - 1))
        legs = side_legs(facing, 14 + 18 * k, -(8 + 14 * k), 1 - 0.26 * k, 1 - 0.18 * k)
        back = -facing * (30 + 70 * k)
        arms = {"arm_w": back - 12 * facing, "arm_b": back + 12 * facing}
        poses.append(Pose(turn=0.72 * facing, lean=(6 + 8 * k) * facing, squash=1 - 0.1 * k,
                          look_x=1.4 * facing, look_y=-0.6 * k, tilt=2 * facing, **legs,
                          **arms))
    return poses


def side_leap_poses(facing):
    """Letting the charge go while going one way: he unfolds forward and up."""
    return between(side_charge_poses(facing)[-1], side_rise_poses(facing)[0], 8,
                   lambda k: {"squash": 0.1 * bump(k), "lean": 4 * facing * bump(k)})


def side_apex_poses(facing):
    return between(side_rise_poses(facing)[0], side_fall_poses(facing)[0], 12)


# The animations of the hero: (name, frames, ticks per frame), in the order of the atlas. One frame
# a tick: sixty pictures a second, a new one at every step of the game.
HERO_CLIPS = [("idle", follow_through(idle_poses()), 1),
              ("walk_right", follow_through(walk_poses(1)), 1),
              ("walk_left", follow_through(walk_poses(-1)), 1),
              ("charge", follow_through(charge_poses(), looping=False), 1),
              ("rise", follow_through(rise_poses()), 1), ("fall", follow_through(fall_poses()), 1),
              ("rise_right", follow_through(side_rise_poses(1)), 1),
              ("rise_left", follow_through(side_rise_poses(-1)), 1),
              ("fall_right", follow_through(side_fall_poses(1)), 1),
              ("fall_left", follow_through(side_fall_poses(-1)), 1),
              ("charge_right", follow_through(side_charge_poses(1), looping=False), 1),
              ("charge_left", follow_through(side_charge_poses(-1), looping=False), 1)]
# The transitions between them (sprite2d): (name, frames, ticks per frame, from, to), to None
# meaning any clip. They follow the clips in the atlas. Frames given as a name use the frames of
# that transition again (a landing from a side jump is a landing).
HERO_TRANSITIONS = [
    ("start_right", follow_through(start_poses(1), False), 1, "idle", "walk_right"),
    ("start_left", follow_through(start_poses(-1), False), 1, "idle", "walk_left"),
    ("stop_right", follow_through(stop_poses(1), False), 1, "walk_right", "idle"),
    ("stop_left", follow_through(stop_poses(-1), False), 1, "walk_left", "idle"),
    ("turn_left", follow_through(turn_poses(-1), False), 1, "walk_right", "walk_left"),
    ("turn_right", follow_through(turn_poses(1), False), 1, "walk_left", "walk_right"),
    ("land", follow_through(land_poses(), False), 1, "fall", None),
    ("land_from_right", "land", 1, "fall_right", None),
    ("land_from_left", "land", 1, "fall_left", None),
    ("apex", follow_through(apex_poses(), False), 1, "rise", "fall"),
    ("apex_right", follow_through(side_apex_poses(1), False), 1, "rise_right", "fall_right"),
    ("apex_left", follow_through(side_apex_poses(-1), False), 1, "rise_left", "fall_left"),
    ("leap", follow_through(leap_poses(), False), 1, "charge", "rise"),
    ("leap_right", follow_through(side_leap_poses(1), False), 1, "charge_right", "rise_right"),
    ("leap_left", follow_through(side_leap_poses(-1), False), 1, "charge_left", "rise_left"),
]



def passages():
    """Every other passage he can make between two clips (D29): from standing or walking into a
    fall, a landing that goes on walking, a turn to the other side in the air, a jump that comes
    down on the other side... each from the pose the first clip starts with (ends with, for a
    charge) to the one the second starts with, eased, as the walk's are. With the Animator's
    finish, each plays to its end: no jump from the middle of one."""
    clips = {name: poses for name, poses, _ in HERO_CLIPS}
    start = {name: poses[0] for name, poses in clips.items()}
    end = {name: poses[-1] for name, poses in clips.items()}
    crouch = Pose(squash=0.8, bob=3, leg_w=-24, leg_b=24, leg_len_w=LEG_L * 0.78,
                  leg_len_b=LEG_L * 0.78, arm_w=-62, arm_b=62, look_y=1.5, tilt=0)

    def landing_into(source, target):
        """The body gives as he lands, then he goes on into the target (walking, standing)."""
        low = between(crouch, target, 3)[1]
        return between(source, low, 6) + between(low, target, 11)[1:]

    ground, rise = ["idle", "walk_right", "walk_left"], ["rise", "rise_right", "rise_left"]
    fall, charge = ["fall", "fall_right", "fall_left"], ["charge", "charge_right", "charge_left"]
    known = {(source, target) for _, _, _, source, target in HERO_TRANSITIONS}
    made = []

    def add(source, target, poses):
        if (source, target) not in known:
            made.append((f"{source}_to_{target}", follow_through(poses, False), 1, source,
                         target))
            known.add((source, target))

    for source in ground:
        for target in fall:
            add(source, target, between(start[source], start[target], 10))
        for target in charge:
            add(source, target, between(start[source], start[target], 6))
    for source in fall:
        for target in ("walk_right", "walk_left"):
            add(source, target, landing_into(start[source], start[target]))
        for target in fall:
            if target != source:
                add(source, target, between(start[source], start[target], 10))
    for source in rise:
        for target in ground:
            add(source, target, landing_into(start[source], start[target]))
        for target in rise:
            if target != source:
                add(source, target, between(start[source], start[target], 10))
        for target in fall:
            add(source, target, between(start[source], start[target], 12))
    for source in charge:
        for target in rise:
            add(source, target, between(end[source], start[target], 8,
                                        lambda k: {"squash": 0.1 * bump(k)}))
    return made


HERO_TRANSITIONS += passages()


# In space (D26): its own set of animations, floating when he hardly moves, flying (the arms
# of the fall) when he goes; passages between them, and from his flight up into orbit.
SPACE_FLOAT = follow_through(float_poses())
SPACE_TRANSITIONS = [
    # (name, frames, ticks per frame, from (set, clip), to clip of the space set)
    ("float_to_fly", follow_through(between(SPACE_FLOAT[0], fall_poses()[0], 10), False), 1,
     ("hero_space", "float"), "fly"),
    ("fly_to_float", follow_through(between(fall_poses()[0], SPACE_FLOAT[0], 18), False), 1,
     ("hero_space", "fly"), "float"),
    ("rise_to_fly", follow_through(between(rise_poses()[0], fall_poses()[0], 12), False), 1,
     ("hero", "rise"), "fly"),
    ("rise_to_float", follow_through(between(rise_poses()[0], SPACE_FLOAT[0], 18), False), 1,
     ("hero", "rise"), "float"),
]
SPACE_IDS = {"float": 101, "fly": 102}
SPACE_STILL = 70  # slower than this along both axes: he floats

HERO_COLUMNS = 36  # 5760 pixels wide (under 8192 both ways)


def hero_sheet():
    frames = []
    for clip in HERO_CLIPS + HERO_TRANSITIONS:
        if not isinstance(clip[1], str):
            frames += [hero_frame(pose) for pose in clip[1]]
    frames += [hero_frame(pose) for pose in SPACE_FLOAT]
    for clip in SPACE_TRANSITIONS:
        frames += [hero_frame(pose) for pose in clip[1]]
    return sheet_of(frames, HERO_COLUMNS, CELL_PX, CELL_PX)


def hero_curled():
    """The hero asleep, as he drifts in his barrier: floating as in Martin's picture, limbs loose,
    eyes shut."""
    pose = Pose(ground=False, tilt=-7, lean=-4, blink=0.06, look_y=1.2, leg_w=-14, leg_b=10,
                leg_len_w=LEG_L * 0.95, arm_w=-38, arm_b=30, bob=-6)
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


def swirl(r, theta, seed, arms=120, t=0.0, wind=1.6):
    """Streaks turning around a centre, 0 to 1, as matter falling into a black hole: many thin
    ones wound tight around it. At t (0 to 1, a loop) each streak swells and fades in its own
    time, so that they seem to come and go as they turn."""
    rng = np.random.default_rng(seed)
    total = np.zeros_like(r)
    log_r = np.log(np.maximum(r, 1e-3))
    for k in range(8, arms):
        breathe = 0.55 + 0.45 * math.sin(2 * math.pi * (t * rng.integers(1, 3)) +
                                         rng.uniform(0, 2 * math.pi))
        total += rng.uniform(0.2, 1.0) * breathe / math.sqrt(k) * np.sin(
            k * theta + wind * k * log_r + rng.uniform(0, 2 * math.pi))
    total /= np.abs(total).max()
    return np.clip(0.5 + 0.5 * total, 0, 1)


HOLE_FRAMES = 24  # the black hole's loop: its streaks breathe as it turns (D29)
HOLE_CELL = 384  # ... each frame this many pixels


def black_hole(t=0.0, size=HOLE):
    """A black hole (docs: "Les trous noirs"): a black heart, a thin ring of light hugging it,
    fine bright streaks wound tight around it, close to the ring (D29), and a faint halo out to
    the edge of its pull. At t (0 to 1, a loop), the streaks reach out and draw back, each in
    its own time. It is turned as it plays, so the streaks spin."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    dx, dy = x - size / 2 + 0.5, y - size / 2 + 0.5
    r = np.hypot(dx, dy) / (size / 2)
    theta = np.arctan2(dy, dx)
    streaks = swirl(r, theta, 5, t=t)
    # How far the streaks reach, all round: it wavers along the ring and in time.
    reach = (1 + 0.3 * np.sin(3 * theta + 2 * math.pi * t) +
             0.18 * np.sin(5 * theta - 4 * math.pi * t + 1.0) +
             0.12 * np.sin(8 * theta + 2 * math.pi * t + 2.0))
    core = np.clip((HEART - r) / 0.012, 0, 1)
    photon = np.exp(-((r - HEART - 0.01) / 0.01) ** 2)
    disc = np.exp(-((r - HEART - 0.035) / (0.04 * reach)) ** 2) * (0.2 + 1.0 * streaks ** 3)
    outer = np.exp(-((r - HEART - 0.085) / (0.05 * reach)) ** 2) * streaks ** 5 * 0.45
    halo = np.clip(1 - (r - HEART) / (1 - HEART), 0, 1) ** 2.6 * 0.38
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


def black_holes():
    """The loop of a black hole, HOLE_FRAMES frames (cells of the black_hole atlas)."""
    frames = [black_hole(index / HOLE_FRAMES, HOLE_CELL) for index in range(HOLE_FRAMES)]
    return sheet_of(frames, 6, HOLE_CELL, HOLE_CELL)


# Every black hole drawn plays the loop (sprite2d).
HOLE_LOOP = {"sprite2d.SpriteAnimation": {"first_frame": 0, "frame_count": HOLE_FRAMES,
                                          "ticks_per_frame": 3, "looping": True}}


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


CRYSTAL = 128  # pixels of a carbon crystal


def crystal(seed=3):
    """A cluster of carbon crystals coming out of the ground: white hexagonal prisms, each with a
    lit face, a grey one and a dark edge, glowing a little (the white's food)."""
    rng = np.random.default_rng(seed)
    big = CRYSTAL * SUPER
    picture = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(picture)
    base_y = big * 0.92
    for index in range(5):
        x = big * (0.5 + rng.uniform(-0.26, 0.26))
        height = big * rng.uniform(0.35, 0.78) * (1.0 if index < 3 else 0.6)
        width = big * rng.uniform(0.09, 0.14)
        lean = math.radians(rng.uniform(-24, 24))
        dx, dy = math.sin(lean), -math.cos(lean)
        nx, ny = -dy, dx  # across
        top = (x + dx * height, base_y + dy * height)
        tip = (top[0] + dx * width * 1.3, top[1] + dy * width * 1.3)
        left = [(x - nx * width, base_y - ny * width), (top[0] - nx * width, top[1] - ny * width),
                tip, (x, base_y)]
        right = [(x, base_y), tip, (top[0] + nx * width, top[1] + ny * width),
                 (x + nx * width, base_y + ny * width)]
        draw.polygon(left, fill=(246, 248, 252, 255))
        draw.polygon(right, fill=(176, 182, 196, 255))
        draw.line(left[:3], fill=(255, 255, 255, 255), width=SUPER * 2)
    alpha = np.asarray(picture, dtype=np.float64)[..., 3] / 255.0
    rgb = np.asarray(picture, dtype=np.float64)[..., :3]
    halo = blur(alpha, 10 * SUPER) * 0.7
    out_alpha = alpha + halo * (1 - alpha)
    out_rgb = (rgb * alpha[..., None] + 240 * (halo * (1 - alpha))[..., None]) / np.maximum(
        out_alpha[..., None], 1e-4)
    return shrink(out_rgb, out_alpha, CRYSTAL, CRYSTAL)


def glow():
    """A soft white glow, bright in the middle."""
    size = 512  # drawn big (the air of a planet): smooth even enlarged
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    r = np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) / (size / 2)
    return to_image(np.full((size, size, 3), 255.0), np.clip(1 - r, 0, 1) ** 2.2)


def value_noise(x, y, z, seed, frequency, octaves=6, gain=0.5):
    """Smooth noise read at any points of space (x, y, z, arrays of any shape), the same
    whatever the resolution it is read at: a far picture and a near one of the same thing agree.
    Laid on a sphere, it has no seams nor stretching. 0 to about 1."""
    rng = np.random.default_rng(seed)
    total = np.zeros_like(x)
    amplitude, norm = 1.0, 0.0
    n = 64
    for _ in range(octaves):
        grid = rng.uniform(0, 1, (n, n, n))
        px, py, pz = x * frequency + 100, y * frequency + 100, z * frequency + 100
        i0 = [np.floor(p).astype(int) for p in (px, py, pz)]
        f = [p - np.floor(p) for p in (px, py, pz)]
        f = [t * t * (3 - 2 * t) for t in f]
        a, b, c = (i % n for i in i0)
        a1, b1, c1 = (a + 1) % n, (b + 1) % n, (c + 1) % n
        fx, fy, fz = f
        x00 = grid[a, b, c] * (1 - fx) + grid[a1, b, c] * fx
        x10 = grid[a, b1, c] * (1 - fx) + grid[a1, b1, c] * fx
        x01 = grid[a, b, c1] * (1 - fx) + grid[a1, b, c1] * fx
        x11 = grid[a, b1, c1] * (1 - fx) + grid[a1, b1, c1] * fx
        total += ((x00 * (1 - fy) + x10 * fy) * (1 - fz) + (x01 * (1 - fy) + x11 * fy) * fz) *             amplitude
        norm += amplitude
        amplitude *= gain
        frequency *= 2.03
    return total / norm


PLANET_TURN, PLANET_TILT = 0.0, -0.3  # how the planet is turned: a land at its top
PLANET_LIGHT = np.array([-0.55, -0.6, 0.58]) / np.linalg.norm([-0.55, -0.6, 0.58])


def planet_at(nx, ny, detail=7):
    """The planet of the tutorial seen from space, at the points (nx, ny) of its disc (-1 to 1,
    any resolution): in black and white, seas dark and smooth, lands grey with mountain ridges,
    ice at its poles, swirls of cloud; lit from the upper left, a thin bright edge of air.
    Colours (0 to 255) and opacity (0 to 1)."""
    r = np.hypot(nx, ny)
    nz = np.sqrt(np.clip(1 - r * r, 0, 1))
    # A point of the sphere, the planet a little turned: the noise is laid on it, not on the disc.
    x, y, z = nx * math.cos(PLANET_TURN) + nz * math.sin(PLANET_TURN), ny,         nz * math.cos(PLANET_TURN) - nx * math.sin(PLANET_TURN)
    y, z = y * math.cos(PLANET_TILT) - z * math.sin(PLANET_TILT),         y * math.sin(PLANET_TILT) + z * math.cos(PLANET_TILT)
    wx = value_noise(x, y, z, 301, 1.5, 3) - 0.5
    wy = value_noise(x, y, z, 311, 1.5, 3) - 0.5
    lands = value_noise(x + wx * 0.35, y + wy * 0.35, z, 302, 1.4, detail)
    land = np.clip((lands - 0.49) / 0.012, 0, 1)
    coast = np.exp(-((lands - 0.49) / 0.006) ** 2)
    ridges = 1 - np.abs(value_noise(x, y, z, 303, 4.0, detail) * 2 - 1)
    mountains = np.clip(ridges ** 5 * 2.0 * np.clip((lands - 0.505) * 12, 0, 1), 0, 1)
    plains = value_noise(x, y, z, 304, 7.0, detail)
    ground = 78 + 50 * plains + 115 * mountains
    deep = np.clip((0.49 - lands) * 6, 0, 1)
    sea = 50 - 26 * deep + 8 * value_noise(x, y, z, 305, 5.0, 4)
    value = sea * (1 - land) + ground * land + 30 * coast
    # Relief: the lands lit on their slopes that face the light, shaded on the others.
    height = (lands * 0.5 + mountains * 0.05 + plains * 0.012) * land
    slope_y, slope_x = np.gradient(height, ny[:, 0], nx[0, :])
    relief = np.clip(-(slope_x * PLANET_LIGHT[0] + slope_y * PLANET_LIGHT[1]) * 2.2, -0.6, 0.6)
    value = value * (1 + relief * land)
    # Clouds: long swirls along the latitudes, drawn over all.
    sx = value_noise(x, y, z, 306, 1.2, 3) - 0.5
    cloud = value_noise(x * 1.6 + sx * 0.9, y * 4.0, z * 1.6 + sx * 0.9, 307, 1.3, detail)
    clouds = np.clip((cloud - 0.63) / 0.14, 0, 1) ** 1.4 * 0.85
    value = value * (1 - clouds) + 232 * clouds
    # Light: day, a wide soft edge to the night (the terminator), the night faintly seen (lit by
    # the stars), so that the whole disc stands out from the black of space.
    sun = nx * PLANET_LIGHT[0] + ny * PLANET_LIGHT[1] + nz * PLANET_LIGHT[2]
    light = np.clip((sun + 0.1) / 1.1, 0, 1)
    shine = np.exp(-((nx + 0.38) ** 2 + (ny + 0.42) ** 2) / 0.03) * 0.3 * (1 - land) * (
        1 - clouds)  # the sun in the seas
    value = value * (0.13 + 0.92 * light ** 0.9) + 255 * shine
    # The air: a thin bright line along the whole edge, strong on the side of the sun, and a
    # glow out of it into space, as wide as the air is thick.
    edge_light = np.clip(nx * PLANET_LIGHT[0] + ny * PLANET_LIGHT[1] + 0.45, 0, 1) / 1.45
    rim = np.exp(-((r - 1) / 0.016) ** 2) * (0.3 + 0.7 * edge_light)
    haze = np.exp(-((1 - np.clip(r, 0, 1)) / 0.06)) * (r < 1) * (0.15 + 0.5 * edge_light)
    air = (np.exp(-np.clip(r - 1, 0, None) / 0.05) * (r > 1) * (0.25 + 0.45 * edge_light)
           * np.clip((PLANET_MARGIN - 0.02 - r) / 0.12, 0, 1))
    inside = np.clip((1 - r) * 300, 0, 1)
    alpha = np.clip(inside + rim + air, 0, 1)
    value = np.where(r < 1, value * (1 - haze) + 215 * haze + 110 * rim, 225)
    rgb = np.dstack([value * 0.96, value * 0.99, value * 1.05])
    return rgb, alpha


PLANET_PX = 2048
# The part of the planet seen when it covers the screen, drawn on its own and much finer (its
# disc's coordinates): its top, where he comes down.
PLANET_NEAR = (-0.5, -1.14, 0.5, -0.1)
PLANET_MARGIN = 1.2  # the picture of the whole planet: its disc and as much again round it (air)
PLANET_NEAR_PX = 2048


def planet():
    """The whole planet, PLANET_PX wide."""
    t = ((np.arange(PLANET_PX) + 0.5) / PLANET_PX * 2 - 1) * PLANET_MARGIN
    nx, ny = np.meshgrid(t, t)
    rgb, alpha = planet_at(nx, ny)
    return to_image(rgb, alpha)


def planet_near():
    """The top of the planet, PLANET_NEAR of its disc, finer: seen when it fills the screen."""
    x0, y0, x1, y1 = PLANET_NEAR
    height = round(PLANET_NEAR_PX * (y1 - y0) / (x1 - x0))
    xs = x0 + (np.arange(PLANET_NEAR_PX) + 0.5) / PLANET_NEAR_PX * (x1 - x0)
    ys = y0 + (np.arange(height) + 0.5) / height * (y1 - y0)
    nx, ny = np.meshgrid(xs, ys)
    rgb, alpha = planet_at(nx, ny, detail=9)
    # Its edges fade out: the whole planet, under it, shows the same there (no line).
    u = np.linspace(0, 1, PLANET_NEAR_PX)[None, :]
    v = np.linspace(0, 1, height)[:, None]
    edges = np.clip(np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v)) / 0.04, 0, 1)
    return to_image(rgb, alpha * edges)


BAND_SLOPE = -0.42  # the milky band of the skies rises to the right, as in the docs' pictures


def octave(width, height, rng, scale):
    """One smooth random layer, features about scale pixels apart, -1 to 1."""
    small = rng.uniform(0, 1, (height // scale + 3, width // scale + 3))
    big = np.asarray(Image.fromarray((small * 65535).astype(np.uint16)).convert(
        "I").resize(((width // scale + 3) * scale, (height // scale + 3) * scale),
                    Image.BICUBIC), dtype=np.float64)[:height, :width] / 65535.0
    return np.clip(big, 0, 1) * 2 - 1


def sample(field, xs, ys):
    """The field read between its pixels (bilinear), at float coordinates (clamped)."""
    h, w = field.shape
    xs = np.clip(xs, 0, w - 1.001)
    ys = np.clip(ys, 0, h - 1.001)
    x0, y0 = xs.astype(int), ys.astype(int)
    fx, fy = xs - x0, ys - y0
    top = field[y0, x0] * (1 - fx) + field[y0, x0 + 1] * fx
    bottom = field[y0 + 1, x0] * (1 - fx) + field[y0 + 1, x0 + 1] * fx
    return top * (1 - fy) + bottom * fy


def ridged(width, height, seed, scale, octaves=6, gain=0.55, warp=0.0):
    """Ridged multifractal noise, as Spacescape draws its nebulae: thin bright ridges where the
    noise crosses zero, each finer octave stronger where the coarser ones are bright. warp bends
    it all by a slow field (pixels): filaments instead of a grid of blobs. 0 to about 1."""
    rng = np.random.default_rng(seed)
    pad = int(warp) + 2
    big_w, big_h = width + 2 * pad, height + 2 * pad
    total = np.zeros((big_h, big_w))
    weight = np.ones((big_h, big_w))
    amplitude, norm = 1.0, 0.0
    for index in range(octaves):
        size = max(2, int(scale / (2 ** index)))
        layer = (1 - np.abs(octave(big_w, big_h, rng, size))) ** 2
        layer = layer * weight
        weight = np.clip(layer * 1.6, 0, 1)
        total += layer * amplitude
        norm += amplitude
        amplitude *= gain
    total /= norm
    ys, xs = np.mgrid[0:height, 0:width].astype(np.float64)
    if warp:
        wx = octave(width, height, rng, int(scale * 1.5)) * warp
        wy = octave(width, height, rng, int(scale * 1.5)) * warp
        return sample(total, xs + pad + wx, ys + pad + wy)
    return total[pad:pad + height, pad:pad + width]


def band_of(width, height, thickness=0.28):
    """How much of the milky band each pixel is in, 0 to 1."""
    y = np.linspace(0, 1, height)[:, None]
    x = np.linspace(0, 1, width)[None, :]
    middle = 0.62 + BAND_SLOPE * (x - 0.5)
    return np.exp(-((y - middle) / thickness) ** 2)


def nebula(width, height, seed, palette, density=1.0):
    """Clouds of gas along a band across the sky, as Spacescape makes them: bright ridged
    filaments, a softer glow under them, darker lanes of dust, coloured along a palette (dark to
    bright). Its colours (0 to 255) and how much it covers (0 to 1)."""
    unit = max(width, height) / 1920
    gas = ridged(width, height, seed, int(300 * unit), 8, 0.62, warp=150 * unit)
    fine = ridged(width, height, seed + 3, int(90 * unit), 5, 0.6, warp=40 * unit)
    glow = noise(width, height, seed + 1, ((256, 1.0), (128, 0.5), (64, 0.25)))
    lanes = noise(width, height, seed + 2, ((96, 1.0), (48, 0.5), (24, 0.3), (12, 0.15)))
    band = band_of(width, height, 0.3)
    # Where the gas is: the band, broken into clouds by the slow glow.
    mask = band * np.clip((glow - 0.2) * 2.0, 0, 1) * density
    body = np.clip(gas ** 1.4 * 1.5 * (0.55 + 0.75 * fine), 0, 1) * mask
    haze = blur(mask, 40 * unit) * 0.3
    dust = np.clip((lanes - 0.56) * 4.0, 0, 1)
    cover = np.clip((body + haze) * (1 - 0.75 * dust), 0, 1)
    colours = np.array(palette, dtype=np.float64)
    t = np.clip(body * 0.9 + glow * 0.4 - 0.15, 0, 0.999) * (len(colours) - 1)
    low = np.floor(t).astype(int)
    frac = (t - low)[..., None]
    rgb = colours[low] * (1 - frac) + colours[np.minimum(low + 1, len(colours) - 1)] * frac
    return rgb, cover


def stars(width, height, seed, count, coloured=False, density=None):
    """Stars as Spacescape lays them: a dust of faint points, thicker where the gas is (density,
    0 to 1); a few bright stars with a soft round halo and a faint cross."""
    rng = np.random.default_rng(seed)
    if density is None:
        density = band_of(width, height, 0.3)
    light = np.zeros((height, width, 3))
    xs = rng.uniform(0, width - 1, count * 4)
    ys = rng.uniform(0, height - 1, count * 4)
    keep = rng.uniform(0, 1, xs.size) < 0.2 + 0.8 * density[ys.astype(int), xs.astype(int)]
    xs, ys = xs[keep][:count], ys[keep][:count]
    brightness = rng.uniform(0.0, 1.0, xs.size) ** 4 * 0.9 + 0.12
    palette = np.array([[255, 214, 186], [196, 214, 255], [255, 255, 255], [255, 236, 214],
                        [210, 228, 255]], dtype=np.float64)
    tones = palette[rng.integers(0, 5, xs.size)] if coloured else np.full((xs.size, 3), 255.0)
    # Each point shared between its four pixels: no hard single pixels.
    x0, y0 = xs.astype(int), ys.astype(int)
    fx, fy = xs - x0, ys - y0
    for dx, dy, w in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                      (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        np.add.at(light, (y0 + dy, x0 + dx), tones * (brightness * w)[:, None])
    light /= 255.0
    # The bright ones: a soft halo, wider the brighter.
    bright = max(4, count // 260)
    flares = np.zeros((height, width))
    bx = rng.integers(8, width - 8, bright)
    by = rng.integers(8, height - 8, bright)
    power = rng.uniform(0.4, 1.0, bright) ** 2
    flares[by, bx] = power
    tint = palette[rng.integers(0, 5, bright)] / 255.0 if coloured else np.ones((bright, 3))
    halo = np.zeros((height, width, 3))
    reach = 40
    wy, wx = np.mgrid[-reach:reach + 1, -reach:reach + 1].astype(np.float64)
    r2 = wx * wx + wy * wy
    shape = (2.2 * np.exp(-r2 / (2 * 1.2 ** 2)) + 0.45 * np.exp(-r2 / (2 * 4.0 ** 2)) +
             0.1 * np.exp(-r2 / (2 * 13.0 ** 2)))
    for x, y, p, t in zip(bx, by, power, tint):
        x0, x1 = max(0, x - reach), min(width, x + reach + 1)
        y0, y1 = max(0, y - reach), min(height, y + reach + 1)
        part = shape[y0 - y + reach:y1 - y + reach, x0 - x + reach:x1 - x + reach]
        halo[y0:y1, x0:x1] += (part * p)[..., None] * t
    cross = np.zeros_like(flares)
    for shift in range(1, 10):
        fade = (1 - shift / 10) ** 3 * 0.25
        cross += (np.roll(flares, shift, 1) + np.roll(flares, -shift, 1) +
                  np.roll(flares, shift, 0) + np.roll(flares, -shift, 0)) * fade
    total = light + blur_rgb(light, 0.7) * 0.6 + halo + cross[..., None]
    return np.clip(total, 0, 1)


def blur_rgb(rgb, radius):
    return np.dstack([blur(rgb[..., channel], radius) for channel in range(3)])


def sky(coloured=False, scale=1.0):
    """The sky behind everything, the size of the screen, after Spacescape: black, a band of gas
    in filaments with dark lanes, stars thick in it. In colour for the opening and the title
    (docs: "le ciel"), in greys for the tutorial (docs: the character's picture)."""
    w, h = round(SCREEN_W * scale), round(SCREEN_H * scale)
    if coloured:
        rgb, gas = nebula(w, h, 31, [(8, 30, 48), (14, 70, 90), (30, 130, 140), (60, 150, 210),
                                     (150, 140, 235), (240, 200, 245)], 1.3)
    else:
        rgb, gas = nebula(w, h, 31, [(40, 41, 48), (78, 80, 90), (122, 124, 136),
                                     (170, 172, 184), (220, 222, 232)], 0.85)
    star = stars(w, h, 7, round((16000 if coloured else 13000) * scale * scale), coloured=coloured,
                 density=np.clip(gas * 1.5 + band_of(w, h, 0.3) * 0.3, 0, 1))
    base = np.dstack([np.full((h, w), 3.0), np.full((h, w), 3.5), np.full((h, w), 6.0)])
    picture = base * (1 - gas[..., None]) + rgb * gas[..., None]
    picture = picture + (255 - picture) * star * (1 - gas[..., None] * 0.25)
    return to_image(picture, np.ones((h, w)))


def star_layers():
    """The stars of the opening's sky in four pictures, the brightest first: after the explosion
    the sky is black, then it builds up, a few stars, then more and more (D20). Transparent, drawn
    added."""
    w, h = SCREEN_W, SCREEN_H
    made = []
    for seed, count in ((71, 500), (72, 1600), (73, 4000), (74, 9000)):
        light = stars(w, h, seed, count)
        alpha = np.clip(light.max(axis=2), 0, 1)
        made.append(to_image(light * 255 / np.maximum(alpha[..., None], 1e-4), alpha))
    return made


def gas(coloured=False):
    """The gas of the sky alone, without its stars: in colour first, then grey (the opening)."""
    w, h = SCREEN_W, SCREEN_H
    if coloured:
        rgb, cover = nebula(w, h, 31, [(8, 30, 48), (14, 70, 90), (30, 130, 140), (60, 150, 210),
                                       (150, 140, 235), (240, 200, 245)], 1.3)
    else:
        rgb, cover = nebula(w, h, 31, [(40, 41, 48), (78, 80, 90), (122, 124, 136),
                                       (170, 172, 184), (220, 222, 232)], 0.85)
    return to_image(rgb, cover)


def nebula_veil(coloured=False):
    """A veil of gas that drifts before the sky (a little parallax): more depth."""
    w, h = 1600, 900
    palette = ([(20, 60, 90), (50, 130, 160), (130, 120, 220), (220, 190, 240)] if coloured else
               [(90, 92, 102), (140, 142, 152), (196, 198, 208)])
    rgb, gas = nebula(w, h, 58 if coloured else 59, palette, 0.8 if coloured else 0.55)
    # Its edges fade out: no frame shows, whatever the camera does.
    y = np.linspace(-1, 1, h)[:, None]
    x = np.linspace(-1, 1, w)[None, :]
    edges = np.clip((1 - np.abs(x)) * 4, 0, 1) * np.clip((1 - np.abs(y)) * 4, 0, 1)
    return to_image(rgb, gas * (0.5 if coloured else 0.38) * edges)


BANG_PALETTE = [(255, 255, 255), (170, 215, 255), (110, 160, 255), (175, 120, 255),
                (255, 130, 210), (255, 170, 90), (255, 220, 150)]


def light_picture(light):
    """Light to add (0 to about 1 per channel): its colours at full strength, its strength as the
    opacity (the pictures of the explosion are drawn added)."""
    alpha = np.clip(light.max(axis=2), 0, 1)
    return to_image(np.clip(light, 0, 1) * 255 / np.maximum(alpha, 1e-4)[..., None], alpha)


def polar_to_square(polar, size):
    """A picture drawn in (angle, radius) rows, laid round its middle: a square size wide."""
    angles, radii = polar.shape[:2]
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    dx, dy = x - size / 2 + 0.5, y - size / 2 + 0.5
    r = np.hypot(dx, dy) / (size / 2) * (radii - 1)
    a = (np.arctan2(dy, dx) + math.pi) / (2 * math.pi) * angles
    wrapped = np.concatenate([polar, polar[:1]], axis=0)
    return np.dstack([sample(wrapped[..., channel], r, a) for channel in range(polar.shape[2])])


def bang_rays(size=1024, seed=81):
    """The rays of the explosion (docs: the big bang): a crowd of thin streaks of light out of
    the middle, of every length, white near it, coloured further out."""
    rng = np.random.default_rng(seed)
    angles, radii = 4096, size // 2
    polar = np.zeros((angles, radii, 3))
    rr = np.arange(radii, dtype=np.float64)
    palette = np.array(BANG_PALETTE[1:], dtype=np.float64) / 255.0
    for _ in range(1800):
        a = int(rng.integers(angles))
        length = rng.uniform(0.12, 1.0) ** 0.8 * radii * 0.97
        start = rng.uniform(0.0, 0.3) * length
        width = int(rng.integers(1, 5))
        strength = rng.uniform(0.15, 1.0) ** 2.2 * 0.9
        colour = palette[rng.integers(len(palette))]
        profile = (np.clip((rr - start) / 14, 0, 1) * np.clip((length - rr) / (length * 0.6), 0, 1)
                   ** 1.4)
        rows = np.arange(a, a + width) % angles
        polar[rows] += profile[None, :, None] * strength * colour[None, None, :]
    polar = np.dstack([blur(polar[..., channel], 0.8) for channel in range(3)])
    light = polar_to_square(polar, size)
    r = np.hypot(*(np.mgrid[0:size, 0:size] - size / 2 + 0.5)) / (size / 2)
    # White near the middle; nothing at the edge of the square.
    white = np.clip(1 - r / 0.45, 0, 1) ** 2
    light = light * (1 - white[..., None]) + light.max(axis=2, keepdims=True) * white[..., None]
    light += (np.exp(-(r / 0.05) ** 2) * 1.0 + np.exp(-(r / 0.2) ** 2) * 0.25)[..., None]
    return light_picture(light * np.clip((1 - r) * 6, 0, 1)[..., None])


def bang_core(size=512, seed=82):
    """The heart of the explosion: a white light in rings of colour, not quite round."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    dx, dy = x - size / 2 + 0.5, y - size / 2 + 0.5
    lumps = noise(size, size, seed, ((64, 1.0), (32, 0.5), (16, 0.25)))
    r = np.hypot(dx, dy) / (size / 2) * (0.85 + 0.3 * lumps)
    light = np.zeros((size, size, 3))
    for reach, strength, colour in ((0.07, 1.2, (255, 255, 255)), (0.22, 0.6, (190, 225, 255)),
                                    (0.42, 0.35, (120, 150, 255)), (0.7, 0.22, (190, 120, 255)),
                                    (0.95, 0.12, (255, 160, 120))):
        light += np.exp(-(r / reach) ** 2)[..., None] * strength * np.array(colour) / 255.0
    edge = np.clip((1 - np.hypot(dx, dy) / (size / 2)) * 5, 0, 1)
    return light_picture(light * edge[..., None])


def flares():
    """The reflections of a light too strong (a lens): a long flat streak, a ring, a soft disc.
    Three cells of 512 by 128 (the ring and the disc in the middle of theirs)."""
    w, h = 512, 128
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    u, v = (x - w / 2 + 0.5) / (w / 2), (y - h / 2 + 0.5) / (h / 2)
    along = np.clip(1 - np.abs(u), 0, 1) ** 1.6
    streak = (np.exp(-(v / 0.05) ** 2) * along + np.exp(-(v / 0.3) ** 2) * along ** 3 * 0.35)
    tint = np.array([200, 225, 255]) / 255.0
    made = [light_picture(streak[..., None] * tint)]
    r = np.hypot(u * w / h, v)
    ring = np.zeros((h, w, 3))
    for channel, radius in enumerate((0.86, 0.83, 0.8)):  # a little rainbow on its edge
        ring[..., channel] = np.exp(-((r - radius) / 0.05) ** 2) * 0.55
    ring += (np.clip(1 - r / 0.8, 0, 1) * 0.08)[..., None]
    made.append(light_picture(ring * (r < 1)[..., None]))
    disc = np.clip(1 - r, 0, 1) ** 1.5 * 0.5
    made.append(light_picture(disc[..., None] * np.array([180, 200, 255]) / 255.0))
    return sheet_of(made, 3, w, h)


CLOUD = 512


def clouds():
    """Clouds of gas (docs: the big bang): wisps and filaments round a thicker heart, white, to be
    tinted. Four of them, in cells of CLOUD."""
    made = []
    size = CLOUD
    y, x = np.mgrid[0:size, 0:size].astype(np.float64)
    u, v = (x - size / 2 + 0.5) / (size / 2), (y - size / 2 + 0.5) / (size / 2)
    for seed in range(4):
        rng = np.random.default_rng(90 + seed)
        bend = noise(size, size, 95 + seed, ((128, 1.0), (64, 0.5)))
        stretch = rng.uniform(0.7, 1.3)
        r = np.hypot(u * stretch, v / stretch) * (0.75 + 0.5 * bend)
        heart = np.clip(1 - r, 0, 1) ** 1.3
        wisps = ridged(size, size, 100 + seed, 150, 6, 0.6, warp=60)
        soft = noise(size, size, 110 + seed, ((96, 1.0), (48, 0.5), (24, 0.25)))
        edge = np.clip((1 - np.hypot(u, v)) * 4, 0, 1)
        cover = np.clip((wisps ** 1.5 * 1.5 + soft * 0.35 - 0.15) * heart * 1.8, 0, 1) * edge
        value = 140 + 115 * np.clip(wisps * 1.3, 0, 1)
        made.append(to_image(np.dstack([value, value, value]), cover))
    return sheet_of(made, 4, size, size)


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
    # Their feet melt away, along a ragged line: the bottom of the band never shows as an edge,
    # whatever the camera does.
    ys = np.arange(h)[:, None].astype(np.float64)
    feet = 30 + 80 * ragged(w, 30, 128)[None, :]
    alpha = alpha * np.clip((h - ys - feet) / 70.0, 0, 1)
    return to_image(np.full((h, w, 3), 5.0), alpha)


CAVE_W, CAVE_H = 1024, 1536  # world units of the inside of the ground, behind what is dug
CAVE_TOP = 40  # how far under the ground's surface its picture starts (it fades in, ragged)


def wrap_x(field):
    """A field whose left and right edges meet (endless side by side): blended with itself shifted
    by half its width, across a seam in the middle."""
    w = field.shape[1]
    shifted = np.roll(field, w // 2, axis=1)
    weight = np.abs(np.linspace(-1, 1, w))[None, :]
    return field * (1 - weight) + shifted * weight


def ragged(width, seed, scale=96):
    """A line along x, 0 to 1, that wanders slowly with small bumps on it; its two ends meet (for
    a picture repeated side by side)."""
    line = wrap_x(noise(width, 8, seed, ((scale, 1.0), (max(2, scale // 4), 0.35),
                                         (max(2, scale // 12), 0.12))))[0]
    low, high = line.min(), line.max()
    return (line - low) / max(high - low, 1e-6)


def cave_pictures():
    """What is seen in what is dug (docs: "il y a du carbone dans la planète"), one pixel a unit:
    the near rock, in strata, with roots near the surface, cracks, pockets of matter ore (dark,
    with bluish glints) and of carbon crystals (white), fossils; caverns open in it deeper down.
    Returns the rock (lit by the scene's lights) and the glints of its crystals (which glow)."""
    w, h = CAVE_W, CAVE_H
    rng = np.random.default_rng(17)
    y = np.arange(h)[:, None].astype(np.float64)
    soft = wrap_x(noise(w, h, 17, ((128, 1.0), (64, 0.6), (32, 0.35), (16, 0.2), (8, 0.12))))
    warp = wrap_x(noise(w, h, 18, ((256, 1.0), (128, 0.5))))
    strata = np.sin(y / 34.0 + warp * 9.0) * 0.5 + 0.5
    cracks = wrap_x(ridged(w, h, 19, 160, 5, 0.55, warp=30))
    value = 26 + 22 * soft + 9 * strata - 14 * np.clip((cracks - 0.62) * 4, 0, 1)
    # Near the surface: soil, lighter and finer, with roots hanging into it. Its top is ragged
    # and soft (the soil of the ground's back wall shows through it): never a straight line.
    top_line = 34 + 60 * ragged(w, 27, 160)
    fuzz = wrap_x(noise(w, h, 28, ((24, 1.0), (8, 0.5))))
    top_alpha = np.clip((y - top_line[None, :] + 40 + 24 * (fuzz - 0.5)) / 40.0, 0, 1)
    soil = np.clip(1 - (y - top_line[None, :]) / 140.0, 0, 1)
    value = value + 10 * soil
    # The roots, drawn bigger then reduced: smooth, thinner and thinner towards their tips.
    picture = Image.new("L", (w * SUPER, h * SUPER), 0)
    draw = ImageDraw.Draw(picture)
    for _ in range(90):
        x = rng.uniform(0, w)
        length = rng.uniform(40, 190)
        px, py = x, top_line[int(x) % w] - rng.uniform(14, 40)
        steps = int(length / 6)
        for step in range(steps):
            nx_ = px + rng.uniform(-4, 4)
            ny_ = py + 6
            width = max(1, int(round(SUPER * (2.6 - 2.0 * step / steps))))
            draw.line([(px * SUPER, py * SUPER), (nx_ * SUPER, ny_ * SUPER)], fill=255,
                      width=width, joint="curve")
            px, py = nx_, ny_
    picture = picture.resize((w, h), Image.LANCZOS)
    roots = np.clip(np.asarray(picture, dtype=np.float64) / 255.0, 0, 1)
    value = value * (1 - 0.6 * roots)
    rgb = np.dstack([value, value * 1.0, value * 1.07])
    glints = np.zeros((h, w))
    # Pockets of matter ore: dark stones, a cold glint on each.
    for _ in range(46):
        cx, cy = rng.uniform(20, w - 20), rng.uniform(120, h - 20)
        r = rng.uniform(5, 13)
        mask = np.clip((r - np.hypot(np.arange(w)[None, :] - cx, y - cy)) / 1.5, 0, 1)
        rgb = rgb * (1 - mask[..., None]) + np.array([16, 18, 26]) * mask[..., None]
        spot = np.clip((r * 0.35 - np.hypot(np.arange(w)[None, :] - cx + r * 0.3,
                                            y - cy + r * 0.3)) / 1.0, 0, 1)
        rgb = rgb + np.array([60, 80, 120]) * spot[..., None]
    # Carbon: clusters of small white crystals, that glow.
    for _ in range(38):
        cx, cy = rng.uniform(20, w - 20), rng.uniform(80, h - 20)
        for _ in range(rng.integers(2, 6)):
            ox, oy = rng.uniform(-8, 8), rng.uniform(-5, 5)
            length, width = rng.uniform(5, 12), rng.uniform(1.5, 3)
            a = rng.uniform(-0.9, 0.9) - math.pi / 2
            u = (np.arange(w)[None, :] - cx - ox) * math.cos(a) + (y - cy - oy) * math.sin(a)
            v = -(np.arange(w)[None, :] - cx - ox) * math.sin(a) + (y - cy - oy) * math.cos(a)
            mask = np.clip(1 - np.maximum(np.abs(u - length / 2) / (length / 2),
                                          np.abs(v) / width), 0, 1) ** 0.3
            rgb = rgb * (1 - mask[..., None]) + np.array([232, 236, 244]) * mask[..., None]
            glints = np.maximum(glints, mask)
    # Fossils: pale spirals.
    for _ in range(14):
        cx, cy = rng.uniform(30, w - 30), rng.uniform(150, h - 30)
        r = rng.uniform(7, 14)
        dx, dy = np.arange(w)[None, :] - cx, y - cy
        dist = np.hypot(dx, dy)
        angle = np.arctan2(dy, dx)
        spiral = np.abs(((dist / r * 4 - angle / math.pi) % 2) - 1)
        mask = np.clip((r - dist) / 1.5, 0, 1)
        shade = 70 + 60 * spiral
        rgb = rgb * (1 - mask[..., None]) + np.dstack([shade, shade, shade * 0.96]) * mask[..., None]
    # Caverns, deeper down: open (the far caves show through), their rims darker.
    voids = wrap_x(noise(w, h, 23, ((192, 1.0), (96, 0.5), (48, 0.25))))
    depth_in = np.clip((y - 420) / 200.0, 0, 1)
    open_ = np.clip((voids - 0.6) * 9, 0, 1) * depth_in
    rim = np.clip(blur(open_, 6) * 2.2 - open_, 0, 1)
    rgb = rgb * (1 - 0.55 * rim[..., None])
    alpha = np.maximum(top_alpha, roots * 0.9) * (1 - open_)
    rock = to_image(np.clip(rgb, 0, 255), alpha)
    glints = glints * top_alpha
    glow = blur(glints, 3) * 1.6 + glints
    glints_picture = to_image(np.dstack([np.full((h, w), 230.0), np.full((h, w), 238.0),
                                         np.full((h, w), 255.0)]), np.clip(glow, 0, 1) * 0.9)
    return rock, glints_picture


CAVE_FAR_W, CAVE_FAR_H = 1024, 2048


def cave_far():
    """The far caves seen through the caverns: dark walls, stalactites, a few glowing seams far
    away; endless side by side. Half a pixel a unit: it is far and soft."""
    w, h = CAVE_FAR_W // 2, CAVE_FAR_H // 2
    rng = np.random.default_rng(31)
    y = np.arange(h)[:, None].astype(np.float64)
    field = wrap_x(noise(w, h, 31, ((96, 1.0), (48, 0.5), (24, 0.3), (12, 0.15))))
    walls = wrap_x(ridged(w, h, 32, 120, 4, 0.5, warp=20))
    value = 10 + 12 * field + 10 * np.clip((walls - 0.5) * 2, 0, 1)
    picture = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(picture)
    for _ in range(120):
        x, top = rng.uniform(0, w), rng.uniform(0, h)
        length, width = rng.uniform(10, 45), rng.uniform(3, 8)
        draw.polygon([(x - width, top), (x + width, top), (x, top + length)], fill=255)
    hanging = blur(np.asarray(picture, dtype=np.float64) / 255.0, 1.0)
    value = value * (1 - 0.5 * hanging)
    seams = np.zeros((h, w))
    for _ in range(26):
        cx, cy = rng.integers(0, w), rng.integers(0, h)
        seams[cy, cx] = rng.uniform(0.5, 1.0)
    seams = blur(seams, 4) * 60 + blur(seams, 1.2) * 6
    value = value + 140 * np.clip(seams, 0, 1)
    rgb = np.dstack([value, value * 1.02, value * 1.1])
    # Its top and bottom fade along ragged lines: no edge shows through the caverns.
    top = h * (0.03 + 0.1 * ragged(w, 33, 64))[None, :]
    bottom = h * (0.03 + 0.1 * ragged(w, 34, 64))[None, :]
    alpha = (np.clip((y - top) / (h * 0.06), 0, 1) *
             np.clip((h - 1 - y - bottom) / (h * 0.06), 0, 1))
    return to_image(rgb, alpha).resize((CAVE_FAR_W, CAVE_FAR_H), Image.BICUBIC)


ORE = 96  # pixels of a buried stone of matter


def ore():
    """A stone of matter in the ground: dark, angular, with cold glints along its veins."""
    rng = np.random.default_rng(41)
    big = ORE * SUPER
    picture = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(picture)
    points = []
    for index in range(9):
        a = 2 * math.pi * index / 9 + rng.uniform(-0.2, 0.2)
        r = big * rng.uniform(0.32, 0.45)
        points.append((big / 2 + math.cos(a) * r, big / 2 + math.sin(a) * r * 0.8))
    draw.polygon(points, fill=(26, 28, 36, 255))
    for _ in range(5):
        a = rng.uniform(0, math.pi)
        x0, y0 = big / 2 + rng.uniform(-big * 0.2, big * 0.2), big / 2 + rng.uniform(-big * 0.15, big * 0.15)
        draw.line([(x0 - math.cos(a) * big * 0.18, y0 - math.sin(a) * big * 0.12),
                   (x0 + math.cos(a) * big * 0.18, y0 + math.sin(a) * big * 0.12)],
                  fill=(110, 140, 200, 255), width=SUPER * 2)
    draw.polygon(points[:4] + [(big / 2, big / 2)], fill=(44, 47, 60, 255))
    alpha = np.asarray(picture, dtype=np.float64)[..., 3] / 255.0
    rgb = np.asarray(picture, dtype=np.float64)[..., :3]
    return shrink(rgb, alpha, ORE, ORE)


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


def curve(name, onto, field, keys, start=None):
    """A track that goes through its keys along one smooth curve (tween2d curve): one motion,
    slowing down only at its ends."""
    made = track(name, onto, field, keys, start)
    made["components"]["tween2d.Track"]["curve"] = True
    return made


def spin(name, onto, ticks):
    """Turns the tagged entity round and round, once every `ticks`."""
    return track(name, onto, "transform2d.Rotation.angle", [(0, 0), (ticks, 360)], smooth=False,
                 loop=True)


def hero_entity(x, y, locked=False, falling=0):
    hero = entity("hero", x, y, {
        "sprite2d.Sprite": {"atlas": "hero", "width": CELL, "height": CELL,
                            "layer": LAYERS["hero"], "unlit": True},
        # Each passage between two animations plays to its end (D29).
        "sprite2d.Animator": {"set": "hero", "finish": True},
        # His white half lights what is around him (Ori's glow in Limbo's dark).
        "light2d.Light": {"radius": 340, "red": 232, "green": 236, "blue": 246,
                          "intensity": 0.95, "offset_y": -24, "flicker": 0.05,
                          "flicker_speed": 2},
        "tag.Tag": {"tag": "hero"},
        "transform2d.Velocity": {"y": falling},
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
        "jump.Jumper": {"speed": JUMP_SPEED, "charge_time": HERO_CHARGE_TIME, "charge_low": 0.55,
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
        hero["components"]["action_control.Controlled"]["action_x"] = "demo_walk"
        hero["components"]["jump.Jumper"]["action"] = "demo_jump"
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
        # Going up as soon as he is thrown up; coming down as soon as he has been off the ground
        # a moment (never walking in the air, even at the top of a jump).
        "rise": (40, "transform2d.Velocity.y", None, -60),
        "fall": (39, "jump.Jumper.since_ground", 0.1, None),
        "rise_right": (42, "transform2d.Velocity.y", None, -60),
        "rise_left": (42, "transform2d.Velocity.y", None, -60),
        "fall_right": (41, "jump.Jumper.since_ground", 0.1, None),
        "fall_left": (41, "jump.Jumper.since_ground", 0.1, None),
        "charge_right": (51, "jump.Jumper.charge", 0.02, None),
        "charge_left": (51, "jump.Jumper.charge", 0.02, None),
    }
    # A jump to one side: going that way too (a second condition).
    sideways = {"rise_right": (60, None), "fall_right": (60, None),
                "rise_left": (None, -60), "fall_left": (None, -60),
                "charge_right": (60, None), "charge_left": (None, -60)}
    ids = {name: index + 1 for index, (name, _, _) in enumerate(HERO_CLIPS)}
    made = []
    first = 0
    for name, poses, ticks in HERO_CLIPS:
        priority, field, low, high = conditions[name]
        clip = {"set": "hero", "priority": priority, "first_frame": first,
                "frame_count": len(poses), "ticks_per_frame": ticks,
                "looping": not name.startswith("charge")}
        if field:
            clip["field"] = field
        if low is not None:
            clip["min"] = low
        if high is not None:
            clip["max"] = high
        if name in sideways:
            clip["and_field"] = "transform2d.Velocity.x"
            low, high = sideways[name]
            if low is not None:
                clip["and_min"] = low
            if high is not None:
                clip["and_max"] = high
        clip["id"] = ids[name]
        made.append({"name": f"clip_{name}", "components": {"sprite2d.AnimationClip": clip}})
        first += len(poses)
    # The passages from one to the other (sprite2d transitions): played once, between.
    frames_of = {}
    for name, poses, ticks, source, target in HERO_TRANSITIONS:
        if isinstance(poses, str):
            start, count = frames_of[poses]
        else:
            start, count = first, len(poses)
            frames_of[name] = (start, count)
            first += count
        clip = {"set": "hero", "first_frame": start, "frame_count": count,
                "ticks_per_frame": ticks, "looping": False, "from_id": ids[source]}
        if target is not None:
            clip["to_id"] = ids[target]
        made.append({"name": f"clip_{name}", "components": {"sprite2d.AnimationClip": clip}})
    # In space: floating when he hardly moves, else flying (the frames of the fall).
    fall_first, fall_count = hero_frames("fall")
    made += [
        {"name": "clip_space_fly", "components": {"sprite2d.AnimationClip": {
            "set": "hero_space", "first_frame": fall_first, "frame_count": fall_count,
            "ticks_per_frame": 1, "looping": True, "id": SPACE_IDS["fly"]}}},
        {"name": "clip_space_float", "components": {"sprite2d.AnimationClip": {
            "set": "hero_space", "priority": 10, "first_frame": first,
            "frame_count": len(SPACE_FLOAT), "ticks_per_frame": 1, "looping": True,
            "field": "transform2d.Velocity.x", "absolute": True, "max": SPACE_STILL,
            "and_field": "transform2d.Velocity.y", "and_absolute": True,
            "and_max": SPACE_STILL, "id": SPACE_IDS["float"]}}},
    ]
    first += len(SPACE_FLOAT)
    for name, poses, ticks, (source_set, source), target in SPACE_TRANSITIONS:
        sources = [SPACE_IDS[source]] if source_set == "hero_space" else [
            ids[clip] for clip in (source, f"{source}_right", f"{source}_left")]
        for index, source_id in enumerate(sources):
            made.append({"name": f"clip_{name}_{index}", "components": {
                "sprite2d.AnimationClip": {
                    "set": "hero_space", "first_frame": first, "frame_count": len(poses),
                    "ticks_per_frame": ticks, "looping": False, "from_id": source_id,
                    "to_id": SPACE_IDS[target]}}})
        first += len(poses)
    return made


JUMP_HOLE_Y = -78  # the jump's black hole, over his head
JUMP_HOLE_SIZE = 24  # its scale (and radius) at a full charge
HOLE_GROWTH = 12  # held full five seconds, the hole's radius then grows this much a second
JUMP_SPEED = 560  # the jump's speed (times 0.55 to 1.8 with the charge)
LAUNCH_SPEED = 1900  # ... up to this, the hole growing (three seconds): out of the atmosphere
LAUNCH_GROWTH = 3.0  # the seconds of growth to the strongest jump


def jump_hole():
    """The small black hole above the hero's head while he charges a jump: its size follows the
    charge, and it costs matter while it is there."""
    return [
        entity("jump_hole", 0, 0, {
            "tag.Tag": {"tag": "jump_hole"},
            "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                                "layer": LAYERS["hero"] + 5, "unlit": True},
            "transform2d.Scale": {"factor": 0},
            **HOLE_LOOP,
            "transform2d.Rotation": {},
            "follow2d.Follow": {"target": "hero", "offset_y": JUMP_HOLE_Y, "stiffness": 1},
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
             0, 1, 0, JUMP_HOLE_SIZE, 1, onto="jump_hole"),
        spin("jump_hole_turns", "jump_hole", 150),
        # The departure (D21, D22): held full for five seconds, the same hole goes on growing,
        # without end, its middle going up so that it never covers him (it is drawn to him, it
        # cannot be on him); the bigger it is, the faster he is thrown, up to a limit.
        bind("jump_hole_swells", "hold", "transform2d.Position.x", "sprite2d.Sprite.width",
             DEPARTURE_HOLD, DEPARTURE_HOLD + 1000, 2, 2 + 2000 * HOLE_GROWTH / JUMP_HOLE_SIZE,
             1, onto="jump_hole"),
        bind("jump_hole_swells_high", "hold", "transform2d.Position.x", "sprite2d.Sprite.height",
             DEPARTURE_HOLD, DEPARTURE_HOLD + 1000, 2, 2 + 2000 * HOLE_GROWTH / JUMP_HOLE_SIZE,
             1, onto="jump_hole"),
        bind("jump_hole_rises", "hold", "transform2d.Position.x", "follow2d.Follow.offset_y",
             DEPARTURE_HOLD, DEPARTURE_HOLD + 1000, JUMP_HOLE_Y,
             JUMP_HOLE_Y - 1000 * HOLE_GROWTH, 1, onto="jump_hole"),
        bind("jump_strengthens", "hold", "transform2d.Position.x", "jump.Jumper.speed",
             DEPARTURE_HOLD, DEPARTURE_HOLD + LAUNCH_GROWTH, JUMP_SPEED, LAUNCH_SPEED, 1,
             onto="hero"),
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
        # No combination is made by the two hands (the harvest is its key, D10): no waiting to see
        # whether the other hand comes too, a power starts at once (and a wall starts where the
        # button was pressed, however fast it is drawn).
        kit["loadout.Kit"].update({"unlocked": [], "change": "locked", "together": 0})
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
            "follow2d.Follow": {"target": "hero", "offset_x": side * 18, "offset_y": 11,
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
        **HOLE_LOOP,
        "transform2d.Rotation": {},
        # Its ring lights the ground; dust spirals into it.
        "light2d.Light": {"radius": 0, "red": 190, "green": 210, "blue": 255, "intensity": 0},
        "particles2d.Emitter": dict(VORTEX_EMITTER, pull=1800, swirl=520),
    })
    # The white (GDD, Blanc): pressed, a bubble grows where the pointer was; dragged further than
    # wall_drag, it becomes a wall to the pointer. Let go, the barrier is raised and paid in carbon
    # for its size; one at a time (a new one takes its place, D29).
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
    # One barrier at a time (D29): a new one takes the place of the last.
    walls = [entity("barrier_1", 0, 0, {"barrier2d.Barrier": dict({
        "pool": 0, "layer": ["barrier"], "freezes": ["rock"], "atlas": "barrier", "bubble": 0,
        "wall": 1, "draw_layer": LAYERS["hero"] + 4}, **BARRIER_LOOK)})]
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


# How a barrier shows (D29): it springs up, glass glints fly off it, then it breathes a little.
BARRIER_LOOK = {"appear": 0.35, "wobble": 0.025, "sparks": 16, "spark_speed": 70,
                "spark_life": 0.7, "spark_size": 5, "spark_color": 0xD2FAF5E6}

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
                "layer": LAYERS["hero"] - 1, "area_width": 50, "area_height": 30,
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


def deposit(name, x, ground_y, count=3, amount=12):
    """A deposit of carbon for the white: crystals half in the ground, that do not move; the
    harvest (or a black hole) swallows them one by one, each worth amount of carbon. The first
    one glows into the dark, to be seen from afar."""
    made = []
    for index in range(count):
        offset = (index - (count - 1) / 2) * 22
        size = 34 if index != count // 2 else 44
        crystal_entity = entity(f"{name}_{index}", x + offset, ground_y - size * 0.32, {
            "sprite2d.Sprite": {"atlas": "crystal", "width": size, "height": size,
                                "layer": LAYERS["hero"] - 3, "unlit": True},
            "collision2d.Collider": {"width": 20, "height": 20, "layer": ["rock"],
                                     "mask": []},
            "attractor2d.Absorbable": {"mass": 10},
            "reserve.Yield": {"kind": "carbon", "amount": amount},
        })
        if index == count // 2:
            crystal_entity["components"]["light2d.Light"] = {
                "radius": 120, "red": 226, "green": 234, "blue": 246, "intensity": 0.6,
                "flicker": 0.15, "flicker_speed": 1.2}
        made.append(crystal_entity)
    return made


def buried(ground_y, width):
    """Resources in the ground: a stone of matter or a crystal of carbon every few hundred units,
    at depths that change; none in the hard rock of the landing."""
    rng = np.random.default_rng(5)
    made = []
    for index, x in enumerate(range(380, int(width) - 200, 230)):
        depth = rng.uniform(60, 1100)
        if LANDING_ROCK[0] - 40 < x < LANDING_ROCK[1] + 40 and depth < 420:
            continue
        y = ground_y + depth
        if index % 2 == 0:
            made.append(entity(f"ore_{index}", x, y, {
                "sprite2d.Sprite": {"atlas": "ore", "width": 30, "height": 30,
                                    "layer": LAYERS["hero"] - 3},
                "collision2d.Collider": {"width": 20, "height": 20, "layer": ["rock"],
                                         "mask": []},
                "attractor2d.Absorbable": {"mass": 10},
                "reserve.Yield": {"kind": "matter", "amount": 14}}))
        else:
            made += deposit(f"buried_{index}", x, y + 12, count=2, amount=10)
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


def camera(bound_right, bound_bottom, y=None, stiffness=0.06):
    return entity("camera", VIEW_W / 2, VIEW_H / 2 if y is None else y, {
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
        "camera2d.CameraFollow": {"stiffness": stiffness, "dead_x": 30, "dead_y": 60,
                                  "offset_y": -40, "look_ahead": 0.25,
                                  "bounded": True, "bound_left": 0, "bound_right": bound_right,
                                  "bound_top": -2000, "bound_bottom": bound_bottom},
    })


def sky_entity(coloured=False):
    return [
        entity("sky", 0, 0, {
            "tag.Tag": {"tag": "sky"},
            "sprite2d.Sprite": {"atlas": "sky_colour" if coloured else "sky", "width": VIEW_W,
                                "height": VIEW_H, "layer": LAYERS["sky"], "unlit": True},
            "sprite2d.Parallax": {"x": 0, "y": 0}, "transform2d.Scale": {"factor": 1}}),
        # The camera going back (the voyage), the sky grows with it: it always covers the screen.
        bind("sky_covers", "camera", "camera2d.Camera.zoom", "transform2d.Scale.factor",
             0.9, ZOOM, ZOOM / 0.9, 1.0, 1, onto="sky"),
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


# The far planes that get a skirt: (name, atlas, size, foot from the ground, parallax, layer).
SKIRTED = [
    ("mountains", "mountains", None, lambda g: g + 30, 0.12, LAYERS["far"] - 20),
    ("hills_far", "hills_far", None, lambda g: g + 40, 0.25, LAYERS["far"]),
    ("hills_near", "hills_near", None, lambda g: g + 40, 0.55, LAYERS["near"]),
    ("ground_far", "ground_far", None, lambda g: g + 110, 0.88, LAYERS["ground"] - 8),
]


def foot_colour(atlas):
    """The colour of the bottom rows of a picture of the art (what its skirt continues)."""
    picture = np.asarray(Image.open(os.path.join(ART, atlas + ".png")).convert("RGBA"),
                         dtype=np.float64)[-6:]
    alpha = picture[..., 3:4] / 255.0
    colour = (picture[..., :3] * alpha).sum(axis=(0, 1)) / max(alpha.sum(), 1e-6)
    return [int(round(c)) for c in colour]


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
    made[-1]["components"]["tag.Tag"] = {"tag": "front"}
    for name, atlas, parallax, layer, alpha in (("hills_far", "hills_far", 0.25, LAYERS["far"],
                                                 255),
                                                ("hills_near", "hills_near", 0.55,
                                                 LAYERS["near"], 255)):
        made.append(layer_entity(name, atlas, (HILLS_W, HILLS_H), ground_y + 40, parallax,
                                 layer, camera_y, alpha=alpha))
    # The inside of the planet, behind the ground: what is seen in what is dug. Near, the rock
    # (lit: the hero's light shows it), its crystals glowing on their own; through its caverns,
    # far caves that move less (depth). They go deep enough for the deepest dig.
    for index in range(2):
        top = ground_y + CAVE_TOP + index * CAVE_H
        made.append(entity(f"inside_{index}", 0, top + CAVE_H / 2, {
            "sprite2d.Sprite": {"atlas": "cave", "width": CAVE_W, "height": CAVE_H,
                                "layer": LAYERS["ground"] - 5},
            "sprite2d.Parallax": {"x": 1, "y": 1, "repeat_x": True}}))
        made.append(entity(f"inside_glints_{index}", 0, top + CAVE_H / 2, {
            "sprite2d.Sprite": {"atlas": "cave_glints", "width": CAVE_W, "height": CAVE_H,
                                "layer": LAYERS["ground"] - 4, "additive": True,
                                "unlit": True, "alpha": 200},
            "sprite2d.Parallax": {"x": 1, "y": 1, "repeat_x": True}}))
    made.append(layer_entity("inside_far", "cave_far", (CAVE_FAR_W, CAVE_FAR_H),
                             ground_y + 200 + CAVE_FAR_H, 0.8, LAYERS["ground"] - 7, camera_y,
                             lit=True))
    # Under every far plane, the colour of its foot all the way down: when the camera rises (a
    # jump, the flight to orbit), no edge of a band shows above the ground.
    for name, atlas, size, foot, parallax, layer in SKIRTED:
        colour = foot_colour(atlas)
        y = foot(ground_y) - camera_y * (1 - parallax) - 2
        made.append(entity(f"{name}_skirt", 0, y + 3000, {
            "sprite2d.Sprite": {"atlas": "plain", "width": 200000, "height": 6000,
                                "layer": layer, "unlit": True, "red": colour[0],
                                "green": colour[1], "blue": colour[2]},
            "sprite2d.Parallax": {"x": parallax, "y": parallax}}))
    # The grass in front fades as the camera goes down into the ground (its foot would show).
    made.append(bind("front_fades", "camera", "transform2d.Position.y", "sprite2d.Sprite.alpha",
                     camera_y + 40, camera_y + 220, 255, 0, 1, onto="front"))
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
        "tag.Tag": {"tag": "ash"},
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
                              "surface": (surface_y - top) / TERRAIN_CELL,
                              # Dug, no crumb left that its drawing (smoothed over 2 cells)
                              # would not show: nothing unseen stops him.
                              "crumble": 2,
                              # Walking down a slope or a step, he stays on the ground.
                              "stick": True},
        "terrain2d.Look": {"layer": LAYERS["ground"], "pixels": 6, "red": 12, "green": 12,
                           "blue": 14, "top_red": 176, "top_green": 180, "top_blue": 190,
                           "lip": 10, "grain": 0.08, "smooth": 2},
    }
    if extra:
        components["terrain2d.Terrain"].update(extra)
    return entity(name, left, top, components)


# The relief of the tutorial's ground (D20): over the small bumps of its noise, gentle hills
# (height above 0) and hollows (below), each a big disc's arc: (x, height, half width).
RELIEF = [(100, 30, 100), (1300, -30, 260), (5300, 55, 380), (5800, -30, 220),
          (6280, 60, 250)]
TERRAIN_RELIEF = {"amplitude": 2.0, "wavelength": 40, "seed": 3}  # +/- 16 units of bumps


def relief_shapes(ground_y):
    """The hills and hollows of RELIEF: a disc filled under a hill, carved over a hollow."""
    made = []
    for index, (x, height, half) in enumerate(RELIEF):
        depth = (half * half - height * height) / (2 * abs(height))
        radius = depth + abs(height)
        centre = ground_y + depth if height > 0 else ground_y - depth
        made.append(entity(f"relief_{index}", x, centre, {"terrain2d.Shape": {
            "radius": radius, "fill": height > 0}}))
    return made


def surface_at(x, ground_y=None):
    """The top of the ground at x, from RELIEF (the small bumps of the noise left aside)."""
    ground_y = GROUND_Y if ground_y is None else ground_y
    for centre_x, height, half in RELIEF:
        if abs(x - centre_x) < half:
            depth = (half * half - height * height) / (2 * abs(height))
            radius = depth + abs(height)
            rise = math.sqrt(radius * radius - (x - centre_x) ** 2) - depth
            return ground_y - rise if height > 0 else ground_y + rise
    return ground_y


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
    """The pause (D6, D25): Escape shuts him in a bubble, everything stops (toggle.Toggle.pauses),
    all around him goes soft (postfx paused_blur); he and his bubble stay sharp (drawn over the
    effects). The two buttons, nothing more."""
    shown = {"toggle.ShownBy": {"toggle": "pause"}, "transform2d.Hidden": {}}
    return [
        entity("pause", 0, 0, {"tag.Tag": {"tag": "pause"}, "toggle.Toggle": {
            "action": "pause", "off": "resume", "pauses": True}}),
        # Him as he is, sharp over the blur, and his bubble.
        entity("pause_hero", 0, 0, {"tag.Tag": {"tag": "pause_hero"}, "sprite2d.Sprite": {
            "atlas": "hero", "width": CELL, "height": CELL, "layer": LAYERS["hero"] + 1,
            "unlit": True, "over_effects": True},
            "follow2d.Follow": {"target": "hero", "stiffness": 1}, **shown}),
        bind("pause_pose", "hero", "sprite2d.Sprite.frame", "sprite2d.Sprite.frame", 0, 100000,
             0, 100000, 1, onto="pause_hero"),
        entity("pause_bubble", 0, 0, {"sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 200, "height": 200,
            "layer": LAYERS["hero"] + 2, "unlit": True, "over_effects": True},
            "follow2d.Follow": {"target": "hero", "stiffness": 1}, **shown}),
        entity("to_title", 0, 0, {"scene_switch.GoToScene": {
            "scene": "title", "use_action": True, "action": "to_title"}}),
    ] + menu_buttons("pause", [("menu.resume", "resume"), ("menu.leave", "to_title")],
                     SCREEN_H - 220, shown_by="pause")


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
            "atlas": "hero_curled", "width": 104, "height": 104, "layer": 30, "unlit": True},
            "transform2d.Rotation": {}}),
        entity("sleeper_bubble", VIEW_W / 2, 318, {"sprite2d.Sprite": {
            "atlas": "barrier", "frame": 0, "width": 118, "height": 118, "layer": 31,
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
# The tutorial (docs/complete_universe/TUTORIEL.md): one planet, played flat.

TUTORIAL_W = 6600  # the planet's ground runs from x 0 to here
TUTORIAL_TOP = -1000  # the ground's grid starts this high (walls at both ends go up to it)
TUTORIAL_DEPTH = 1400  # how deep the ground goes under its surface: it can be dug into
LANDING_X = 600
LANDING_ROCK = (300, 900)  # the hard rock under the landing: the hero cannot dig a trap there
WALK_TO = 1600  # walking ends here
GAP = (1800, 1900)  # jumping: a pit to jump over...
LEDGE = (2100, 2600, 110)  # ...then a ledge this high
PILLAR = (2800, 70)  # x and width of a pillar up to the sky: only digging gets through
CHASM = (3000, 3420, 220)  # a chasm too wide to jump: a wall of white is the bridge
BASIN = (3700, 5000, 80)  # the basin with fossils, then rocks

# The steps of the tutorial, in order. A step "x_practice" follows the step x once it is done: a
# few seconds to try it again and see what it does, before the next one.
STEPS = ["landing", "black_show", "black", "black_practice", "white_show", "white",
         "white_practice", "harvest_show", "harvest", "harvest_practice", "inventory",
         "inventory_practice", "walk_show", "walk", "jump_show", "jump", "jump_practice",
         "obstacles", "lack_white", "lack_black", "death", "after_death", "departure", "end",
         "orbit"]
# A step "x_show" comes before the step x (D19): the game does it by itself, seen close, and says
# how it works; then the player does it, told which key. Its seconds:
SHOW_SECONDS = {"black": 6, "white": 6, "harvest": 6, "walk": 4, "jump": 4}

# The texts of each step (GDD §12: text is only for the controls): a title and a line.
TEXTS = {
    "black_show": ("Le noir",
                   "Ta main droite crée un trou noir : il attire, puis avale ce qui l'entoure."),
    "black": ("À toi : le noir", "Maintiens le clic droit sur les pierres. Avale-en deux."),
    "white_show": ("Le blanc",
                   "Ta main gauche lève une barrière : une bulle, ou un mur d'un point à un autre."),
    "white": ("À toi : le blanc",
              "Clic gauche : une bulle. Clic gauche glissé : un mur. Fais-en deux."),
    "harvest_show": ("La récolte",
                     "Une bulle de récolte avale ce qu'elle touche, sol compris, et le range en toi."),
    "harvest": ("À toi : la récolte", "Maintiens F sur un fossile."),
    "inventory": ("Tes réserves",
                  "Tab : la matière (noire) et le carbone (blanc) que tu portes."),
    "walk_show": ("Marcher", "À chaque pas, un petit trou noir s'ouvre devant toi et tire ta jambe."),
    "walk": ("À toi : marcher", "Q / D ou ← → : avance jusqu'au bout du chemin."),
    "jump_show": ("Sauter",
                  "Un trou noir grandit au-dessus de ta tête tant que tu charges, puis te projette."),
    "jump": ("À toi : sauter",
             "Maintiens Espace puis relâche : saute le fossé, puis monte sur le rebord."),
    "obstacles": ("Les deux mains",
                  "Creuse le pilier avec le noir, puis jette un mur de blanc sur le gouffre."),
    "lack_white": ("Le carbone s'épuise",
                   "F sur les fossiles, ou dans le sol : il contient aussi du carbone."),
    "lack_black": ("La matière s'épuise",
                   "F sur les pierres, ou creuse le sol : il est fait de matière."),
    "death": ("Sans matière, ton corps se défait",
              "Tout devient blanc…"),
    "after_death": ("Sans carbone, ce serait le noir",
                    "Garde toujours les deux : tu reprends au début de l'étape."),
    "departure": ("Le départ",
                  "Garde Espace chargé : au bout de cinq secondes, le trou noir grandit. Relâche."),
    "orbit": ("L'espace", "Z Q S D ou les flèches : un trou noir devant toi t'attire où tu vas."),
}
DEPARTURE_HOLD = 5.0  # seconds of the jump held for the departure
HOLD_OFF = -100000  # the clock of the hold when the jump is not held at full charge
HERO_CHARGE_TIME = 1.2  # seconds to charge a jump fully
PRACTICE_SECONDS = {"black": 6, "white": 6, "harvest": 5, "inventory": 3, "jump": 2}


def step_texts():
    """The title and the line of each step. On the steps seen close, they come once the camera is
    close (the title, then the line)."""
    made = []
    close = LATE_TEXTS
    for step, (title, line) in TEXTS.items():
        phases = [step] + ([f"{step}_practice"] if step in PRACTICE_SECONDS else [])
        for part, y, size, alpha in (("title", SCREEN_H - 168, 50, 240),
                                     ("line", SCREEN_H - 104, 32, 215)):
            name = f"text_{step}_{part}"
            text = entity(name, SCREEN_W / 2, y, {
                "text2d.Text": {"string": f"tutorial.{step}.{part}", "font": "font",
                                "size": size, "pivot_x": 0.5, "pivot_y": 0.5, "screen": True,
                                "alpha": 0 if step in close else alpha},
                "phase.ShowIn": {"phases": phases},
                "transform2d.Hidden": {},
            })
            made.append(text)
            if step in close:
                tag = f"text_{step}_{part}"
                text["components"]["tag.Tag"] = {"tag": tag}
                later = 0 if part == "title" else 25
                made.append(track(f"{name}_comes", tag, "text2d.Text.alpha",
                                  [(0, 0), (TEXT_AFTER + later, 0),
                                   (TEXT_AFTER + later + 30, alpha)], start=close[step]))
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


def tagged(made, tag):
    """The entity made, answering to tag (a tag.Tag)."""
    made["components"]["tag.Tag"] = {"tag": tag}
    return made


def on(name, action, source, field, value):
    return {"name": name, "components": {"rules.OnAction": {
        "action": action, "source": source, "field": field, "value": value}}}


# The steps seen close (D18): the camera comes close as the step starts, then its text shows,
# and the player does it seen close (the black hole that comes, the barrier that rises, the
# fossils and the ground swallowed, the black holes under his feet, the one over his head); it
# goes back once the step is done (walking and jumping follow each other: it stays close).
CLOSE_STEPS = [
    # (name, the action that starts it, the one that ends it): from the show to his success.
    ("black", "landing_done", "black_done"),
    ("white", "black_next", "white_done"),
    ("harvest", "white_next", "harvest_done"),
    ("walk", "inventory_next", "jump_done"),
]
# The texts of the shows come once the camera is close.
LATE_TEXTS = {"black_show": "landing_done", "white_show": "black_next",
              "harvest_show": "white_next", "walk_show": "inventory_next",
              "jump_show": "walk_done"}
AWAKE_ZOOM = ZOOM * 1.7  # the camera close to him
TEXT_AFTER = 60  # ticks: the camera is close, then the text comes
# The first time he does it, light gathers into the part of his body that does it: (name, step,
# watched tag, field, from, part followed, offset y, particles colour, additive).
AWAKENINGS = [
    ("black", "black", "black_hole", "attractor2d.Attractor.radius", 6, "hand_right", 0,
     (150, 160, 190), False),
    ("white", "white", "white_caster", "barrier2d.Caster.radius", 6, "hand_left", 0,
     (240, 244, 255), True),
    ("harvest", "harvest", "harvest_bubble", "attractor2d.Attractor.radius", 20, "hero", -10,
     (230, 236, 250), True),
    ("walk", "walk", "hero", "transform2d.Velocity.x", 60, "hero", 40, (210, 220, 240), True),
    ("jump", "jump", "hero", "jump.Jumper.jumps", 1, "hero", -6, (210, 220, 240), True),
]


def close_steps():
    """The camera close during the steps of CLOSE_STEPS. Close, it also looks a little lower
    (he is in the middle of the screen, his feet above the texts) and follows him exactly (no
    dead zone); going back, it comes back to where a camera at rest stays, at the edge of its
    dead zone (60 above where it aims), then the dead zone comes back."""
    made = []
    for step, begins, ends in CLOSE_STEPS:
        made += [
            track(f"{step}_comes_close", "camera", "camera2d.Camera.zoom",
                  [(0, ZOOM), (50, AWAKE_ZOOM)], start=begins),
            track(f"{step}_looks_down", "camera", "camera2d.CameraFollow.offset_y",
                  [(0, -40), (50, 30)], start=begins),
            track(f"{step}_follows_close", "camera", "camera2d.CameraFollow.dead_y",
                  [(0, 0), (1, 0)], start=begins),
        ]
        made += [
            track(f"{step}_goes_back", "camera", "camera2d.Camera.zoom",
                  [(0, AWAKE_ZOOM), (60, ZOOM)], start=ends),
            track(f"{step}_looks_back", "camera", "camera2d.CameraFollow.offset_y",
                  [(0, 30), (60, -40 - 60), (150, -40 - 60), (151, -40)], start=ends),
            track(f"{step}_follows_back", "camera", "camera2d.CameraFollow.dead_y",
                  [(0, 0), (150, 0), (151, 60)], start=ends),
        ]
    return made


def demonstrations():
    """What the game does by itself before the player's turn (D19): a black hole that swallows
    a rock, a bubble and a wall, a harvest that swallows a fossil; he walks, then jumps."""
    hole_look = {"sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                                     "layer": LAYERS["hero"] + 5, "unlit": True},
                 "transform2d.Scale": {"factor": 0}, "transform2d.Rotation": {}, **HOLE_LOOP}
    made = [
        entity("demo_black", 960, GROUND_Y - 30, dict({
            "tag.Tag": {"tag": "demo_black"},
            "transform2d.Disabled": {}, "phase.ActiveIn": {"phases": ["black_show"]},
            "attractor2d.Attractor": {"start_radius": 6, "grow": 30, "max_radius": 60,
                                      "shrink": 120, "pull": 1500, "pulls": ["rock"],
                                      "inner": HEART},
            "light2d.Light": {"radius": 140, "red": 190, "green": 210, "blue": 255,
                              "intensity": 0.6},
            "particles2d.Emitter": dict(VORTEX_EMITTER, pull=1800, swirl=520, rate=40)},
            **hole_look)),
        bind("demo_black_shows", "demo_black", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 5000, 0, 5000, 1, onto="demo_black"),
        spin("demo_black_turns", "demo_black", 150),
        entity("demo_harvest", 330, GROUND_Y - 12, {
            "tag.Tag": {"tag": "demo_harvest"},
            "transform2d.Disabled": {}, "phase.ActiveIn": {"phases": ["harvest_show"]},
            "attractor2d.Attractor": {"start_radius": 10, "grow": 25, "max_radius": 50,
                                      "shrink": 200, "pull": 300, "pulls": ["rock"],
                                      "inner": 1},
            "terrain2d.Digger": {"radius": 0, "layers": ["ground"]},
            "sprite2d.Sprite": {"atlas": "harvest", "width": 2, "height": 2,
                                "layer": LAYERS["hero"] + 5, "unlit": True},
            "transform2d.Scale": {"factor": 0}}),
        bind("demo_harvest_shows", "demo_harvest", "attractor2d.Attractor.radius",
             "transform2d.Scale.factor", 0, 5000, 0, 5000, 1, onto="demo_harvest"),
        bind("demo_harvest_digs", "demo_harvest", "attractor2d.Attractor.heart",
             "terrain2d.Digger.radius", 0, 5000, 0, 5000, 1, onto="demo_harvest"),
    ]
    # The white: a bubble, then a wall, raised and taken down by the story.
    for name, x, y, round_, extra in (("demo_bubble", 760, GROUND_Y - 130, True,
                                       {"radius": 46}),
                                      ("demo_wall", 880, GROUND_Y - 70, False,
                                       {"end_x": 1000, "end_y": GROUND_Y - 150})):
        made.append(entity(name, x, y, {"tag.Tag": {"tag": name}, "barrier2d.Barrier": dict({
            "pool": 7, "round": round_, "layer": ["barrier"], "freezes": ["rock"],
            "atlas": "barrier", "bubble": 0, "wall": 1, "draw_layer": LAYERS["hero"] + 4,
            "thickness": 14}, **extra, **BARRIER_LOOK)}))
    made += [
        on("demo_bubble_up", "black_next", "demo_bubble", "barrier2d.Barrier.up", 1),
        on("demo_bubble_down", "white_show_done", "demo_bubble", "barrier2d.Barrier.up", 0),
        on("demo_wall_up", "demo_wall_now", "demo_wall", "barrier2d.Barrier.up", 1),
        on("demo_wall_down", "white_show_done", "demo_wall", "barrier2d.Barrier.up", 0),
        entity("demo_wall_timer", 0, 0, {"tween2d.Track": {
            "field": "transform2d.Position.x", "keys": 2, "t0": 0, "v0": 0, "t1": 170,
            "v1": 1, "start": "black_next", "end_action": "demo_wall_now"}}),
        # Walking: he walks a little by himself; jumping: he charges, then lets go.
        {"name": "demo_walks", "components": {"rules.Press": {
            "start": "inventory_next", "action": "demo_walk", "value": 1, "delay": 70,
            "ticks": 90}}},
        {"name": "demo_jumps", "components": {"rules.Press": {
            "start": "walk_done", "action": "demo_jump", "value": 1, "delay": 80,
            "ticks": 55}}},
        # The jump shown in slow motion (D22): the hole at its biggest, then the leap it gives,
        # slowly enough to see that the hole throws him; then the world goes at its speed again.
        entity("world_speed", 0, 0, {"tag.Tag": {"tag": "world_speed"},
                                     "transform2d.WorldSpeed": {"factor": 1}}),
        track("demo_jump_slows", "world_speed", "transform2d.WorldSpeed.factor",
              [(0, 1), (112, 1), (130, 0.25), (215, 0.25), (240, 1)], start="walk_done"),
    ]
    return made


def awakenings():
    """The first time he uses a power, harvests, walks or jumps, light gathers, turning, into the
    part of his body that does it (the black hand, the white hand, the body, the feet), which
    glows (D16, D18)."""
    made = close_steps()
    for name, step, source, field, low, part, offset_y, colour, additive in AWAKENINGS:
        action = f"awakens_{name}"
        made.append({"name": f"{name}_awakens", "components": {
            "rules.Watch": {"source": source, "own": False, "field": field, "min": low,
                            "action": action, "once": True},
            "phase.ActiveIn": {"phases": [step] + ([f"{step}_practice"] if step in
                                                   PRACTICE_SECONDS else [])},
            "transform2d.Disabled": {}}})
        gather = f"{name}_gathers"
        made.append(entity(gather, 0, 0, {
            "tag.Tag": {"tag": gather},
            "follow2d.Follow": {"target": part, "offset_y": offset_y, "stiffness": 1},
            "particles2d.Emitter": {
                "rate": 0, "lifetime": 0.9, "speed": 10, "spread": 360, "pull": 900,
                "swirl": 380, "drag": 1.2, "size": 3.5, "end_size": 1.2, "red": colour[0],
                "green": colour[1], "blue": colour[2], "alpha": 235, "fade": True,
                "additive": additive, "unlit": True, "layer": LAYERS["hero"] + 4,
                "area_width": 150, "area_height": 150, "max": 260}}))
        made.append(track(f"{name}_gathers_light", gather, "particles2d.Emitter.rate",
                          [(0, 0), (20, 240), (120, 240), (150, 0)], start=action))
        shine = f"{name}_shines"
        made.append(entity(shine, 0, 0, {
            "tag.Tag": {"tag": shine},
            "follow2d.Follow": {"target": part, "offset_y": offset_y, "stiffness": 1},
            "sprite2d.Sprite": {"atlas": "glow", "width": 70, "height": 70, "alpha": 0,
                                "additive": True, "unlit": True,
                                "layer": LAYERS["hero"] + 4}}))
        made.append(track(f"{name}_shines_out", shine, "sprite2d.Sprite.alpha",
                          [(0, 0), (90, 0), (120, 230), (160, 120), (210, 0)], start=action))
    return made


def story(fall=0):
    """The steps of the tutorial (module phase), what ends each one (rules.Watch) and what each
    one gives (rules.OnAction). The clock counts the seconds of the steps of practice."""
    made = [entity("story", 0, 0, {"tag.Tag": {"tag": "story"},
                                   "phase.Phase": {"current": "landing"}}),
            entity("clock", 0, 0, {"tag.Tag": {"tag": "clock"},
                                   "transform2d.Velocity": {"x": 1}})]
    ends = {}
    for index, step in enumerate(STEPS[:-1]):
        action = {"death": "revive", "end": "in_orbit"}.get(step, f"{step}_done" if not step.endswith(
            "_practice") else f"{step[:-len('_practice')]}_next")
        ends[step] = action
        made.append({"name": f"after_{step}", "components": {"phase.Transition": {
            "action": action, "from": [step], "from_any": False, "to": STEPS[index + 1]}}})
    # A step shown: the clock starts again, and the player's turn comes after some seconds.
    for step, seconds in SHOW_SECONDS.items():
        show = f"{step}_show"
        made.append(on(f"{show}_clock", ends[STEPS[STEPS.index(show) - 1]], "clock",
                       "transform2d.Position.x", 0))
        made.append(watch(f"{show}_watch", show, "clock", "transform2d.Position.x", ends[show],
                          seconds, inside=True))
    # A step of practice: the clock starts again, and the next step comes after some seconds.
    for step, seconds in PRACTICE_SECONDS.items():
        made.append(on(f"{step}_clock", ends[step], "clock", "transform2d.Position.x", 0))
        made.append(watch(f"{step}_practice_watch", f"{step}_practice", "clock",
                          "transform2d.Position.x", ends[f"{step}_practice"], seconds,
                          inside=True))
    made += [
        # The landing: he wakes, a moment, then the black (D4: he does not move yet).
        entity("landing_timer", 0, 0, {"tween2d.Tween": {
            "field": "transform2d.Position.x", "from": 0, "to": 1, "ticks": 170 + fall,
            "use_action": True, "action": "landing_done"}}),
        on("gives_black", "black_show_done", "kit", "loadout.Kit.unlocked", 1),
        # The black swallows two rocks.
        watch("black_watch", "black", "black_hole", "attractor2d.Attractor.swallowed",
              "black_done", 2),
        on("gives_white", "white_show_done", "kit", "loadout.Kit.unlocked", 3),
        # The white raises two barriers.
        watch("white_watch", "white", "white_caster", "barrier2d.Caster.raised", "white_done",
              2),
        on("gives_harvest", "harvest_show_done", "harvest_key", "loadout.Shortcut.action",
           ACTIONS.index("harvest")),
        # The harvest brings carbon in.
        watch("harvest_watch", "harvest", "carbon", "reserve.Reserve.gained", "harvest_done",
              15),
        # The inventory opened.
        watch("inventory_watch", "inventory", "inventory", "toggle.Toggle.times",
              "inventory_done", 1),
        # Walking, and jumping at once (a hole dug in front of him must not trap him); the jump
        # is the game's again while it is shown.
        on("learns_walking", "walk_show_done", "hero", "action_control.Controlled.action_x",
           ACTIONS.index("move_x")),
        on("learns_jumping", "walk_show_done", "hero", "jump.Jumper.action",
           ACTIONS.index("jump")),
        on("shows_jumping", "walk_done", "hero", "jump.Jumper.action", ACTIONS.index("demo_jump")),
        on("jumps_again", "jump_show_done", "hero", "jump.Jumper.action", ACTIONS.index("jump")),
        # The obstacles: past the chasm.
        watch("obstacles_watch", "obstacles", "hero", "transform2d.Position.x", "obstacles_done",
              CHASM[1] + 60),
        watch("walk_watch", "walk", "hero", "transform2d.Position.x", "walk_done", WALK_TO),
        watch("jump_watch", "jump", "hero", "transform2d.Position.x", "jump_done",
              LEDGE[0] + 60),
        # From now on the reserves can run out.
        on("matter_unguarded", "obstacles_done", "matter", "reserve.Reserve.floor", 0),
        on("carbon_unguarded", "obstacles_done", "carbon", "reserve.Reserve.floor", 0),
        # The white runs low and fast: harvest carbon back above half.
        on("white_falls", "obstacles_done", "carbon", "reserve.Reserve.amount", 25),
        on("white_drains", "obstacles_done", "carbon", "reserve.Reserve.drain", 1.2),
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
        # The departure: the jump held five seconds, its black hole as big as it gets over his
        # head; full, it slips out of his hands (no need to let go).
        # The jump is the same as before (D19): only the time it is held counts. Full, the clock
        # starts; let go, it is put far back; five seconds held in all, he goes.
        entity("hold", HOLD_OFF, 0, {"tag.Tag": {"tag": "hold"},
                                     "transform2d.Velocity": {"x": 1}}),
        watch("charge_full_watch", "departure", "hero", "jump.Jumper.charge", "charge_full",
              0.995),
        on("hold_starts", "charge_full", "hold", "transform2d.Position.x", 0),
        watch("charge_lost_watch", "departure", "hero", "jump.Jumper.charge", "charge_lost",
              high=0.5),
        on("hold_lost", "charge_lost", "hold", "transform2d.Position.x", HOLD_OFF),
        # Thrown up out of the atmosphere (above the top of the planet's ground): gone. Not
        # high enough, or stopped by what is over his head, he falls back: to be done again.
        watch("departure_watch", "departure", "hero", "transform2d.Position.y",
              "departure_done", high=LEAVING_Y),
    ]
    return made + step_texts()


def tutorial(arrival=False):
    """The planet of the tutorial, from left to right: the landing on hard rock, the path, the
    pit and the ledge, the basin, the far edge. Its ground goes deep: what
    is dug opens the way down, and the camera follows. With arrival, he starts high in its sky,
    as the opening leaves him (the same picture: no cut), and falls to the ground."""
    bottom = GROUND_Y + TUTORIAL_DEPTH
    height = bottom - TUTORIAL_TOP
    ground = terrain_entity("ground", 0, TUTORIAL_TOP, TUTORIAL_W, height, GROUND_Y,
                            {**TERRAIN_RELIEF, "climb": 20})
    # The back wall of the ground, as deep as the inside's pictures start: the same relief, never
    # dug, nothing stands on it. What is dug near the surface shows it, not the far planes.
    back = terrain_entity("ground_back", 0, TUTORIAL_TOP, TUTORIAL_W,
                          GROUND_Y + CAVE_TOP + 160 - TUTORIAL_TOP, GROUND_Y,
                          {**TERRAIN_RELIEF, "layer": ["back"]})
    back["components"]["terrain2d.Look"].update({
        "layer": LAYERS["ground"] - 6, "pixels": 3, "red": 40, "green": 40, "blue": 45,
        "top_red": 50, "top_green": 50, "top_blue": 56, "lip": 4, "grain": 0.18, "smooth": 3})
    # Once the planet is far below, out of sight, it is gone (D19).
    for made in (ground, back):
        made["components"]["bounds2d.OutOfView"] = {"side_x": 0, "side_y": 1, "margin": 600,
                                                    "despawn": True}
    rock_w = LANDING_ROCK[1] - LANDING_ROCK[0]
    shapes = relief_shapes(GROUND_Y) + [
        # Walls at both ends, up to the top of the grid.
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
        # The obstacles (D19): a pillar of earth up to the top of the sky (dig through it), then
        # a chasm too wide to jump, not too deep to climb out of (a wall of white across it).
        shape("pillar", PILLAR[0], (TUTORIAL_TOP + GROUND_Y) / 2, PILLAR[1],
              GROUND_Y - TUTORIAL_TOP, True),
        shape("chasm", (CHASM[0] + CHASM[1]) / 2, GROUND_Y + CHASM[2] / 2 - 8,
              CHASM[1] - CHASM[0], CHASM[2] + 16, False),
    ]
    basin_floor = GROUND_Y + BASIN[2]
    things = [rock_entity(f"rock_{name}", x, GROUND_Y - 11)
              for name, x in (("a", 700), ("b", 760), ("c", 470), ("d", 830))]
    things += [fossil_entity(f"fossil_{name}", x, GROUND_Y - 11)
               for name, x in (("a", 420), ("b", 530))]
    # What the demonstrations use, worth nothing: a rock for the black, a fossil for the harvest.
    things += [rock_entity("rock_demo", 960, GROUND_Y - 11, amount=0),
               fossil_entity("fossil_demo", 330, GROUND_Y - 11, amount=0)]
    for index, x in enumerate((3850, 4000, 4150, 4300)):
        things.append(fossil_entity(f"fossil_{index + 1}", x, basin_floor - 11))
    for index, x in enumerate((4500, 4650, 4800, 4950)):
        things.append(rock_entity(f"rock_{index + 1}", x, basin_floor - 11))
    # In the ground, deeper and deeper: stones of matter and pockets of carbon crystals, seen in
    # the cut of the ground, found by digging (D19).
    things += buried(GROUND_Y, TUTORIAL_W)
    # Deposits of carbon all along: the white never lacks food for long.
    for index, (x, y) in enumerate(((1100, surface_at(1100)), (1500, surface_at(1500)),
                                    (2350, GROUND_Y - LEDGE[2]), (4250, basin_floor),
                                    (5400, surface_at(5400)), (6100, surface_at(6100)))):
        things += deposit(f"deposit_{index + 1}", x, y)
    # Early on the reserves cannot fall below a quarter: nothing to lose before learning.
    return (arrival_sky_end(sky_entity(), arrival) + far_planes(scenery(GROUND_Y), arrival) +
            [ground, back] + shapes +
            things +
            [falling_now(hero_entity(LANDING_X, ARRIVAL_Y if arrival else 220, locked=True,
                                     falling=ARRIVAL_VY if arrival else 0), arrival),
             camera(TUTORIAL_W, bottom, ARRIVAL_Y - ARRIVAL_CAMERA_DY if arrival else None,
                    stiffness=0.25 if arrival else 0.06)] +
            hero_clips() + hero_trail() + hands() + powers(harvest_locked=True) +
            inventory() + jump_hole() +
            death(watch_story=True) + reserves(matter=80, carbon=80, floor=25) +
            story(ARRIVAL_FALL if arrival else 0) + landing(arrival) + awakenings() +
            demonstrations() + pause_menu() + departure())


FAR_PLANES = ["mountains", "mist_far", "mist_near", "ground_far", "hills_far",
              "hills_near", "inside_far"] + [f"{name}_skirt" for name, *_ in SKIRTED]


def far_planes(made, arrival):
    """With arrival, the planes of the planet's surface come out of the dark as he falls to it:
    high in its sky, there is only the sky (as the opening leaves him: no cut)."""
    if not arrival:
        return made
    for item in list(made):
        if item["name"] in FAR_PLANES:
            sprite = item["components"]["sprite2d.Sprite"]
            alpha = sprite.get("alpha", 255)
            sprite["alpha"] = 0
            item["components"].setdefault("tag.Tag", {"tag": item["name"]})
            tag = item["components"]["tag.Tag"]["tag"]
            made.append(track(f"{item['name']}_comes", tag, "sprite2d.Sprite.alpha",
                              [(20, 0), (ARRIVAL_FALL + 10, alpha)]))
    return made


def falling_now(hero, arrival):
    """With arrival, he is already falling, at the first frame of the fall's loop, where the
    opening leaves him (its last frame is the loop's last): no pose of rest in between."""
    if arrival:
        fall_first, fall_count = hero_frames("fall")
        fall_id = [name for name, _, _ in HERO_CLIPS].index("fall") + 1
        hero["components"]["sprite2d.Sprite"]["frame"] = fall_first
        hero["components"]["sprite2d.Animator"]["clip"] = fall_id
        hero["components"]["sprite2d.SpriteAnimation"] = {
            "first_frame": fall_first, "frame_count": fall_count, "ticks_per_frame": 1,
            "looping": True}
    return hero


def arrival_sky_end(made, arrival):
    """With arrival, the sky is bigger and still coming down as the opening leaves it; it stops
    as he lands (one motion with the opening's)."""
    if not arrival:
        return made
    sky = next(item for item in made if item["name"] == "sky")
    sky["components"]["sprite2d.Sprite"].update(atlas="sky_big", width=VIEW_W * SKY_BIG,
                                                height=VIEW_H * SKY_BIG)
    # The opening's last frame showed it at OPENING_TICKS - 2: this one, one tick on; the
    # track's first key, the next.
    sky["components"]["transform2d.Position"]["y"] = sky_slide(OPENING_TICKS - 1)
    veil = next(item for item in made if item["name"] == "veil")
    veil["components"]["tag.Tag"] = {"tag": "veil"}
    veil["components"]["sprite2d.Sprite"].update(width=1600 * 1.6 * VEIL_BIG,
                                                 height=900 * 1.6 * VEIL_BIG)
    veil["components"]["transform2d.Position"]["y"] = sky_slide(OPENING_TICKS - 1)
    ticks = [0, 1, 30, 60, 85, ARRIVAL_DOWN]
    keys = [(tick, sky_slide(OPENING_TICKS + tick)) for tick in ticks]
    return made + [curve("sky_comes_down", "sky", "transform2d.Position.y", keys),
                   curve("veil_comes_down", "veil", "transform2d.Position.y", keys)]


def landing(arrival=False):
    """The start of the tutorial: out of the white of the opening (or, with arrival, from high
    in the sky, as the opening leaves him), he falls to the ground."""
    if arrival:
        # The camera keeps up with his fall, then is soft again.
        return [track("camera_keeps_up", "camera", "camera2d.CameraFollow.stiffness",
                      [(0, 0.25), (ARRIVAL_FALL + 20, 0.25), (ARRIVAL_FALL + 80, 0.06)])]
    return [
        cover("whitein", 255),
        track("whitein_fades", "whitein", "sprite2d.Sprite.alpha", [(0, 255), (80, 0)]),
    ]


LEAVING_Y = TUTORIAL_TOP - 300  # above the top of the ground: out of the atmosphere, he is gone
ORBIT_SPEED = 320  # ... down to this speed upwards: he is out of the pull, he drifts
VOYAGE_TICKS = 600  # the drift to the next planet (GDD: 5 to 10 seconds, no control)
NEXT_PLANET = (1500, -1300)  # where the next planet is, from where he reaches orbit
SPACE_SPEED = 220  # his speed in space, moving by himself
SPACE_HOLE_AHEAD = 95  # the small black hole that pulls him in space: this far ahead of him
SPACE_HOLE_SIZE = 20  # ... and its radius
SPACE_HOLE_TURN = 240  # it goes round him where the arrows point, this many degrees a second
NEXT_PLANET_SIZE = 1400


def departure():
    """The end of the tutorial (D19 to D22): the jump held full, its black hole grown, throws
    him up out of the atmosphere; the planet's pull slows him down and bends his way; out of it,
    in orbit, he moves where he wants, a small black hole ahead of him, towards the next planet
    seen ahead. No cut scene, no other scene: the planet's ground is gone once out of sight."""
    planet_ahead = entity("next_planet", 0, 0, {
        "tag.Tag": {"tag": "next_planet"},
        "sprite2d.Sprite": {"atlas": "planet", "width": NEXT_PLANET_SIZE * PLANET_MARGIN,
                            "height": NEXT_PLANET_SIZE * PLANET_MARGIN, "layer": LAYERS["far"],
                            "alpha": 0,
                            "unlit": True},
        "transform2d.Scale": {"factor": 0.75},
        # Kept ahead of him until the drift starts; then it stays where it is.
        "follow2d.Follow": {"target": "hero", "offset_x": NEXT_PLANET[0],
                            "offset_y": NEXT_PLANET[1], "stiffness": 1}})
    air = entity("next_planet_air", 0, 0, {
        "tag.Tag": {"tag": "next_planet_air"},
        "sprite2d.Sprite": {"atlas": "glow", "width": NEXT_PLANET_SIZE * 1.5,
                            "height": NEXT_PLANET_SIZE * 1.5, "layer": LAYERS["far"] - 1,
                            "alpha": 0, "additive": True, "unlit": True},
        "transform2d.Scale": {"factor": 0.75},
        "follow2d.Follow": {"target": "next_planet", "stiffness": 1}})
    to_follow = entity("to_follow", SCREEN_W / 2, SCREEN_H - 150, {
        "tag.Tag": {"tag": "to_follow"},
        "text2d.Text": {"string": "tutorial.next", "font": "font", "size": 44, "pivot_x": 0.5,
                        "pivot_y": 0.5, "screen": True, "alpha": 0}})
    pull = entity("space_hole", 0, 0, {
        "tag.Tag": {"tag": "space_hole"},
        "sprite2d.Sprite": {"atlas": "black_hole", "width": 2, "height": 2,
                            "layer": LAYERS["hero"] + 5, "alpha": 0, "unlit": True},
        "transform2d.Scale": {"factor": 0}, "transform2d.Rotation": {},
        **HOLE_LOOP,
        # Ahead of him, the way he goes, always as far.
        "follow2d.Follow": {"target": "hero", "ahead": SPACE_HOLE_AHEAD, "stiffness": 1,
                            "way_x": ACTIONS.index("move_x"), "way_y": ACTIONS.index("move_y"),
                            "way_turn": SPACE_HOLE_TURN}})
    return [
        planet_ahead, air, to_follow, pull,
        # Out of the atmosphere: no control, no powers, his reserves kept; the
        # planet's pull slows him down and bends his way.
        on("no_steering", "departure_done", "hero", "action_control.Controlled.horizontal", 0),
        on("no_jump", "departure_done", "hero", "jump.Jumper.action", ACTIONS.index("locked")),
        on("no_powers", "departure_done", "kit", "loadout.Kit.unlocked", 0),
        on("no_dust", "departure_done", "hero_dust", "particles2d.Emitter.max", 0),
        on("matter_kept", "departure_done", "matter", "reserve.Reserve.drain", 0),
        on("carbon_kept", "departure_done", "carbon", "reserve.Reserve.drain", 0),
        on("no_ash", "departure_done", "ash", "particles2d.Emitter.rate", 0),
        on("sky_opens", "departure_done", "camera", "camera2d.CameraFollow.bound_top", -100000),
        on("space_opens", "departure_done", "camera", "camera2d.CameraFollow.bound_right",
           100000),
        on("bent", "departure_done", "hero", "gravity2d.Gravity.x", 140),
        watch("orbit_watch", "end", "hero", "transform2d.Velocity.y", "in_orbit",
              low=-ORBIT_SPEED, inside=True),
        # In orbit: nothing pulls him any more. He moves where he wants (D22): a small black hole
        # ahead of him, the way he goes, pulls him; slow to start, slow to stop.
        on("floats", "in_orbit", "hero", "gravity2d.Gravity.y", 0),
        # Nothing pulls him: he floats, or flies (D26).
        on("space_moves", "in_orbit", "hero", "sprite2d.Animator.set", 1),
        on("floats_x", "in_orbit", "hero", "gravity2d.Gravity.x", 0),
        on("steers_x", "in_orbit", "hero", "action_control.Controlled.horizontal", 1),
        on("steers_y", "in_orbit", "hero", "action_control.Controlled.vertical", 1),
        on("steers_up", "in_orbit", "hero", "action_control.Controlled.action_y",
           ACTIONS.index("move_y")),
        on("space_speed", "in_orbit", "hero", "action_control.Controlled.speed", SPACE_SPEED),
        on("space_start", "in_orbit", "hero", "action_control.Controlled.acceleration", 260),
        on("space_stop", "in_orbit", "hero", "action_control.Controlled.braking", 70),
        bind("space_hole_grows", "space_hole", "follow2d.Follow.speed",
             "transform2d.Scale.factor", 20, SPACE_SPEED * 0.6, 0, SPACE_HOLE_SIZE, 1,
             onto="space_hole"),
        track("space_hole_shows", "space_hole", "sprite2d.Sprite.alpha", [(0, 0), (30, 255)],
              start="in_orbit"),
        spin("space_hole_turns", "space_hole", 120),
        on("planet_stays", "in_orbit", "next_planet", "follow2d.Follow.stiffness", 0),
        track("planet_shows", "next_planet", "sprite2d.Sprite.alpha", [(0, 0), (160, 255)],
              start="in_orbit"),
        track("planet_air_shows", "next_planet_air", "sprite2d.Sprite.alpha",
              [(0, 0), (160, 70)], start="in_orbit"),
        track("planet_nears", "next_planet", "transform2d.Scale.factor",
              [(0, 0.75), (VOYAGE_TICKS, 1.0)], start="in_orbit"),
        track("planet_air_nears", "next_planet_air", "transform2d.Scale.factor",
              [(0, 0.75), (VOYAGE_TICKS, 1.0)], start="in_orbit"),
        track("voyage_zoom", "camera", "camera2d.Camera.zoom",
              [(0, ZOOM), (240, 0.72 * ZOOM), (VOYAGE_TICKS - 60, 0.72 * ZOOM),
               (VOYAGE_TICKS + 120, 0.9 * ZOOM)], start="in_orbit"),
        track("to_follow_shows", "to_follow", "text2d.Text.alpha",
              [(0, 0), (VOYAGE_TICKS - 60, 0), (VOYAGE_TICKS + 60, 230)], start="in_orbit"),
    ]


# The colours of the seven fragments, and the order they go out after the explosion (GDD §2:
# "plein de couleurs, puis de moins en moins").
FRAGMENT_COLOURS = [("pink", (240, 130, 190)), ("violet", (150, 90, 210)),
                    ("orange", (240, 140, 50)), ("red", (220, 60, 50)),
                    ("yellow", (240, 210, 80)), ("green", (80, 190, 110)),
                    ("blue", (70, 130, 230))]
BANG = 220  # black, a point that grows and trembles; then the explosion, this tick (D24)
REVEAL = (170, 470)  # the white light of the explosion fades out between these ticks: him, far
WANDER = 720  # he is near, he wanders to the planet of the tutorial
ARRIVAL = (1450, 1760)  # the pivot: the planet slides down, the sky after it, and stops
OPENING_TICKS = ARRIVAL[1] + 5  # then the tutorial, as he falls (from the explosion)
PLANET_W = 400  # the planet seen from space, at first (world units at the zoom at rest)
# Where the tutorial starts him, high in its sky, and where its camera has him then.
ARRIVAL_Y = -1400
ARRIVAL_CAMERA_DX = 40  # the tutorial's camera stops at the left edge: he is this far left
ARRIVAL_PULL = ARRIVAL[1] - 35  # the planet pulls him: he starts falling (the sky still stopping)
# How he is when the tutorial takes over (measured on the opening): falling this fast, the camera
# this far above him.
ARRIVAL_VY = 1100
ARRIVAL_CAMERA_DY = 152.7
ARRIVAL_FALL = 70  # the ticks his fall takes, more than from the usual start
ARRIVAL_DOWN = 107  # ... and from the tutorial's start to the ground
SKY_BIG = 1.9  # the sky of the arrival (its own picture), bigger than the screen: it can slide
PIVOT_SLIDE = 640  # ... down this much with the pivot, nearly a screen: the view turns 90 degrees
FALL_RISE = 180  # ... then up this much as he falls (the stars rise)
VEIL_BIG = 1.15  # its veil of gas, bigger too: it slides with it
# The ticks (of the opening) of the pivot, of the planet's pull and of his landing.
SKY_SLIDE_TICKS = (1380, ARRIVAL_PULL, OPENING_TICKS + ARRIVAL_DOWN)


def sky_slide(tick):
    """Where the sky of the arrival is at a tick of the opening (past its end: of the tutorial
    that follows, from OPENING_TICKS): it comes down with the pivot, slows down, then, as he
    falls to the planet, goes up (the stars rise), slower and slower until he lands (D26)."""
    start, pull, end = SKY_SLIDE_TICKS
    if tick <= pull:
        k = min(max((tick - start) / (pull - start), 0.0), 1.0)
        return round(PIVOT_SLIDE / 2 - PIVOT_SLIDE * (1 - k * k * (3 - 2 * k)), 2)
    k = min((tick - pull) / (end - pull), 1.0)
    return round(PIVOT_SLIDE / 2 - FALL_RISE * k * k * (3 - 2 * k), 2)
# The camera's zoom along the opening: back from the explosion, close on him as he nears, then
# at rest, as in the tutorial.
OPENING_ZOOM = [(0, 1.3 * ZOOM), (240, 0.55 * ZOOM), (WANDER, 1.6 * ZOOM), (880, ZOOM)]
SKY_MARGIN = 1.08  # the skies of the opening, a little bigger than the screen whatever the zoom
# The gas of the explosion, the colours going out in turn (GDD §2), then the grey that stays.
CLOUD_TINTS = [(255, 150, 70), (255, 110, 190), (180, 110, 255), (90, 140, 255),
               (80, 210, 255), (255, 210, 140)]
CLOUD_GREY = (130, 134, 146)
CLOUD_COUNT, CLOUD_GREYS = 22, 6  # clouds of the explosion; the last ones grey
GLOW_COUNT = 4


def hero_frames(clip):
    """The first frame of a clip of the hero in its atlas, and how many it has."""
    first = 0
    for name, poses, _ in HERO_CLIPS:
        if name == clip:
            return first, len(poses)
        first += len(poses)
    raise KeyError(clip)


def bang_clouds():
    """The clouds of gas of the explosion (docs: the big bang): they fly out of it, grow and turn
    slowly; coloured and glowing first, going out in turn, the grey ones staying (D23)."""
    rng = np.random.default_rng(23)
    made = []
    for index in range(CLOUD_COUNT):
        grey = index >= CLOUD_COUNT - CLOUD_GREYS
        name = f"cloud_{index}"
        angle = math.radians(index * 137.5 + float(rng.uniform(-20, 20)))
        near = float(rng.uniform(250, 700) if grey else rng.uniform(150, 420))
        speed = float(rng.uniform(40, 130))
        size = round(float(rng.uniform(900, 1400) if grey else rng.uniform(750, 1300)))
        tint = CLOUD_GREY if grey else CLOUD_TINTS[index % len(CLOUD_TINTS)]
        sprite = {"atlas": "clouds", "frame": index % 4, "width": size, "height": size,
                  "layer": -103 if grey else -98 + index, "alpha": 0, "unlit": True,
                  "red": tint[0], "green": tint[1], "blue": tint[2]}
        if not grey:
            sprite["additive"] = True
        turn = round(float(rng.uniform(0, 360)))
        before = BANG / 60 * speed  # they move from the start: there at the bang
        made.append(entity(name, round(math.cos(angle) * (near - before)),
                           round(math.sin(angle) * (near - before)), {
            "tag.Tag": {"tag": name}, "sprite2d.Sprite": sprite,
            "transform2d.Velocity": {"x": round(math.cos(angle) * speed, 1),
                                     "y": round(math.sin(angle) * speed, 1)},
            "transform2d.Scale": {"factor": 0.3}, "transform2d.Rotation": {"angle": turn}}))
        if grey:
            strength = round(float(rng.uniform(70, 120)))
            keys = [(250, 0), (450 + round(float(rng.uniform(0, 80))), strength),
                    (1100, strength), (1300, 0)]
        else:
            strength = round(float(rng.uniform(200, 255)))
            out = 330 + (index % len(CLOUD_TINTS)) * 45 + round(float(rng.uniform(0, 30)))
            keys = [(4, 0), (30 + round(float(rng.uniform(0, 40))), strength),
                    (out, round(strength * 0.8)), (out + 160, 0)]
        made += [
            track(f"{name}_shows", name, "sprite2d.Sprite.alpha", keys),
            track(f"{name}_grows", name, "transform2d.Scale.factor",
                  [(0, 0.3), (220, 1.0), (OPENING_TICKS, 1.9)]),
            track(f"{name}_turns", name, "transform2d.Rotation.angle",
                  [(0, turn), (OPENING_TICKS, turn + round(float(rng.uniform(-35, 35))))]),
        ]
    # Glows of colour in the gas, coming and going.
    for index, tint in enumerate(CLOUD_TINTS[:GLOW_COUNT]):
        name = f"glow_{index}"
        angle = math.radians(40 + index * 95)
        made += [
            entity(name, round(math.cos(angle) * (140 - BANG / 60 * 45)),
                   round(math.sin(angle) * (140 - BANG / 60 * 45)), {
                "tag.Tag": {"tag": name},
                "sprite2d.Sprite": {"atlas": "glow", "width": 1100, "height": 1100,
                                    "layer": -97, "alpha": 0, "additive": True, "unlit": True,
                                    "red": tint[0], "green": tint[1], "blue": tint[2]},
                "transform2d.Velocity": {"x": round(math.cos(angle) * 45, 1),
                                         "y": round(math.sin(angle) * 45, 1)}}),
            track(f"{name}_shows", name, "sprite2d.Sprite.alpha",
                  [(0, 0), (25 + index * 10, 170), (160 + index * 50, 120),
                   (330 + index * 60, 0)]),
        ]
    return made


def bang_light():
    """The light of the explosion: a white heart that stays, then fades out slowly; rays, two
    crowds turning each its way; a long flat streak and reflections, as through a lens."""
    made = [
        entity("core", 0, 0, {"tag.Tag": {"tag": "core"}, "sprite2d.Sprite": {
            "atlas": "bang_core", "width": 700, "height": 700, "layer": 50, "additive": True,
            "alpha": 0, "unlit": True}, "transform2d.Scale": {"factor": 0.5}}),
        track("core_grows", "core", "transform2d.Scale.factor",
              [(0, 0.3), (20, 1.1), (REVEAL[0], 1.25), (REVEAL[1], 0.5)]),
        track("core_fades", "core", "sprite2d.Sprite.alpha",
              [(0, 255), (REVEAL[0], 255), (REVEAL[1], 0)]),
    ]
    for name, tint, layer, scale, alpha, turn in (
            ("rays_a", (255, 255, 255), 49, [(0, 0.15), (30, 1.0), (320, 2.6)],
             [(0, 0), (6, 200), (140, 150), (380, 0)], 360),
            ("rays_b", (255, 200, 160), 48, [(10, 0.3), (60, 1.4), (380, 3.4)],
             [(10, 0), (35, 130), (210, 90), (430, 0)], -360)):
        made += [
            entity(name, 0, 0, {"tag.Tag": {"tag": name}, "sprite2d.Sprite": {
                "atlas": "bang_rays", "width": 1600, "height": 1600, "layer": layer,
                "alpha": 0, "additive": True, "unlit": True, "red": tint[0],
                "green": tint[1], "blue": tint[2]},
                "transform2d.Scale": {"factor": scale[0][1]}, "transform2d.Rotation": {}}),
            track(f"{name}_grows", name, "transform2d.Scale.factor", scale),
            track(f"{name}_shows", name, "sprite2d.Sprite.alpha", alpha),
            track(f"{name}_turns", name, "transform2d.Rotation.angle",
                  [(0, 0), (9000, turn)], smooth=False),
        ]
    made += [
        entity("streak", 0, 0, {"tag.Tag": {"tag": "streak"}, "sprite2d.Sprite": {
            "atlas": "flares", "frame": 0, "width": 3200, "height": 200, "layer": 52,
            "alpha": 0, "additive": True, "unlit": True}, "transform2d.Scale": {"factor": 0.4}}),
        track("streak_grows", "streak", "transform2d.Scale.factor",
              [(0, 0.4), (40, 1.0), (420, 1.3)]),
        track("streak_shows", "streak", "sprite2d.Sprite.alpha",
              [(0, 0), (12, 255), (200, 170), (430, 0)]),
    ]
    for index, (x, y, size, frame, tint) in enumerate((
            (190, 105, 110, 1, (255, 255, 255)), (340, 190, 60, 2, (200, 220, 255)),
            (-280, -155, 170, 1, (255, 200, 170)), (-130, -72, 46, 2, (220, 190, 255)))):
        name = f"ghost_{index}"
        made += [
            entity(name, x, y, {"tag.Tag": {"tag": name}, "sprite2d.Sprite": {
                "atlas": "flares", "frame": frame, "width": size * 4, "height": size,
                "layer": 53, "alpha": 0, "additive": True, "unlit": True, "red": tint[0],
                "green": tint[1], "blue": tint[2]}}),
            track(f"{name}_shows", name, "sprite2d.Sprite.alpha",
                  [(8, 0), (40, 120), (180, 90), (320, 0)]),
        ]
    return made


def later(made, ticks):
    """The tracks among made played that many ticks later (before its first key, a track
    leaves its field alone)."""
    for item in made:
        track_of = item["components"].get("tween2d.Track")
        if track_of and not track_of.get("loop"):
            for index in range(track_of["keys"]):
                track_of[f"t{index}"] += ticks
    return made


def screen_entity(name, x, y, sprite, scale=1.0):
    """A picture of the background that stays on the screen whatever the camera does: (x, y)
    from the middle of the screen, in world units at the camera's zoom at rest."""
    return entity(name, x, y, {"tag.Tag": {"tag": name}, "sprite2d.Sprite": sprite,
                               "sprite2d.Parallax": {"x": 0, "y": 0},
                               "transform2d.Scale": {"factor": scale}})


def arrival_planet():
    """He nears the planet of the tutorial (D24, D27): a point of light far off at the bottom
    right of the screen, that grows coming towards him until it fills the background, its lands,
    seas and mountains getting finer. Then the pivot: the planet slides down out of the screen, the sky after it, slower and slower, until it stops: the planet is
    below him now, he falls to it."""
    # (tick, x, y, scale): where the planet is on the screen and how big.
    # (The camera is still closer than at rest when it shows: 2.1 times, then 1.5 from 880.)
    keys = [(780, 400, 205, 0.006), (1000, 330, 215, 0.3), (1150, 220, 260, 1.0),
            (1250, 120, 330, 2.2), (1420, 0, 700, 7.0), (ARRIVAL[1], 0, 2700, 10.0)]
    near_x = (PLANET_NEAR[0] + PLANET_NEAR[2]) / 2 * PLANET_W / 2
    near_y = (PLANET_NEAR[1] + PLANET_NEAR[3]) / 2 * PLANET_W / 2
    near_w = (PLANET_NEAR[2] - PLANET_NEAR[0]) * PLANET_W / 2
    near_h = (PLANET_NEAR[3] - PLANET_NEAR[1]) * PLANET_W / 2
    made = [
        screen_entity("planet", 400, 205, {
            "atlas": "planet", "width": PLANET_W * PLANET_MARGIN,
            "height": PLANET_W * PLANET_MARGIN, "layer": -96, "alpha": 0,
            "unlit": True}, 0.006),
        screen_entity("planet_near", 400, 205, {
            "atlas": "planet_near", "width": near_w, "height": near_h, "layer": -95,
            "alpha": 0, "unlit": True}, 0.006),
        # First a point of light, which lights up at once: the planet far off (its picture, a few
        # pixels wide, would flicker). As it nears, the disc takes its place.
        screen_entity("planet_dot", 400, 205, {
            "atlas": "glow", "width": PLANET_W * 1.6, "height": PLANET_W * 1.6, "layer": -95,
            "alpha": 0, "additive": True, "unlit": True, "red": 205, "green": 212,
            "blue": 228}, 0.006),
        track("planet_dot_shows", "planet_dot", "sprite2d.Sprite.alpha",
              [(780, 0), (786, 255), (830, 255), (880, 0)]),
        track("planet_shows", "planet", "sprite2d.Sprite.alpha", [(815, 0), (875, 255)]),
        track("planet_near_shows", "planet_near", "sprite2d.Sprite.alpha",
              [(1360, 0), (1440, 255)]),
    ]
    for name in ("planet", "planet_dot", "planet_near"):
        dx, dy = (near_x, near_y) if name == "planet_near" else (0, 0)
        made += [
            curve(f"{name}_x", name, "transform2d.Position.x",
                  [(t, round(x + dx * s, 1)) for t, x, y, s in keys]),
            curve(f"{name}_y", name, "transform2d.Position.y",
                  [(t, round(y + dy * s, 1)) for t, x, y, s in keys]),
            curve(f"{name}_grows", name, "transform2d.Scale.factor",
                  [(t, s) for t, x, y, s in keys]),
        ]
    return made


def arrival_sky():
    """The sky of the planet, behind it, a little bigger than the screen and higher; it comes
    down with the pivot and stops where it is when he falls (as the tutorial starts: the same
    picture, no cut)."""
    veil_x, veil_y = -0.06 * VIEW_W / 2, -0.06 * (ARRIVAL_Y - ARRIVAL_CAMERA_DY)
    # (the tutorial's veil, seen from its camera at the start: the same place on the screen)
    return [
        screen_entity("sky_t", 0, sky_slide(0), {
            "atlas": "sky_big", "width": VIEW_W * SKY_BIG, "height": VIEW_H * SKY_BIG,
            "layer": -118, "alpha": 0, "unlit": True}),
        # As in the tutorial, a veil of gas lit by him in the dark (light2d).
        screen_entity("veil_t", round(veil_x, 1), round(veil_y + sky_slide(0), 1), {
            "atlas": "veil", "width": 1600 * 1.6 * VEIL_BIG, "height": 900 * 1.6 * VEIL_BIG,
            "layer": -95,
            "alpha": 0}),
        track("sky_t_shows", "sky_t", "sprite2d.Sprite.alpha", [(1100, 0), (1300, 255)]),
        track("veil_t_shows", "veil_t", "sprite2d.Sprite.alpha", [(1100, 0), (1300, 200)]),
        # It comes down with the pivot, then goes up as he falls, until he lands (the tutorial
        # goes on with it: one motion).
        track("sky_t_comes", "sky_t", "transform2d.Position.y",
              [(tick, sky_slide(tick)) for tick in SKY_SLIDE_TICKS]),
        track("veil_t_comes", "veil_t", "transform2d.Position.y",
              [(tick, round(veil_y + sky_slide(tick), 1)) for tick in SKY_SLIDE_TICKS]),
    ]


def big_bang_seed():
    """Before the explosion (D24): black; a point of light appears, grows a little and
    trembles more and more; then it bursts."""
    return [
        entity("seed", 0, 0, {"tag.Tag": {"tag": "seed"}, "sprite2d.Sprite": {
            "atlas": "glow", "width": 60, "height": 60, "layer": 51, "alpha": 0,
            "additive": True, "unlit": True}, "transform2d.Scale": {"factor": 0.15}}),
        track("seed_shows", "seed", "sprite2d.Sprite.alpha",
              [(30, 0), (90, 255), (BANG, 255), (BANG + 1, 0)]),
        track("seed_grows", "seed", "transform2d.Scale.factor",
              [(30, 0.15), (90, 0.35), (BANG - 15, 0.9), (BANG, 0.7)]),
        track("seed_trembles_x", "seed", "transform2d.Position.x",
              [(0, 0), (2, 2.5), (4, -2), (6, 1.5), (8, -2.5), (10, 0)], smooth=False,
              loop=True),
        track("seed_trembles_y", "seed", "transform2d.Position.y",
              [(0, 0), (3, -2), (5, 2.5), (7, -1.5), (9, 2), (11, 0)], smooth=False,
              loop=True),
        # The camera trembles with it, more and more, and is thrown by the explosion.
        track("seed_shakes", "camera", "camera2d.CameraShake.strength",
              [(100, 0), (BANG, 7), (BANG + 1, 34), (BANG + 70, 0)]),
    ]


def opening():
    """The opening (GDD §2, D23, D24): black, a point that grows and trembles; an explosion,
    rays and reflections, clouds of gas in every colour flying out with rocks; the colours go out
    in turn, the sky builds up. The white light of its heart fades out slowly: far off in it,
    him. He grows nearer and wanders to the planet of the tutorial, which grows behind him until
    it fills the background; the pivot: it slides down, the sky after it, which stops; he falls
    (the tutorial's scene, the same picture: no cut)."""
    fall_first, fall_count = hero_frames("fall")
    hero_light = hero_entity(0, 0)["components"]["light2d.Light"]
    made = [
        # The skies stay on the screen, the screen's size whatever the zoom. Black after the
        # flash (nothing behind: true black); the gas and the stars come in turn.
        entity("void", 0, 0, {"tag.Tag": {"tag": "void"}, "sprite2d.Sprite": {
            "atlas": "plain", "width": SCREEN_W * 2, "height": SCREEN_H * 2,
            "layer": -120, "unlit": True, "red": 0, "green": 0, "blue": 0},
            "sprite2d.Parallax": {"x": 0, "y": 0}, "transform2d.Scale": {"factor": 1}}),
        entity("nebula", 0, 0, {"tag.Tag": {"tag": "nebula"}, "sprite2d.Sprite": {
            "atlas": "veil", "width": SCREEN_W * 1.4, "height": SCREEN_H * 1.4, "layer": -105,
            "alpha": 0, "unlit": True}, "sprite2d.Parallax": {"x": 0, "y": 0},
            "transform2d.Scale": {"factor": 1}}),
        entity("nebula_colour", 0, 0, {"tag.Tag": {"tag": "nebula_colour"},
                                       "sprite2d.Sprite": {
            "atlas": "veil_colour", "width": SCREEN_W * 1.4, "height": SCREEN_H * 1.4,
            "layer": -104, "alpha": 0, "unlit": True}, "sprite2d.Parallax": {"x": 0, "y": 0},
            "transform2d.Scale": {"factor": 1}}),
        entity("flash", 0, 0, {"tag.Tag": {"tag": "flash"}, "sprite2d.Sprite": {
            "atlas": "glow", "width": 300, "height": 300, "layer": 51, "additive": True,
            "unlit": True, "alpha": 0}, "transform2d.Scale": {"factor": 0.2}}),
        # The camera stays on him: on the heart of the explosion first, where he is. At the end,
        # as the tutorial's camera has him: it follows him the same way, the same light, the
        # same effects (the tutorial takes over as he falls: one motion).
        entity("camera", 0, 0, {"tag.Tag": {"tag": "camera"},
                                "camera2d.Camera": {"zoom": 1.3 * ZOOM},
                                "camera2d.CameraShake": {"strength": 0, "decay": 3,
                                                         "speed": 30},
                                "camera2d.CameraFollow": {"stiffness": 0.06, "look_ahead": 0.25},
                                "light2d.Ambient": {"red": 92, "green": 94, "blue": 106},
                                "postfx.Effects": {"bloom": 0.8, "vignette": 0.5,
                                                   "vignette_size": 0.4, "grain": 0.05,
                                                   "rays": 0, "rays_x": 0.5, "rays_y": 0.08,
                                                   "rays_length": 0.22}}),
        # Him, in the heart of the explosion: a speck in the white light, his limbs moving.
        entity("wanderer", 0, 0, {"tag.Tag": {"tag": "wanderer"}, "sprite2d.Sprite": {
            "atlas": "hero", "frame": fall_first, "width": CELL, "height": CELL, "layer": 45,
            "alpha": 0, "unlit": True}, "sprite2d.SpriteAnimation": {
                "first_frame": fall_first, "frame_count": fall_count, "ticks_per_frame": 3,
                "looping": True},
            "light2d.Light": dict(hero_light), "camera2d.CameraTarget": {},
            "transform2d.Velocity": {}, "gravity2d.Gravity": {"y": 0, "terminal_speed": 1100},
            "transform2d.Scale": {"factor": 0.02}, "transform2d.Rotation": {"angle": -20}}),
        cover("whiteout"),
    ]
    for name, atlas, layer, additive in (("gas", "gas", -112, False),
                                         ("gas_colour", "gas_colour", -111, False),
                                         ("stars_4", "stars_4", -110, True),
                                         ("stars_3", "stars_3", -109, True),
                                         ("stars_2", "stars_2", -108, True),
                                         ("stars_1", "stars_1", -107, True)):
        sprite = {"atlas": atlas, "width": SCREEN_W * SKY_MARGIN,
                  "height": SCREEN_H * SKY_MARGIN, "layer": layer, "alpha": 0, "unlit": True}
        if additive:
            sprite["additive"] = True
        made.append(entity(name, 0, 0, {"tag.Tag": {"tag": name}, "sprite2d.Sprite": sprite,
                                        "sprite2d.Parallax": {"x": 0, "y": 0},
                                        "transform2d.Scale": {"factor": 1}}))
    for name in ("void", "nebula", "nebula_colour", "gas", "gas_colour", "stars_1", "stars_2",
                 "stars_3", "stars_4"):
        made.append(track(f"{name}_covers", name, "transform2d.Scale.factor",
                          [(tick, round(1 / zoom, 4)) for tick, zoom in OPENING_ZOOM]))
    # New stars light up one by one all over the black, and fade into the sky that builds.
    made.append(entity("births", 0, 0, {"tag.Tag": {"tag": "births"}, "particles2d.Emitter": {
        "rate": 0, "lifetime": 3.5, "lifetime_spread": 1.5, "speed": 0, "size": 5,
        "end_size": 2, "red": 240, "green": 244, "blue": 255, "alpha": 255, "fade": True,
        "additive": True, "unlit": True, "layer": -100, "area_width": SCREEN_W * 1.4,
        "area_height": SCREEN_H * 1.4, "max": 400}}))
    # Sparks of every colour fly out, and go out in turn; the grey stays longest.
    for index, (name, colour) in enumerate(FRAGMENT_COLOURS + [("grey", (200, 200, 200))]):
        made += [
            entity(f"burst_{name}", 0, 0, {"tag.Tag": {"tag": f"burst_{name}"},
                                           "particles2d.Emitter": {
                "rate": 0, "burst": 160, "active": False,
                "lifetime": 2.2 + 0.6 * index if name != "grey" else 9,
                "lifetime_spread": 0.5, "speed": 440, "speed_spread": 320, "spread": 360,
                "drag": 0.45, "wander": 30, "size": 7, "end_size": 2.5, "red": colour[0],
                "green": colour[1], "blue": colour[2], "alpha": 255, "fade": True,
                "additive": True, "unlit": True, "layer": 40, "max": 180}}),
            track(f"burst_{name}_goes", f"burst_{name}", "particles2d.Emitter.active",
                  [(0, 1)], smooth=False),
        ]
    made += bang_light() + bang_clouds() + arrival_planet() + arrival_sky()
    made += [
        track("flash_grows", "flash", "transform2d.Scale.factor", [(0, 0.2), (30, 5)]),
        track("flash_fades", "flash", "sprite2d.Sprite.alpha", [(0, 255), (60, 0)]),
        # Black, then the sky builds up: a few bright stars, then more and more, new ones
        # lighting up all over; the gas of the universe in colours, which then go out: black
        # and white is left. Near the planet, its own sky takes their place.
        track("births_come", "births", "particles2d.Emitter.rate",
              [(70, 0), (110, 60), (380, 60), (470, 0)]),
        track("stars_1_come", "stars_1", "sprite2d.Sprite.alpha",
              [(80, 0), (170, 255), (1100, 255), (1300, 0)]),
        track("stars_2_come", "stars_2", "sprite2d.Sprite.alpha",
              [(150, 0), (250, 255), (1100, 255), (1300, 0)]),
        track("stars_3_come", "stars_3", "sprite2d.Sprite.alpha",
              [(230, 0), (340, 255), (1100, 255), (1300, 0)]),
        track("stars_4_come", "stars_4", "sprite2d.Sprite.alpha",
              [(310, 0), (440, 230), (1100, 230), (1300, 0)]),
        track("gas_colour_comes", "gas_colour", "sprite2d.Sprite.alpha",
              [(170, 0), (290, 255), (340, 255), (480, 0)]),
        track("nebula_colour_comes", "nebula_colour", "sprite2d.Sprite.alpha",
              [(200, 0), (300, 230), (350, 230), (490, 0)]),
        track("gas_comes", "gas", "sprite2d.Sprite.alpha",
              [(330, 0), (480, 255), (1100, 255), (1300, 0)]),
        track("dust_comes", "nebula", "sprite2d.Sprite.alpha",
              [(340, 0), (520, 210), (1100, 210), (1300, 0)]),
        # Out of the white light, far: he grows nearer, the camera closing in on him; he
        # wanders, turning slowly, and ends upright, his feet to the planet.
        track("wanderer_shows", "wanderer", "sprite2d.Sprite.alpha", [(0, 255)], smooth=False),
        track("wanderer_nears", "wanderer", "transform2d.Scale.factor",
              [(0, 0.02), (REVEAL[0], 0.02), (REVEAL[1], 0.25), (WANDER, 1.0)]),
        track("wanderer_turns", "wanderer", "transform2d.Rotation.angle",
              [(0, -20), (WANDER, 15), (1000, -12), (1250, 6), (ARRIVAL[0], 0)]),
        # He drifts, slowly; then stops drifting: the planet pulls him down, he falls.
        track("wanderer_drifts", "wanderer", "transform2d.Velocity.x",
              [(WANDER, 0), (WANDER + 150, 28), (ARRIVAL[0], 28), (ARRIVAL_PULL - 20, 0)]),
        track("wanderer_pulled", "wanderer", "gravity2d.Gravity.y",
              [(ARRIVAL_PULL - 1, 0), (ARRIVAL_PULL, 1800)], smooth=False),
        # His limbs at the pace of the tutorial's fall, before the end, and at the start of their
        # loop as the tutorial takes over (its animation starts there).
        track("wanderer_pace", "wanderer", "sprite2d.SpriteAnimation.ticks_per_frame",
              [(ARRIVAL[0], 3), (ARRIVAL[0] + 1, 1)], smooth=False),
        track("wanderer_loop", "wanderer", "sprite2d.SpriteAnimation.elapsed",
              [(OPENING_TICKS - fall_count, 0), (OPENING_TICKS, fall_count)], smooth=False),
        track("camera_zoom", "camera", "camera2d.Camera.zoom", OPENING_ZOOM),
        # Where the tutorial's camera has him: a little left and below the middle; then it
        # follows him as it does (a dead zone, a firmer hold while he falls).
        track("camera_aims_x", "camera", "camera2d.CameraFollow.offset_x",
              [(ARRIVAL[0], 0), (ARRIVAL_PULL - 10, ARRIVAL_CAMERA_DX)]),
        track("camera_aims_y", "camera", "camera2d.CameraFollow.offset_y",
              [(ARRIVAL[0], 0), (ARRIVAL_PULL - 10, -40)]),
        track("camera_dead_x", "camera", "camera2d.CameraFollow.dead_x",
              [(ARRIVAL_PULL - 1, 0), (ARRIVAL_PULL, 30)], smooth=False),
        track("camera_dead_y", "camera", "camera2d.CameraFollow.dead_y",
              [(ARRIVAL_PULL - 1, 0), (ARRIVAL_PULL, 60)], smooth=False),
        track("camera_holds", "camera", "camera2d.CameraFollow.stiffness",
              [(ARRIVAL_PULL - 30, 0.06), (ARRIVAL_PULL, 0.25)]),
        track("camera_bloom", "camera", "postfx.Effects.bloom", [(ARRIVAL[0], 0.8),
                                                                (ARRIVAL[1], 0.45)]),
        track("camera_vignette", "camera", "postfx.Effects.vignette",
              [(ARRIVAL[0], 0.5), (ARRIVAL[1], BASE_VIGNETTE)]),
        track("camera_vignette_size", "camera", "postfx.Effects.vignette_size",
              [(ARRIVAL[0], 0.4), (ARRIVAL[1], BASE_VIGNETTE_SIZE)]),
        track("camera_rays", "camera", "postfx.Effects.rays", [(ARRIVAL[0], 0),
                                                              (ARRIVAL[1], 0.22)]),
    ]
    later(made, BANG)
    made += big_bang_seed() + [
        # The end: the tutorial, he falls to the ground (the same picture); or, Escape, the
        # tutorial at once.
        entity("to_arrival", 0, 0, {"scene_switch.GoToScene": {
            "scene": "arrival", "after_ticks": BANG + OPENING_TICKS}}),
        entity("to_tutorial", 0, 0, {"scene_switch.GoToScene": {
            "scene": "tutorial", "use_action": True, "action": "skip"}}),
    ]
    return made


# ---------------------------------------------------------------------------------------------
# The manifest

ACTIONS = ["move_x", "move_y", "jump", "out_of_matter", "out_of_carbon", "hand_left", "hand_right",
           "change", "pointer_x", "pointer_y", "harvest", "inventory", "take_off", "revive",
           "locked", "skip", "in_orbit", "refill", "awakens_black", "awakens_white",
           "awakens_harvest", "awakens_walk", "awakens_jump", "demo_walk", "demo_jump", "demo_wall_now", "charge_full", "charge_lost", "pause", "resume", "to_title", "menu_up",
           "menu_down", "accept", "menu_click", "play", "quit"]
for _step in STEPS[:-1]:
    for _action in (f"{_step}_done", f"{_step}_next"):
        if _action not in ACTIONS and not _step.endswith("_practice"):
            ACTIONS.append(_action)
TAGS = ["front", "black_gathers", "white_gathers", "harvest_gathers", "walk_gathers", "jump_gathers",
        "black_shines", "white_shines", "harvest_shines", "walk_shines", "jump_shines",
        "text_black_show_title", "text_white_show_title", "text_harvest_show_title",
        "text_walk_show_title", "text_jump_show_title", "text_black_show_line",
        "text_white_show_line", "text_harvest_show_line", "text_walk_show_line",
        "text_jump_show_line", "demo_black", "demo_harvest", "demo_bubble", "demo_wall", "hero", "hero_dust", "white_sparks", "black_hole_chips", "harvest_bubble_chips", "camera", "matter", "carbon", "black_hole", "white_caster", "harvest_bubble",
        "harvest_key", "ground_carbon", "inventory", "matter_bubble", "carbon_bubble",
        "jump_hole", "dead", "kit", "hand_left", "hand_right", "story", "clock", "stars_1",
        "stars_2", "stars_3", "stars_4", "sky", "hold", "space_hole", "world_speed", "gas", "gas_colour", "births", "ash",
        "next_planet", "next_planet_air", "nebula", "nebula_colour", "flash", "planet", "planet_dot", "sleeper",
        "wanderer", "void", "core", "seed", "pause_hero", "veil", *FAR_PLANES, "sky_t", "veil_t", "planet_near", "rays_a", "rays_b", "streak", "ghost_0", "ghost_1", "ghost_2", "ghost_3",
        "sleeper_bubble", "whiteout", "whitein", "runaway", "blast", "to_follow", "pause"]
TAGS += ([f"cloud_{index}" for index in range(CLOUD_COUNT)] +
         [f"glow_{index}" for index in range(GLOW_COUNT)] +
         [f"burst_{name}" for name, _ in FRAGMENT_COLOURS + [("grey", None)]])
ATLASES = ["sky", "sky_colour", "sky_big", "stars_1", "stars_2", "stars_3", "stars_4", "gas", "gas_colour", "veil", "veil_colour", "hills_far", "hills_near", "cave", "mountains", "mist", "ground_far", "front", "hero",
           "hero_curled", "orb", "black_hole", "rock", "fossil", "barrier", "harvest",
           "inventory", "plain", "font", "glow", "planet", "button", "crystal", "cave_glints",
           "cave_far", "ore", "planet_near", "bang_rays", "bang_core", "flares", "clouds"]
SCENE_NAMES = ["title", "opening", "arrival", "tutorial", "sandbox", "sandbox_low"]
SCENARIOS = ["walks", "jumps", "terrain", "life_drains", "edges_turn", "assigns_hands", "wheel",
             "hands_apart", "black_hole", "barrier_catches_rock", "harvest_sorts",
             "inventory_opens", "walking_costs", "charged_jump", "dies_and_restarts",
             "tutorial_through", "cannot_walk_early", "powers_in_order",
             "opening", "opening_skips", "pause_stops_everything", "pause_resumes",
             "title_starts", "jumps_sideways"]


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
            "layer": ["hero", "ground", "rock", "barrier", "back"],
            "tag": TAGS,
            "resource": ["matter", "carbon"],
            "phase": STEPS,
            "power": POWERS,
            "animation_set": ["hero", "hero_space"],
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
    entries["tutorial.next"] = "À suivre…"
    entries.update({"menu.title": "COMPLETE UNIVERSE", "menu.play": "Jouer",
                    "menu.quit": "Quitter", "menu.pause": "Pause", "menu.resume": "Reprendre",
                    "menu.leave": "Retour au titre"})
    with open(os.path.join(TEXT, "fr.json"), "w", encoding="utf-8") as out:
        json.dump({"schema_version": 1, "locale": "fr", "entries": entries}, out, indent=2,
                  ensure_ascii=False)
        out.write("\n")


# A climbed step of ground (a cell, 8 units, at once) glides over a few ticks (D29).
HERO_SMOOTH = {"share": 0.22, "most": 24, "snap": 300}


def smoothed(entities):
    """The hero, and what is drawn on him, drawn gliding after where he is (transform2d.Smooth):
    a stop against a wall, a step climbed, a corner turned show as a short glide."""
    for item in entities:
        components = item["components"]
        follows = components.get("follow2d.Follow", {})
        on_him = follows.get("target") == "hero" and follows.get("stiffness") == 1 and (
            "sprite2d.Sprite" in components) and not follows.get("ahead")
        if item["name"] in ("hero", "pause_hero") or on_him:
            components["transform2d.Smooth"] = dict(HERO_SMOOTH)
    return entities


def write_scene(name, entities):
    smoothed(entities)
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
    write_scene("arrival", tutorial(arrival=True))
    write_scene("opening", opening())
    write_scene("title", title())


def write_pictures():
    save(hero_sheet(), "hero", cell=(CELL_PX, CELL_PX))
    save(hero_curled(), "hero_curled")
    save(sky(), "sky")
    save(sky(coloured=True), "sky_colour")
    save(sky(scale=SKY_BIG), "sky_big")  # the arrival's, bigger: it slides nearly a screen
    for index, picture in enumerate(star_layers()):
        save(picture, f"stars_{index + 1}")
    save(gas(), "gas")
    save(gas(coloured=True), "gas_colour")
    save(nebula_veil(), "veil")
    save(nebula_veil(coloured=True), "veil_colour")
    save(hills(12, 170, 30, 9, 30), "hills_far")
    save(hills(13, 120, 13, 14, 8), "hills_near")
    save(orbs(), "orb", cell=(ORB, ORB))
    cave_rock, cave_glints = cave_pictures()
    save(cave_rock, "cave")
    save(cave_glints, "cave_glints")
    save(cave_far(), "cave_far")
    save(ore(), "ore")
    save(mountains(), "mountains")
    save(mist(51), "mist")
    save(ground_far(), "ground_far")
    save(foreground(), "front")
    save(black_holes(), "black_hole", cell=(HOLE_CELL, HOLE_CELL))
    save(rock(5), "rock")
    save(rock(9, colour=(120, 118, 110), light=(226, 224, 214), spiral=True), "fossil")
    save(barriers(), "barrier", cell=(BARRIER, BARRIER))
    save(sheet_of([button(False), button(True)], 2, *BUTTON), "button", cell=BUTTON)
    save(harvest(), "harvest")
    save(inventory_bubbles(), "inventory", cell=(INVENTORY, INVENTORY))
    save(Image.new("RGBA", (4, 4), (255, 255, 255, 255)), "plain")
    save(glow(), "glow")
    save(crystal(), "crystal")
    save(planet(), "planet")
    save(planet_near(), "planet_near")
    save(bang_rays(), "bang_rays")
    save(bang_core(), "bang_core")
    save(flares(), "flares", cell=(512, 128))
    save(clouds(), "clouds", cell=(CLOUD, CLOUD))
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
