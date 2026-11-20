#!/usr/bin/env python3
"""Rebuilds the generated files of chess: the atlas pictures, the prefabs, the scenes and the
scenarios.

    python3 games/chess/tools/make_assets.py

The pieces are drawn here, pixel by pixel, from a few shapes (no binary that cannot be
reviewed); the font is the one of doodle_jump (scripts/make_font.py). The rules of chess are
not here: they are the patterns of the pieces (entities of the scene), the guards, the zones of
promotion and the phases. This script only writes them down, which is long, not hard.

The test benches (scenes/test_*.json) are the real game with few pieces: a pin, a castling, a
promotion... so that each rule is checked on a position that is easy to read.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
from make_png import write_png  # noqa: E402

GAME = os.path.join(HERE, "..")
CELL = 64  # the size of a cell of the board, in world units
SIZE = 8  # cells on a side

# The kinds of pieces (the game's list mover_kind), as numbers where a rule needs one.
PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING = range(6)
KINDS = ["pawn", "knight", "bishop", "rook", "queen", "king"]
WHITE, BLACK = 0, 1

# The phases of the game (the list phase), and their bits in the sets of phase.ShowIn.
PLAYING, PROMOTION, WHITE_WON, BLACK_WON, DRAW = 1, 2, 4, 8, 16
PHASES = ["playing", "promotion", "white_won", "black_won", "draw"]
OVER = WHITE_WON | BLACK_WON | DRAW

# The window and what the camera makes of it (see CameraFit in the game scene).
WIDTH, HEIGHT = 480, 720
AREA = SIZE * CELL + 28
MARGIN_TOP, MARGIN_BOTTOM = 96, 196
ZOOM = min(WIDTH / AREA, (HEIGHT - MARGIN_TOP - MARGIN_BOTTOM) / AREA)
MIDDLE_Y = (MARGIN_TOP + HEIGHT - MARGIN_BOTTOM) / 2


# --- pictures ------------------------------------------------------------------------------

def rgba(hex_color, alpha=255):
    return [int(hex_color[i: i + 2], 16) for i in (0, 2, 4)] + [alpha]


CLEAR = [0, 0, 0, 0]
P = 32  # the size of a piece's picture


def circle(cx, cy, r):
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def ellipse(cx, cy, rx, ry):
    return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1


def rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1


def trapezoid(y0, y1, half0, half1, cx=15.5):
    """Centred; its half width goes from half0 at y0 to half1 at y1."""
    def inside(x, y):
        if not y0 <= y <= y1:
            return False
        half = half0 + (half1 - half0) * (y - y0) / max(y1 - y0, 1)
        return abs(x - cx) <= half
    return inside


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


def base():
    return union(rect(7, 25, 24, 28), rect(9, 23, 22, 24))


def shape_of(kind):
    if kind == PAWN:
        return union(circle(15.5, 10, 4.6), rect(11, 14, 20, 15), trapezoid(15, 24, 3, 7), base())
    if kind == ROOK:
        teeth = union(rect(8, 5, 10, 9), rect(13, 5, 18, 9), rect(21, 5, 23, 9))
        return union(teeth, rect(8, 8, 23, 10), rect(10, 10, 21, 23), base())
    if kind == KNIGHT:
        head = polygon([(9, 24), (11, 17), (10, 13), (14, 6), (16, 3), (17, 7), (21, 9), (24, 14),
                        (23, 18), (20, 16), (18, 15), (19, 19), (22, 24)])
        return union(head, base())
    if kind == BISHOP:
        return union(ellipse(15.5, 11, 4.4, 7), circle(15.5, 4, 2), trapezoid(17, 24, 3, 7),
                     rect(11, 17, 20, 18), base())
    if kind == QUEEN:
        spikes = union(*[circle(x, 6, 1.8) for x in (7.5, 11.8, 15.5, 19.3, 23.5)])
        crown = polygon([(7, 8), (11, 14), (13, 8), (15.5, 14), (18, 8), (20, 14), (24, 8),
                         (22, 20), (9, 20)])
        return union(spikes, crown, trapezoid(19, 24, 6, 7.5), base())
    cross = union(rect(14, 2, 17, 9), rect(12, 4, 19, 6))
    return union(cross, ellipse(15.5, 13, 7, 5), trapezoid(14, 24, 6, 7.5), base())


def draw_piece(kind, fill, outline):
    mask = shape_of(kind)
    inside = [[mask(x, y) for x in range(P)] for y in range(P)]
    tile = [[CLEAR] * P for _ in range(P)]
    for y in range(P):
        for x in range(P):
            if not inside[y][x]:
                continue
            edge = any(not (0 <= x + dx < P and 0 <= y + dy < P) or not inside[y + dy][x + dx]
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            tile[y][x] = outline if edge else fill
    if kind == KNIGHT:
        tile[9][18] = outline
        tile[9][19] = outline
    return tile


def solid(color, size=16):
    return [[color] * size for _ in range(size)]


def swatch(fill, edge):
    tile = solid(fill)
    for i in range(16):
        for a, b in ((i, 0), (i, 15), (0, i), (15, i)):
            tile[b][a] = edge
    return tile


def disc(radius, thickness=None):
    tile = [[CLEAR] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            if d <= radius and (thickness is None or d >= radius - thickness):
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


# --- squares -------------------------------------------------------------------------------

def cell_of(square):
    """"e4" gives (column, row): the files go from the left, the ranks from the bottom."""
    return ord(square[0]) - ord("a"), SIZE - int(square[1])


def centre(col, row):
    return (col - (SIZE - 1) / 2) * CELL, (row - (SIZE - 1) / 2) * CELL


def pixel_of(square):
    """Where a touch on a square is, in pixels of the window."""
    col, row = cell_of(square)
    x, y = centre(col, row)
    return round(WIDTH / 2 + x * ZOOM), round(MIDDLE_Y + y * ZOOM)


# --- prefabs -------------------------------------------------------------------------------

def prefabs():
    return {
        "piece": ("A piece: it sits on a cell, glides to the cell it is sent to, and shows the\n"
                  "picture of its kind (it changes with the kind: a promoted pawn changes\n"
                  "picture). The side and the kind are set where the piece is placed.", {
                      "schema_version": 1, "name": "piece", "components": {
                          "transform2d.Position": {},
                          "grid2d.GridPos": {"grid": "board", "glide": 0.4},
                          "turns.Team": {},
                          "grid_moves.Mover": {},
                          "sprite2d.Sprite": {"atlas": "pieces", "frame": 0, "width": 60,
                                              "height": 60, "layer": 2},
                          "sprite2d.FrameBind": {"field": "grid_moves.Mover.kind",
                                                 "frame_count": 6}}}),
        "button": ("A button under the board: a picture and its text.", {
            "schema_version": 1, "name": "button", "components": {
                "transform2d.Position": {},
                "sprite2d.Sprite": {"atlas": "ui", "frame": 1, "width": 200, "height": 44,
                                    "screen": True, "layer": 10},
                "text2d.Text": {"font": "font", "size": 20, "pivot_x": 0.5, "pivot_y": 0.5,
                                "screen": True},
                "ui.Button": {"menu": "game_menu", "width": 200, "height": 44, "normal_frame": 1,
                              "focus_frame": 2}}}),
    }


# --- patterns: how every kind of piece moves ----------------------------------------------

def pattern(kind, dx, dy, **fields):
    data = {"kind": KINDS[kind], "dx": dx, "dy": dy}
    data.update(fields)
    return {"name": "", "components": {"grid_moves.Pattern": data}}


DIAGONALS = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
LINES = [(1, 0), (-1, 0), (0, 1), (0, -1)]
LEAPS = [(1, 2), (2, 1), (-1, 2), (-2, 1), (1, -2), (2, -1), (-1, -2), (-2, -1)]


def patterns():
    out = []
    for dx, dy in LEAPS:
        out.append(pattern(KNIGHT, dx, dy, jump=True))
    for dx, dy in DIAGONALS:
        out.append(pattern(BISHOP, dx, dy))
    for dx, dy in LINES:
        out.append(pattern(ROOK, dx, dy))
    for dx, dy in DIAGONALS + LINES:
        out.append(pattern(QUEEN, dx, dy))
        out.append(pattern(KING, dx, dy, range=1))
    # Castling: the king goes two cells towards a rook that has not moved, with the cells
    # between them empty and the king's three cells (start, crossed, arrival) not threatened;
    # the rook jumps to the cell the king crossed.
    out.append(pattern(KING, 1, 0, range=2, min_range=2, mode=1, first_move_only=True,
                       safe_path=True, companion_kind="rook", companion_from=3, companion_to=1))
    out.append(pattern(KING, -1, 0, range=2, min_range=2, mode=1, first_move_only=True,
                       safe_path=True, companion_kind="rook", companion_from=4, companion_to=1))
    # The pawns: a step ahead (mode 1: no capture), two on the first move, a capture on the
    # diagonal (mode 2: only with a capture), and the capture "in passing": onto the empty cell
    # behind a pawn that has just made a double step, which is taken from its own cell.
    for player, ahead in ((WHITE, -1), (BLACK, 1)):
        out.append(pattern(PAWN, 0, ahead, player=player, range=1, mode=1))
        out.append(pattern(PAWN, 0, ahead, player=player, range=2, min_range=2, mode=1,
                           first_move_only=True))
        for side in (-1, 1):
            out.append(pattern(PAWN, side, ahead, player=player, range=1, mode=2))
            out.append(pattern(PAWN, side, ahead, player=player, range=1, mode=1,
                               victim_kind="pawn", victim_dx=0, victim_dy=-ahead,
                               victim_min_distance=2))
    for index, item in enumerate(out):
        item["name"] = "pattern_%02d" % index
    return out


# --- scenes --------------------------------------------------------------------------------

DARK = {"red": 60, "green": 50, "blue": 40}


def text(string, size, x, y, anchor_x=0.0, anchor_y=0.0, pivot_x=0.0, pivot_y=0.0, **extra):
    """A text in window pixels, anchored to the window: it follows its size."""
    return {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": anchor_x, "anchor_y": anchor_y, "offset_x": x, "offset_y": y},
        "text2d.Text": dict({"string": string, "font": "font", "size": size, "screen": True,
                             "pivot_x": pivot_x, "pivot_y": pivot_y}, **extra),
    }


def shown_in(components, phases):
    return dict(components, **{"phase.ShowIn": {"phases": phases}})


def tagged(components, tag):
    return dict(components, **{"tag.Tag": {"tag": tag}})


def go(name, scene, **trigger):
    return {"name": name, "components": {"scene_switch.GoToScene": dict({"scene": scene},
                                                                        **trigger)}}


def paper():
    return {"name": "paper", "components": {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5},
        "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": 4000, "height": 4000,
                            "screen": True, "layer": -10, "red": 250, "green": 247, "blue": 239}}}


def squares():
    out = [{"name": "frame", "components": {
        "transform2d.Position": {},
        "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": SIZE * CELL + 16,
                            "height": SIZE * CELL + 16, "layer": -3,
                            "red": 90, "green": 62, "blue": 44}}}]
    for row in range(SIZE):
        for col in range(SIZE):
            x, y = centre(col, row)
            tint = ({"red": 181, "green": 136, "blue": 99} if (col + row) % 2
                    else {"red": 240, "green": 217, "blue": 181})
            out.append({"name": "square_%d_%d" % (col, row), "components": {
                "transform2d.Position": {"x": x, "y": y},
                "sprite2d.Sprite": dict({"atlas": "ui", "frame": 0, "width": CELL, "height": CELL,
                                         "layer": -2}, **tint)}})
    return out


def markers():
    """What the board shows on top of the squares: where the chosen piece can go, the chosen
    piece, the last move, a king in danger. They show and hide by themselves (grid_moves)."""
    out = []

    def mark(name, what, col, row, frame, size, **tint):
        x, y = centre(col, row)
        out.append({"name": name, "components": {
            "transform2d.Position": {"x": x, "y": y},
            "grid2d.GridPos": {"grid": "board", "col": col, "row": row},
            "grid_moves.Marker": {"grid": "board", "what": what},
            "sprite2d.Sprite": dict({"atlas": "ui", "frame": frame, "width": size,
                                     "height": size, "layer": 1}, **tint)}})

    for row in range(SIZE):
        for col in range(SIZE):
            mark("quiet_%d_%d" % (col, row), 0, col, row, 5, 20, red=40, green=120, blue=60,
                 alpha=170)
            mark("capture_%d_%d" % (col, row), 1, col, row, 6, CELL, red=200, green=50, blue=40,
                 alpha=200)
    mark("selected", 2, 0, 0, 0, CELL, red=250, green=220, blue=60, alpha=130)
    mark("last_from", 3, 0, 0, 0, CELL, red=130, green=200, blue=90, alpha=110)
    mark("last_to", 4, 0, 0, 0, CELL, red=130, green=200, blue=90, alpha=150)
    mark("danger", 5, 0, 0, 0, CELL, red=230, green=60, blue=50, alpha=170)
    return out


BACK_RANK = [ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK]


def start_position():
    """(name, kind, player, square): the name is the square the piece starts from."""
    out = []
    for col, kind in enumerate(BACK_RANK):
        file = chr(ord("a") + col)
        out.append((file + "1", kind, WHITE, file + "1"))
        out.append((file + "2", PAWN, WHITE, file + "2"))
        out.append((file + "7", PAWN, BLACK, file + "7"))
        out.append((file + "8", kind, BLACK, file + "8"))
    return out


def piece_entity(name, kind, player, square):
    col, row = cell_of(square)
    x, y = centre(col, row)
    components = {
        "transform2d.Position": {"x": x, "y": y},
        "grid2d.GridPos": {"col": col, "row": row},
        "turns.Team": {"player": player},
        "grid_moves.Mover": {"kind": KINDS[kind]},
        "sprite2d.FrameBind": {"first_frame": 6 * player},
    }
    if kind == KING:
        components["grid_moves.Guard"] = {}
    return {"name": name, "prefab": "piece", "components": components}


def rules_entities():
    out = [
        {"name": "board", "components": {
            "transform2d.Position": {},
            "grid2d.Grid": {"id": "board", "columns": SIZE, "rows": SIZE,
                            "cell_width": CELL, "cell_height": CELL}}},
        {"name": "camera", "components": {
            "transform2d.Position": {}, "camera2d.Camera": {"zoom": 1},
            "camera2d.CameraFit": {"width": AREA, "height": AREA, "margin_top": MARGIN_TOP,
                                   "margin_bottom": MARGIN_BOTTOM, "max_zoom": 1.5}}},
        # The turn: white first; moves pass it on (grid_moves.MoveRequest). The picture of its
        # player is shown by the sprite that carries it.
        {"name": "turn", "components": {
            "turns.Turn": {"players": 2},
            "transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 0.0, "anchor_y": 0.0, "offset_x": 34, "offset_y": 76},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 3, "width": 24, "height": 24,
                                "screen": True, "layer": 5},
            "sprite2d.FrameBind": {"field": "turns.Turn.current", "first_frame": 3,
                                   "frame_count": 2}}},
        # The only way in for a move: the picker fills it, a network opponent would too.
        {"name": "moves", "components": {"grid_moves.MoveRequest": {
            "grid": "board", "moved_action": "moved", "refused_action": "refused"}}},
        {"name": "cursor", "components": {
            "grid2d.GridCursor": {"grid": "board", "pointer_x": "pointer_x",
                                  "pointer_y": "pointer_y", "click": "click"},
            "phase.ActiveIn": {"phases": PLAYING}}},
        {"name": "picker", "components": {
            "grid_moves.Picker": {"grid": "board"},
            "phase.ActiveIn": {"phases": PLAYING}}},
        # What each side faces, as facts; the Watches below name them.
        {"name": "side_white", "components": tagged(
            {"grid_moves.Side": {"grid": "board", "player": WHITE}}, "side_white")},
        {"name": "side_black", "components": tagged(
            {"grid_moves.Side": {"grid": "board", "player": BLACK}}, "side_black")},
    ]
    for color, player in (("white", WHITE), ("black", BLACK)):
        tag = "side_" + color
        for name, status, action in (("mate", 3, color + "_mated"), ("stalemate", 2, "stalemate"),
                                     ("check", 1, "check_on"), ("clear", 0, "check_off")):
            out.append({"name": "%s_%s" % (color, name), "components": {"rules.Watch": {
                "own": False, "source": tag, "field": "grid_moves.Side.status",
                "min": status, "max": status, "action": action}}})
    # A pawn that reaches the far rank stops the game on the promotion phase, and the
    # menu's buttons say what it becomes (rules.OnAction on the piece the zone points to).
    for name, row, tag in (("promotion_top", 0, "promotion_top"),
                           ("promotion_bottom", SIZE - 1, "promotion_bottom")):
        out.append({"name": name, "components": tagged({"grid2d.Zone": {
            "grid": "board", "col": 0, "row": row, "columns": SIZE, "rows": 1,
            "match_field": "grid_moves.Mover.kind", "match_value": PAWN,
            "action": "need_promotion"}}, tag)})
    return out


PROMOTIONS = [("queen", QUEEN, "Queen"), ("rook", ROOK, "Rook"), ("bishop", BISHOP, "Bishop"),
              ("knight", KNIGHT, "Knight")]


def phase_entities():
    def transition(name, action, source, target):
        return {"name": name, "components": {"phase.Transition": {
            "action": action, "from_any": False, "from": source, "to": target}}}

    out = [{"name": "game_phase", "components": {"phase.Phase": {"current": "playing"}}},
           transition("to_promotion", "need_promotion", PLAYING, "promotion"),
           transition("to_black_won", "white_mated", PLAYING | PROMOTION, "black_won"),
           transition("to_white_won", "black_mated", PLAYING | PROMOTION, "white_won"),
           transition("to_draw", "stalemate", PLAYING | PROMOTION, "draw"),
           transition("white_resigns", "resign_white", PLAYING, "black_won"),
           transition("black_resigns", "resign_black", PLAYING, "white_won")]
    for name, _, _ in PROMOTIONS:
        out.append(transition("back_after_" + name, "promote_" + name, PROMOTION, "playing"))
        for tag in ("promotion_top", "promotion_bottom"):
            kind = {"queen": QUEEN, "rook": ROOK, "bishop": BISHOP, "knight": KNIGHT}[name]
            out.append({"name": "make_%s_%s" % (name, tag.split("_")[1]), "components": {
                "rules.OnAction": {"action": "promote_" + name, "source": tag,
                                   "link": "grid2d.Zone.entered",
                                   "field": "grid_moves.Mover.kind", "value": kind}}})
    return out


def interface_entities():
    out = [
        {"name": "title", "components": text("hud.title", 34, 22, 18, 0, 0, 0, 0, **DARK)},
        {"name": "to_move", "components": text("hud.to_move", 18, 66, 78, 0, 0, 0, 0.5, **DARK)},
        {"name": "check_text", "components": tagged(
            text("hud.check", 24, -22, 76, 1, 0, 1, 0.5, red=200, green=50, blue=40, alpha=0),
            "check_text")},
        {"name": "show_check", "components": {"rules.OnAction": {
            "action": "check_on", "source": "check_text", "field": "text2d.Text.alpha",
            "value": 255}}},
        {"name": "hide_check", "components": {"rules.OnAction": {
            "action": "check_off", "source": "check_text", "field": "text2d.Text.alpha",
            "value": 0}}},
        {"name": "help", "components": shown_in(
            text("hud.help", 17, 0, -150, 0.5, 1, 0.5, 0.5, **DARK), PLAYING)},
        {"name": "promotion_title", "components": shown_in(
            text("hud.promote", 24, 0, -150, 0.5, 1, 0.5, 0.5, **DARK), PROMOTION)},
        {"name": "white_wins", "components": shown_in(
            text("end.white", 26, 0, -150, 0.5, 1, 0.5, 0.5, red=60, green=120, blue=60),
            WHITE_WON)},
        {"name": "black_wins", "components": shown_in(
            text("end.black", 26, 0, -150, 0.5, 1, 0.5, 0.5, red=60, green=120, blue=60),
            BLACK_WON)},
        {"name": "draw_text", "components": shown_in(
            text("end.draw", 26, 0, -150, 0.5, 1, 0.5, 0.5, red=60, green=90, blue=150), DRAW)},
        # Only the pointer drives the buttons.
        {"name": "game_menu_input", "components": {"ui.Menu": {
            "group": "game_menu", "use_pointer": True, "pointer_x": "pointer_x",
            "pointer_y": "pointer_y", "click": "click"}}},
        {"name": "promotion_menu_input", "components": {"ui.Menu": {
            "group": "promotion_menu", "use_pointer": True, "pointer_x": "pointer_x",
            "pointer_y": "pointer_y", "click": "click"}}},
        {"name": "button_new", "prefab": "button", "components": {
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_y": -45},
            "ui.Button": {"order": 0, "action": "new_game"},
            "text2d.Text": {"string": "button.new"}}},
        {"name": "button_resign_white", "prefab": "button", "components": {
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_x": -108, "offset_y": -100},
            "ui.Button": {"order": 1, "action": "resign_white"},
            "text2d.Text": {"string": "button.resign_white", "size": 16},
            "phase.ShowIn": {"phases": PLAYING}}},
        {"name": "button_resign_black", "prefab": "button", "components": {
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_x": 108, "offset_y": -100},
            "ui.Button": {"order": 2, "action": "resign_black"},
            "text2d.Text": {"string": "button.resign_black", "size": 16},
            "phase.ShowIn": {"phases": PLAYING}}},
    ]
    for index, (name, _, _) in enumerate(PROMOTIONS):
        out.append({"name": "button_" + name, "prefab": "button", "components": {
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0,
                          "offset_x": (index - 1.5) * 112, "offset_y": -100},
            "sprite2d.Sprite": {"width": 104},
            "ui.Button": {"menu": "promotion_menu", "order": index, "width": 104,
                          "action": "promote_" + name},
            "text2d.Text": {"string": "button." + name, "size": 18},
            "phase.ShowIn": {"phases": PROMOTION}}})
    out.append(go("new_game", "game", use_action=True, action="new_game"))
    return out


def scene(pieces=None):
    """The game (pieces None: the start position) or a bench with the given pieces, given as
    (name, kind, player, square)."""
    placed = start_position() if pieces is None else pieces
    out = [paper()] + squares() + markers() + rules_entities() + phase_entities()
    out += patterns() + interface_entities()
    out += [piece_entity(*item) for item in placed]
    return out


def scenes():
    kings = [("wk", KING, WHITE, "e1"), ("bk", KING, BLACK, "e8")]
    return {
        "game": scene(),
        # A knight shielding its king from a rook: it is pinned. A second rook watches the d file.
        "test_pin": scene([("wk", KING, WHITE, "e1"), ("wn", KNIGHT, WHITE, "e2"),
                           ("br", ROOK, BLACK, "e8"), ("br2", ROOK, BLACK, "d8"),
                           ("bk", KING, BLACK, "h8")]),
        # A rook that is about to give check, a pawn that cannot answer it, and a free square.
        "test_check": scene([("wk", KING, WHITE, "e1"), ("wp", PAWN, WHITE, "a2"),
                             ("br", ROOK, BLACK, "h8"), ("bk", KING, BLACK, "g7")]),
        # White to play Qc7: black has no move and is not in check.
        "test_stalemate": scene([("wk", KING, WHITE, "b6"), ("wq", QUEEN, WHITE, "e7"),
                                 ("bk", KING, BLACK, "a8")]),
        # Both kings and the four rooks, nothing between: castling on both sides.
        "test_castle": scene(kings + [("wr_a", ROOK, WHITE, "a1"), ("wr_h", ROOK, WHITE, "h1"),
                                      ("br_a", ROOK, BLACK, "a8"), ("br_h", ROOK, BLACK, "h8")]),
        # A black rook watches f1: castling short is not allowed (the king crosses it).
        "test_castle_threat": scene([("wk", KING, WHITE, "e1"), ("wr_a", ROOK, WHITE, "a1"),
                                     ("wr_h", ROOK, WHITE, "h1"), ("bk", KING, BLACK, "e8"),
                                     ("br", ROOK, BLACK, "f8")]),
        # A pawn on its fifth rank beside the starting cell of a black pawn.
        "test_en_passant": scene(kings + [("wp", PAWN, WHITE, "e5"), ("bp", PAWN, BLACK, "d7")]),
        # A white pawn one step from the far rank, and a black one from its own.
        "test_promotion": scene(kings + [("wp", PAWN, WHITE, "a7"), ("bp", PAWN, BLACK, "h2")]),
        # A promotion that is a mate: Qa8 against the king h8 shut in by its pawn and a rook.
        "test_promotion_mate": scene([("wk", KING, WHITE, "e1"), ("wp", PAWN, WHITE, "a7"),
                                      ("wr", ROOK, WHITE, "g1"), ("bk", KING, BLACK, "h8"),
                                      ("bp", PAWN, BLACK, "h7")]),
    }


# --- scenarios -----------------------------------------------------------------------------

def tap(tick, action):
    return [{"tick": tick, "action": action, "value": 1},
            {"tick": tick + 1, "action": action, "value": 0}]


def touch(tick, x, y):
    return [{"tick": tick - 1, "action": "pointer_x", "value": x},
            {"tick": tick - 1, "action": "pointer_y", "value": y}] + tap(tick, "click")


def moves(*items, start=10, gap=12):
    """The touches of a game: each move is a touch on a square, then on another, `gap` ticks
    apart (the pieces are given as "e2e4"). Returns the inputs and the tick after the last."""
    inputs = []
    tick = start
    for item in items:
        inputs += touch(tick, *pixel_of(item[:2]))
        inputs += touch(tick + gap // 2, *pixel_of(item[2:4]))
        tick += gap
    return inputs, tick


def at_tick(tick, *checks):
    return [{"at": tick, "check": check} for check in checks]


def scenario(name, comment, ticks, inputs, expect, scene_name=None):
    data = {"schema_version": 1, "name": name}
    if scene_name:
        data["scene"] = scene_name
    data.update({"ticks": ticks, "inputs": inputs, "expect": expect})
    return name, comment, data


def on(entity, square):
    col, row = cell_of(square)
    return ["%s.grid2d.GridPos.col == %d" % (entity, col), "%s.grid2d.GridPos.row == %d" % (entity, row)]


PHASE = "game_phase.phase.Phase.current == %d"


def scenarios():
    game_size = len(scene())
    out = []

    def add(*args, **kwargs):
        out.append(scenario(*args, **kwargs))

    add("start_position",
        "The game starts with the 32 pieces on their squares, white to move, nobody threatened.",
        20, [],
        at_tick(20, "entities == %d" % game_size, "turn.turns.Turn.current == 0",
                "side_white.grid_moves.Side.status == 0",
                "side_black.grid_moves.Side.status == 0", PHASE % 0,
                "e1.grid_moves.Mover.kind == 5", "d8.grid_moves.Mover.kind == 4",
                "e1.sprite2d.Sprite.frame == 5", "e8.sprite2d.Sprite.frame == 11") +
        at_tick(20, *on("e2", "e2")),
        "game")

    inputs, end = moves("e2e4", "e7e5")
    add("pawn_moves_one_or_two",
        "Touch a piece, then a square it can go to: a pawn goes two squares on its first move,\n"
        "and the turn passes to the other player each time.",
        end + 10, inputs,
        at_tick(20, *on("e2", "e4"), "turn.turns.Turn.current == 1",
                "e2.grid_moves.Mover.moves_made == 1") +
        at_tick(end + 10, *on("e7", "e5"), "turn.turns.Turn.current == 0",
                "moves.grid_moves.MoveRequest.moves == 2"),
        "game")

    inputs, end = moves("e7e5", "g1f3")
    add("only_the_player_to_move_plays",
        "Touching a black piece on white's turn selects nothing: it does not move. A white piece\n"
        "does.",
        end + 10, inputs,
        at_tick(end - 12, *on("e7", "e7"), "moves.grid_moves.MoveRequest.moves == 0",
                "turn.turns.Turn.current == 0") +
        at_tick(end + 10, *on("e7", "e7"), "moves.grid_moves.MoveRequest.moves == 1",
                "turn.turns.Turn.current == 1", *on("g1", "f3")),
        "game")

    inputs, end = moves("b1b3", "b1c3")
    add("illegal_moves_are_not_played",
        "A square that is not one of the piece's moves does nothing (and lets the piece go): a\n"
        "knight does not go straight ahead, but goes to c3.",
        end + 10, inputs,
        at_tick(16, *on("b1", "b1"), "moves.grid_moves.MoveRequest.moves == 0",
                "turn.turns.Turn.current == 0") +
        at_tick(end + 10, *on("b1", "c3"), "moves.grid_moves.MoveRequest.moves == 1"),
        "game")

    inputs, end = moves("e2c3", "e1d1", "e1f1")
    add("a_pinned_piece_cannot_move",
        "A knight that shields its king from a rook cannot move (that would leave the king\n"
        "threatened), and the king cannot step onto a threatened square, but can step aside.",
        end + 10, inputs,
        at_tick(end - 12, *on("wn", "e2"), *on("wk", "e1"),
                "moves.grid_moves.MoveRequest.moves == 0") +
        at_tick(end + 10, *on("wk", "f1"), "moves.grid_moves.MoveRequest.moves == 1"),
        "test_pin")

    inputs, end = moves("a2a3", "h8e8", "a3a4", "e1d1")
    add("a_check_must_be_answered",
        "A rook gives check: \"Check!\" is shown, a move that does not answer it is not played\n"
        "(the pawn stays), and stepping out of the check is allowed and takes the text away.",
        end + 10, inputs,
        at_tick(34, "check_text.text2d.Text.alpha == 255", "side_white.grid_moves.Side.status == 1",
                "turn.turns.Turn.current == 0") +
        at_tick(end - 10, *on("wp", "a3"), "moves.grid_moves.MoveRequest.moves == 2") +
        at_tick(end + 10, *on("wk", "d1"), "check_text.text2d.Text.alpha == 0",
                "side_white.grid_moves.Side.status == 0", "moves.grid_moves.MoveRequest.moves == 3"),
        "test_check")

    inputs, end = moves("f2f3", "e7e5", "g2g4", "d8h4")
    add("fools_mate",
        "The shortest game: black mates in two moves. The game is over, and the board stops\n"
        "answering.",
        end + 20, inputs + moves("a2a3", start=end + 4)[0],
        at_tick(end + 10, PHASE % 3, "side_white.grid_moves.Side.status == 3") +
        at_tick(end + 20, *on("a2", "a2")),
        "game")

    inputs, end = moves("e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7")
    add("scholars_mate",
        "White mates on f7 with the queen: the black pawn there is taken, white wins.",
        end + 10, inputs,
        at_tick(end + 10, PHASE % 2, "f7 gone", "side_black.grid_moves.Side.status == 3",
                *on("d1", "f7")),
        "game")

    inputs, end = moves("e7c7")
    add("stalemate_is_a_draw",
        "Black has no move and is not in check: the game is a draw.",
        end + 10, inputs,
        at_tick(end + 10, PHASE % 4, "side_black.grid_moves.Side.status == 2"),
        "test_stalemate")

    inputs, end = moves("e1g1", "e8c8")
    add("castling_on_both_sides",
        "Castling short for white, long for black: the king goes two squares and the rook\n"
        "jumps over it.",
        end + 10, inputs,
        at_tick(end + 10, *on("wk", "g1"), *on("wr_h", "f1"), *on("bk", "c8"), *on("br_a", "d8"),
                "wk.grid_moves.Mover.moves_made == 1", "wr_h.grid_moves.Mover.moves_made == 1"),
        "test_castle")

    inputs, end = moves("e1g1", "e1c1", "e1g1")
    add("no_castling_through_a_threatened_square",
        "The king may not castle across a square a rook watches (f1), but may on the other side.",
        end + 10, inputs,
        at_tick(16, *on("wk", "e1"), "moves.grid_moves.MoveRequest.moves == 0") +
        at_tick(end + 10, *on("wk", "c1"), *on("wr_a", "d1")),
        "test_castle_threat")

    inputs, end = moves("h1h2", "e8d8", "h2h1", "d8e8", "e1g1")
    add("no_castling_after_the_rook_moved",
        "A rook that has moved (even back to its square) can no longer castle.",
        end + 10, inputs,
        at_tick(end + 10, *on("wk", "e1"), *on("wr_h", "h1"),
                "moves.grid_moves.MoveRequest.moves == 4"),
        "test_castle")

    inputs, end = moves("e1d1", "d7d5", "e5d6")
    add("en_passant",
        "A pawn that has just made a double step next to an enemy pawn is taken as if it had\n"
        "made one: the capturing pawn lands behind it.",
        end + 10, inputs,
        at_tick(end + 10, "bp gone", *on("wp", "d6")),
        "test_en_passant")

    inputs, end = moves("e1d1", "d7d5", "d1e1", "e8d8", "e5d6")
    add("en_passant_only_right_away",
        "The capture in passing is possible at once or never: after another move the pawn stays.",
        end + 10, inputs,
        at_tick(end + 10, "bp alive", *on("wp", "e5"), *on("bp", "d5")),
        "test_en_passant")

    for name, kind, label in PROMOTIONS:
        index = [item[0] for item in PROMOTIONS].index(name)
        button = (round(WIDTH / 2 + (index - 1.5) * 112), HEIGHT - 100)
        inputs, end = moves("a7a8")
        add("promotion_to_" + name,
            "A pawn that reaches the far rank stops the game on a choice of four pieces; the\n"
            "button of the %s turns it into one, and the game goes on." % name,
            end + 40, inputs + touch(end + 10, *button),
            at_tick(end + 5, PHASE % 1, *on("wp", "a8")) +
            at_tick(end + 40, "wp.grid_moves.Mover.kind == %d" % kind, PHASE % 0,
                    "wp.sprite2d.Sprite.frame == %d" % kind, "turn.turns.Turn.current == 1"),
            "test_promotion")

    inputs, end = moves("e1d1", "h2h1")
    index = 0
    button = (round(WIDTH / 2 + (index - 1.5) * 112), HEIGHT - 100)
    add("black_promotes_too",
        "The far rank of black is the first one: its pawn is promoted the same way.",
        end + 40, inputs + touch(end + 10, *button),
        at_tick(end + 5, PHASE % 1) +
        at_tick(end + 40, "bp.grid_moves.Mover.kind == 4", PHASE % 0, *on("bp", "h1")),
        "test_promotion")

    inputs, end = moves("a7a8")
    button = (round(WIDTH / 2 + (0 - 1.5) * 112), HEIGHT - 100)
    add("promotion_can_mate",
        "The new queen is looked at once it exists: promoting to a queen here is a mate.",
        end + 60, inputs + touch(end + 10, *button),
        at_tick(end + 60, PHASE % 2, "side_black.grid_moves.Side.status == 3"),
        "test_promotion_mate")

    new_button = (WIDTH // 2, HEIGHT - 45)
    inputs, end = moves("e2e4")
    add("new_game_with_the_button",
        "New game starts again from the first position at any time, with the button.",
        end + 40, inputs + touch(end + 10, *new_button),
        at_tick(end + 40, "scene is game", *on("e2", "e2"), "turn.turns.Turn.current == 0",
                "moves.grid_moves.MoveRequest.moves == 0"),
        "game")

    inputs, end = moves("e2e4")
    add("new_game_with_r",
        "R does the same.",
        end + 40, inputs + tap(end + 10, "new_game"),
        at_tick(end + 40, *on("e2", "e2"), "turn.turns.Turn.current == 0"),
        "game")

    inputs, end = moves("e2e4")
    resign_black = (round(WIDTH / 2 + 108), HEIGHT - 100)
    add("resigning_loses",
        "A player can resign: the other one wins.",
        end + 30, inputs + touch(end + 10, *resign_black),
        at_tick(end + 30, PHASE % 2),
        "game")
    return out


HEADER = """// "Two players, one board."
// Chess for two players on one screen: a touch (or a click) on a piece shows where it can go,
// a second one plays the move. Every rule is data: the way each piece moves is an entity (a
// Pattern of module grid_moves), a king is a Guard (a move that leaves it threatened is not
// offered, and "check", "mate" and "stalemate" are what the sides report), castling and the
// capture in passing are patterns with a condition, and the choice of a piece when a pawn
// reaches the far rank is a phase of the game with a menu. No game code.
// Pictures, prefabs, scenes and the manifest are generated by tools/make_assets.py.
"""


def write_manifest(scenes_list, scenarios_list):
    data = {
        "schema_version": 1,
        "name": "chess",
        "title": "Chess",
        "description": "Chess for two players on one screen. Touch a piece, then a square it can go "
                       "to. Check, mate, stalemate, castling, the capture in passing and the "
                       "promotion of pawns are all there. R or the button starts a new game.",
        "modules": ["transform2d", "collision2d", "camera2d", "grid2d", "turns", "grid_moves",
                    "rules", "phase", "sprite2d", "text2d", "scene_switch", "ui"],
        "simulation": {"tick_rate": 60, "seed": 1},
        "window": {"width": WIDTH, "height": HEIGHT, "adapt": True},
        "input": "input.json",
        "lists": {
            "grid": ["board"],
            "phase": PHASES,
            "mover_kind": KINDS,
            "action": ["moved", "refused", "white_mated", "black_mated", "stalemate", "check_on",
                       "check_off", "need_promotion", "promote_queen", "promote_rook",
                       "promote_bishop", "promote_knight", "resign_white", "resign_black",
                       "new_game"],
            "menu": ["game_menu", "promotion_menu"],
            "tag": ["side_white", "side_black", "check_text", "promotion_top",
                    "promotion_bottom"],
        },
        "atlases": ["art/pieces.json", "art/ui.json", "art/font.json"],
        "strings": ["text/en.json", "text/fr.json"],
        "locale": "en",
        "prefabs": ["prefabs/piece.json", "prefabs/button.json"],
        "scenes": scenes_list,
        "scenarios": scenarios_list,
        "start_scene": "game",
    }
    with open(os.path.join(GAME, "game.json"), "w") as out:
        out.write(HEADER)
        json.dump(data, out, indent=2)
        out.write("\n")


def main():
    cells = []
    for color in (rgba("f6f1e3"), rgba("33333d")):
        outline = rgba("3a2f26") if color[0] > 128 else rgba("0c0c12")
        for kind in range(6):
            cells.append(draw_piece(kind, color, outline))
    write_atlas("pieces", cells, 6, P)
    # Cells of "ui": 0 white, 1 button, 2 button with the focus, 3 and 4 white and black
    # swatches (whose turn it is), 5 a disc, 6 a ring.
    write_atlas("ui", [solid(rgba("ffffff")), solid(rgba("8f7a66")), solid(rgba("f67c5f")),
                       swatch(rgba("f6f1e3"), rgba("3a2f26")), swatch(rgba("33333d"), rgba("0c0c12")),
                       disc(7), disc(7.5, 1.6)], 7, 16)
    for name, (comment, data) in prefabs().items():
        write_json(os.path.join(GAME, "prefabs", name + ".json"), data, comment)
    for name, entities in scenes().items():
        write_json(os.path.join(GAME, "scenes", name + ".json"),
                   {"schema_version": 1, "name": name, "entities": entities})
    names = []
    for name, comment, data in scenarios():
        write_json(os.path.join(GAME, "scenarios", name + ".json"), data, comment)
        names.append(name)
    write_manifest(["scenes/%s.json" % name for name in scenes()],
                   ["scenarios/%s.json" % name for name in names])
    print("wrote %d scenes and %d scenarios" % (len(scenes()), len(names)))


if __name__ == "__main__":
    main()
