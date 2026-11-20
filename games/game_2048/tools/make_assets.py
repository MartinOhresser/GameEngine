#!/usr/bin/env python3
"""Rebuilds the generated files of game_2048: the atlas pictures, the prefabs, the scenes and the
scenarios.

    python3 games/game_2048/tools/make_assets.py

The pictures are drawn here, pixel by pixel, so the game ships no binary that cannot be
reviewed (the font is the one of doodle_jump, made by scripts/make_font.py). Scenes and
scenarios are written here because the game and its test benches share most of their lines.

The test benches (scenes/test_*.json) are the real game with the tiles put where a rule is
to be tested, and without the random part (no tile is dealt, and none is added after a move
unless the bench is about that), so that each rule is checked on a known board.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
from make_png import write_png  # noqa: E402

GAME = os.path.join(HERE, "..")
CELL = 100  # the size of a cell of the board, in world units
TILE = 90  # the size of a tile
COLUMNS = 4
ROWS = 4

# The phases of the game, as bits of the sets of phase.ShowIn / phase.ActiveIn.
PLAYING, WON, LOST = 1, 2, 4


# --- pictures ------------------------------------------------------------------------------

def rgba(hex_color, alpha=255):
    return [int(hex_color[i: i + 2], 16) for i in (0, 2, 4)] + [alpha]


CLEAR = [0, 0, 0, 0]

# The cell n of "tiles" is the picture of the tile worth 2 to the power n; 0 is an empty cell.
# The colours are light enough for dark digits to read on all of them.
PALETTE = ["cdc1b4", "eee4da", "ede0c8", "f2b179", "f59563", "f67c5f", "f65e3b", "edcf72",
           "edcc61", "edc850", "edc53f", "edc22e", "b7e07a", "7ad1e0"]
SIZE = 32


def rounded(color, size, radius):
    """A square with rounded corners; its lowest rows are a little darker."""
    base = rgba(color)
    shade = [int(c * 0.88) for c in base[:3]] + [255]
    tile = [[CLEAR] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            dx = max(radius - x, x - (size - 1 - radius), 0)
            dy = max(radius - y, y - (size - 1 - radius), 0)
            if dx * dx + dy * dy <= radius * radius:
                tile[y][x] = shade if y >= size - 3 else base
    return tile


def solid(color, size=16):
    return [[color] * size for _ in range(size)]


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


def write_atlas(name, cells, columns, size):
    save_png(os.path.join(GAME, "art", name + ".png"), atlas(cells, columns, size))
    write_json(os.path.join(GAME, "art", name + ".json"),
               {"schema_version": 1, "name": name, "image": name + ".png",
                "cell_width": size, "cell_height": size})


def write_json(path, data, comment=None):
    with open(path, "w") as out:
        if comment:
            for line in comment.strip().splitlines():
                out.write("// " + line.strip() + "\n")
        json.dump(data, out, indent=2)
        out.write("\n")


# --- prefabs -------------------------------------------------------------------------------

def prefabs():
    return {
        "tile": ("A tile. It sits on a cell of the board (it takes the one it is created on),\n"
                 "glides when the board slides, appears small and grows to its size, and shows\n"
                 "its value with the picture of its colour and its digits.", {
                     "schema_version": 1, "name": "tile", "components": {
                         "transform2d.Position": {},
                         "transform2d.Scale": {"factor": 0.2},
                         "tween2d.Settle": {"field": "transform2d.Scale.factor", "toward": 1,
                                            "rate": 0.3},
                         "grid2d.GridPos": {"grid": "board", "glide": 0.5, "from_position": True},
                         "slide_merge.Tile": {"value": 2},
                         "sprite2d.Sprite": {"atlas": "tiles", "frame": 1, "width": TILE,
                                             "height": TILE, "layer": 1},
                         "sprite2d.FrameBind": {"field": "slide_merge.Tile.value", "log2": True,
                                                "frame_count": len(PALETTE)},
                         "text2d.Text": {"string": "tile.value", "font": "font", "size": 34,
                                         "pivot_x": 0.5, "pivot_y": 0.5, "red": 90, "green": 80,
                                         "blue": 70},
                         "ui.Bind": {"field": "slide_merge.Tile.value", "own": True}}}),
        "tile_four": ("The tile that is sometimes dealt instead of a 2.", {
            "schema_version": 1, "name": "tile_four", "extends": "tile",
            "components": {"slide_merge.Tile": {"value": 4}}}),
        "button": ("A button under the board: a picture and its text.", {
            "schema_version": 1, "name": "button", "components": {
                "transform2d.Position": {},
                "sprite2d.Sprite": {"atlas": "ui", "frame": 1, "width": 220, "height": 44,
                                    "screen": True, "layer": 10},
                "text2d.Text": {"font": "font", "size": 22, "pivot_x": 0.5, "pivot_y": 0.5,
                                "screen": True},
                "ui.Button": {"menu": "game_menu", "width": 220, "height": 44, "normal_frame": 1,
                              "focus_frame": 2}}}),
    }


# --- scenes --------------------------------------------------------------------------------

BROWN = {"red": 119, "green": 110, "blue": 101}


def text(string, size, x, y, anchor_x=0.0, anchor_y=0.0, pivot_x=0.0, pivot_y=0.0, **extra):
    """A text in window pixels, anchored to the window: it follows its size."""
    return {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": anchor_x, "anchor_y": anchor_y, "offset_x": x, "offset_y": y},
        "text2d.Text": dict({"string": string, "font": "font", "size": size, "screen": True,
                             "pivot_x": pivot_x, "pivot_y": pivot_y}, **extra),
    }


def bound(components, source, field):
    return dict(components, **{"ui.Bind": {"source": source, "field": field}})


def shown_in(components, phases):
    return dict(components, **{"phase.ShowIn": {"phases": phases}})


def go(name, scene, **trigger):
    return {"name": name, "components": {"scene_switch.GoToScene": dict({"scene": scene},
                                                                        **trigger)}}


def paper():
    return {"name": "paper", "components": {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5},
        "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": 4000, "height": 4000,
                            "screen": True, "layer": -10, "red": 250, "green": 248, "blue": 239}}}


def scoreboard():
    """Follows the score of the board: keeps the best (saved) and stays from scene to scene."""
    return {"name": "scoreboard", "components": {
        "scene_switch.Persistent": {},
        "tag.Tag": {"tag": "scoreboard"},
        "records.Record": {"key": "best_score", "source": "board",
                           "field": "slide_merge.SlideBoard.score"}}}


def boot_scene():
    """Only runs once: it makes the scoreboard that stays for the whole session."""
    return [scoreboard(), go("go_game", "game", after_ticks=2)]


def at(col, row):
    return (col - (COLUMNS - 1) / 2) * CELL, (row - (ROWS - 1) / 2) * CELL


def board_entities():
    """The picture of the board: a plate and an empty cell under every tile."""
    out = [{"name": "plate", "components": {
        "transform2d.Position": {},
        "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": COLUMNS * CELL + 12,
                            "height": ROWS * CELL + 12, "layer": -2,
                            "red": 187, "green": 173, "blue": 160}}}]
    for row in range(ROWS):
        for col in range(COLUMNS):
            x, y = at(col, row)
            out.append({"name": "cell_%d_%d" % (col, row), "components": {
                "transform2d.Position": {"x": x, "y": y},
                "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": TILE, "height": TILE,
                                    "layer": -1, "red": 205, "green": 193, "blue": 180}}})
    return out


def rules_entities(deal, spawn):
    out = [
        {"name": "board", "components": {
            "transform2d.Position": {},
            "grid2d.Grid": {"id": "board", "columns": COLUMNS, "rows": ROWS,
                            "cell_width": CELL, "cell_height": CELL},
            "slide_merge.SlideBoard": {"grid": "board", "left": "left", "right": "right",
                                       "up": "up", "down": "down", "mode": 0,
                                       "moved_action": "moved", "stuck_action": "stuck"},
            "tag.Tag": {"tag": "board"},
            # The board only works while the game is being played (not on the victory or the
            # "no more moves" screen).
            "phase.ActiveIn": {"phases": PLAYING}}},
        {"name": "swipe", "components": {"gesture.Swipe": {
            "pointer_x": "pointer_x", "pointer_y": "pointer_y", "press": "click",
            "min_distance": 30, "max_ticks": 45,
            "left": "left", "right": "right", "up": "up", "down": "down"}}},
        {"name": "camera", "components": {
            "transform2d.Position": {}, "camera2d.Camera": {"zoom": 1},
            "camera2d.CameraFit": {"width": COLUMNS * CELL + 60, "height": ROWS * CELL + 60,
                                   "margin_top": 100, "margin_bottom": 175, "max_zoom": 1.6}}},
        # The game: the phase, and what moves it from one to another.
        {"name": "game_phase", "components": {"phase.Phase": {"current": "playing"}}},
        {"name": "to_won", "components": {"phase.Transition": {
            "action": "goal", "from_any": False, "from": PLAYING, "to": "won"}}},
        {"name": "to_lost", "components": {"phase.Transition": {
            "action": "stuck", "from_any": False, "from": PLAYING, "to": "lost"}}},
        {"name": "to_playing", "components": {"phase.Transition": {
            "action": "continue_game", "from_any": False, "from": WON, "to": "playing"}}},
        # 2048 is reached once: the victory screen is shown that time only.
        {"name": "goal", "components": {"rules.Watch": {
            "own": False, "source": "board", "field": "slide_merge.SlideBoard.highest",
            "min": 2048, "action": "goal", "once": True}}},
        go("new_game", "game", use_action=True, action="new_game"),
    ]
    if deal:
        # At the start of a game the board has not moved yet: that deals the first two tiles.
        out.append({"name": "start", "components": {"rules.Watch": {
            "own": False, "source": "board", "field": "slide_merge.SlideBoard.moves",
            "min": 0, "max": 0, "action": "deal", "once": True}}})
        out.append({"name": "dealer", "components": {"grid2d.SpawnOnEmpty": {
            "grid": "board", "trigger": "deal", "prefab": "tile", "alt_prefab": "tile_four",
            "alt_percent": 10, "count": 2}}})
    if spawn:
        out.append({"name": "adder", "components": {"grid2d.SpawnOnEmpty": {
            "grid": "board", "trigger": "moved", "prefab": "tile", "alt_prefab": "tile_four",
            "alt_percent": 10, "count": 1}}})
    return out


def interface_entities():
    dark = BROWN
    out = [
        {"name": "hud_title", "components": text("hud.title", 44, 22, 22, 0, 0, 0, 0, **dark)},
        {"name": "hud_score", "components": bound(
            text("hud.score", 22, -22, 24, 1, 0, 1, 0, **dark),
            "board", "slide_merge.SlideBoard.score")},
        {"name": "hud_best", "components": bound(
            text("hud.best", 22, -22, 58, 1, 0, 1, 0, **dark),
            "scoreboard", "records.Record.best")},
        {"name": "help", "components": shown_in(
            text("hud.help", 18, 0, -150, 0.5, 1, 0.5, 0.5, **dark), PLAYING)},
        {"name": "won_title", "components": shown_in(
            text("won.title", 30, 0, -150, 0.5, 1, 0.5, 0.5, red=230, green=150, blue=20), WON)},
        {"name": "lost_title", "components": shown_in(
            text("lost.title", 30, 0, -150, 0.5, 1, 0.5, 0.5, red=200, green=60, blue=50), LOST)},
        # Only the pointer drives the buttons: the keys are the game's (R, Enter).
        {"name": "game_menu_input", "components": {"ui.Menu": {
            "group": "game_menu", "use_pointer": True, "pointer_x": "pointer_x",
            "pointer_y": "pointer_y", "click": "click"}}},
        {"name": "button_new", "prefab": "button", "components": {
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_y": -45},
            "ui.Button": {"order": 0, "action": "new_game"},
            "text2d.Text": {"string": "button.new"}}},
        {"name": "button_continue", "prefab": "button", "components": {
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 1.0, "offset_y": -100},
            "ui.Button": {"order": 1, "action": "continue_game"},
            "text2d.Text": {"string": "button.continue"},
            "phase.ShowIn": {"phases": WON}}},
    ]
    return out


def scene(tiles=None, deal=True, spawn=True, with_scoreboard=False):
    """The game (tiles None) or a bench: the same scene with the given tiles on the board.
    Tiles are (name, column, row, value)."""
    out = [paper()] + board_entities() + rules_entities(deal, spawn) + interface_entities()
    for name, col, row, value in tiles or []:
        x, y = at(col, row)
        out.append({"name": name, "prefab": "tile", "components": {
            "transform2d.Position": {"x": x, "y": y},
            "transform2d.Scale": {"factor": 1},
            "grid2d.GridPos": {"col": col, "row": row, "from_position": False},
            "slide_merge.Tile": {"value": value}}})
    if with_scoreboard:
        out.append(scoreboard())  # the benches start without the boot scene
    return out


def checker(size, odd, even):
    """The tiles of a full board with no two equal tiles side by side."""
    return [("t%d" % (row * size + col + 1), col, row, odd if (col + row) % 2 else even)
            for row in range(size) for col in range(size)]


def scenes():
    return {
        "boot": boot_scene(),
        "game": scene(),
        # 2 2 4 on the top row.
        "test_merge": scene([("t1", 0, 0, 2), ("t2", 1, 0, 2), ("t3", 2, 0, 4)],
                            deal=False, spawn=False, with_scoreboard=True),
        # 2 2 2 2 on the top row.
        "test_four_equal": scene([("t1", 0, 0, 2), ("t2", 1, 0, 2), ("t3", 2, 0, 2),
                                  ("t4", 3, 0, 2)], deal=False, spawn=False),
        # One tile, that goes round the board.
        "test_walk": scene([("t1", 1, 1, 2)], deal=False, spawn=False),
        # 2 4 against the left edge; a move to the left changes nothing there.
        "test_packed": scene([("t1", 0, 0, 2), ("t2", 1, 0, 4)], deal=False, spawn=True),
        # One tile in a corner, and the tile that is added after each move.
        "test_spawn": scene([("t1", 3, 3, 2)], deal=False, spawn=True),
        # 1024 1024: one move makes 2048.
        "test_win": scene([("t1", 0, 0, 1024), ("t2", 1, 0, 1024)], deal=False, spawn=False),
        # A full board where no move is possible.
        "test_lost": scene(checker(4, 4, 2), deal=False, spawn=False),
    }


# --- scenarios -----------------------------------------------------------------------------

def tap(tick, action):
    return [{"tick": tick, "action": action, "value": 1},
            {"tick": tick + 1, "action": action, "value": 0}]


def pointer(tick, x, y):
    return [{"tick": tick, "action": "pointer_x", "value": x},
            {"tick": tick, "action": "pointer_y", "value": y}]


def click_at(tick, x, y):
    return pointer(tick - 1, x, y) + tap(tick, "click")


def drag(tick, x0, y0, x1, y1, lasting=6):
    return (pointer(tick, x0, y0) + [{"tick": tick, "action": "click", "value": 1}] +
            pointer(tick + lasting, x1, y1) + [{"tick": tick + lasting, "action": "click",
                                                "value": 0}])


def scenario(name, comment, ticks, inputs, expect, scene_name=None):
    data = {"schema_version": 1, "name": name}
    if scene_name:
        data["scene"] = scene_name
    data.update({"ticks": ticks, "inputs": inputs, "expect": expect})
    return name, comment, data


def at_tick(tick, *checks):
    return [{"at": tick, "check": check} for check in checks]


def scenarios():
    game_size = len(scene())
    packed_size = len(scenes()["test_packed"])
    spawn_size = len(scenes()["test_spawn"])
    # The window of the game is 480 by 720: the buttons are at the bottom, in the middle.
    new_button = (240, 720 - 45)
    continue_button = (240, 720 - 100)
    return [
        scenario(
            "deal_two_tiles",
            "A new game starts with two tiles on the board: a 2 or a 4 each.",
            10, [],
            at_tick(10, "entities == %d" % (game_size + 2),
                    "board.slide_merge.SlideBoard.highest >= 2",
                    "board.slide_merge.SlideBoard.highest <= 4",
                    "board.slide_merge.SlideBoard.moves == 0",
                    "game_phase.phase.Phase.current == 0"),
            "game"),
        scenario(
            "tiles_merge",
            "Slide to the left: the tiles go to the edge, two equal ones make their sum, the one\n"
            "that joins the other vanishes, and the score counts the new tile.",
            40, tap(5, "left"),
            at_tick(10, "t1.slide_merge.Tile.value == 4", "t2.slide_merge.Tile.dying == true",
                    "t2.grid2d.GridPos.col == 0", "t3.grid2d.GridPos.col == 1",
                    "t3.grid2d.GridPos.row == 0",
                    "board.slide_merge.SlideBoard.score == 4",
                    "board.slide_merge.SlideBoard.moves == 1",
                    "t1.sprite2d.Sprite.frame == 2", "t1.text2d.Text.value == 4") +
            at_tick(40, "t2 gone", "t1 alive", "entities == %d" % (len(scenes()["test_merge"]) - 1)),
            "test_merge"),
        scenario(
            "merged_tile_pops",
            "The tile that has just been made swells, then comes back to its size.",
            80, tap(5, "left"),
            at_tick(8, "t1.transform2d.Scale.factor > 1.05") +
            at_tick(80, "t1.transform2d.Scale.factor == 1"),
            "test_merge"),
        scenario(
            "merges_once_per_move",
            "2 2 2 2 gives 4 4, not 8: a tile that has just been made does not merge again in\n"
            "the same move.",
            40, tap(5, "left"),
            at_tick(40, "t1.slide_merge.Tile.value == 4", "t3.slide_merge.Tile.value == 4",
                    "t3.grid2d.GridPos.col == 1", "t2 gone", "t4 gone",
                    "board.slide_merge.SlideBoard.score == 8"),
            "test_four_equal"),
        scenario(
            "slides_in_four_directions",
            "Right, down, left and up (the arrow keys; W A S D and a swipe raise the same\n"
            "actions): a lone tile goes to the edge each time.",
            60, tap(5, "right") + tap(15, "down") + tap(25, "left") + tap(35, "up"),
            at_tick(12, "t1.grid2d.GridPos.col == 3", "t1.grid2d.GridPos.row == 1") +
            at_tick(22, "t1.grid2d.GridPos.row == 3") +
            at_tick(32, "t1.grid2d.GridPos.col == 0") +
            at_tick(60, "t1.grid2d.GridPos.row == 0", "board.slide_merge.SlideBoard.moves == 4"),
            "test_walk"),
        scenario(
            "move_without_effect_adds_nothing",
            "A move that changes nothing is not a move: no tile is added and it is not counted.",
            40, tap(5, "left"),
            at_tick(40, "entities == %d" % packed_size, "board.slide_merge.SlideBoard.moves == 0",
                    "board.slide_merge.SlideBoard.score == 0"),
            "test_packed"),
        scenario(
            "move_adds_a_tile",
            "After a move that changed something a new tile (2 or 4) appears on an empty cell.",
            40, tap(5, "left"),
            at_tick(40, "entities == %d" % (spawn_size + 1), "t1.grid2d.GridPos.col == 0",
                    "t1.grid2d.GridPos.row == 3", "board.slide_merge.SlideBoard.moves == 1",
                    "board.slide_merge.SlideBoard.highest <= 4"),
            "test_spawn"),
        scenario(
            "swipe_slides_the_board",
            "A swipe of the finger (or a drag of the mouse) slides the board like the arrow\n"
            "keys do: here to the left.",
            40, drag(5, 300, 300, 200, 310),
            at_tick(40, "t1.grid2d.GridPos.col == 0", "t1.grid2d.GridPos.row == 1",
                    "board.slide_merge.SlideBoard.moves == 1"),
            "test_walk"),
        scenario(
            "tap_is_not_a_swipe",
            "A touch that hardly moves does not slide anything.",
            40, drag(5, 300, 300, 305, 302),
            at_tick(40, "t1.grid2d.GridPos.col == 1", "board.slide_merge.SlideBoard.moves == 0"),
            "test_walk"),
        scenario(
            "reaching_2048_shows_the_victory",
            "Making 2048 puts the game on the victory screen: the board stops answering.",
            40, tap(5, "left") + tap(20, "down"),
            at_tick(4, "game_phase.phase.Phase.current == 0") +
            at_tick(12, "t1.slide_merge.Tile.value == 2048", "game_phase.phase.Phase.current == 1") +
            at_tick(40, "t1.grid2d.GridPos.row == 0", "board.slide_merge.SlideBoard.moves == 1"),
            "test_win"),
        scenario(
            "continue_after_2048",
            "From the victory screen the game goes on (key C or Enter): the board answers again,\n"
            "and the victory screen is not shown a second time.",
            60, tap(5, "left") + tap(20, "continue_game") + tap(30, "down"),
            at_tick(12, "game_phase.phase.Phase.current == 1") +
            at_tick(25, "game_phase.phase.Phase.current == 0") +
            at_tick(60, "t1.grid2d.GridPos.row == 3", "board.slide_merge.SlideBoard.moves == 2",
                    "game_phase.phase.Phase.current == 0"),
            "test_win"),
        scenario(
            "continue_with_the_button",
            "The Continue button of the victory screen does the same, with the mouse or a finger.",
            60, tap(5, "left") + click_at(20, *continue_button) + tap(30, "down"),
            at_tick(12, "game_phase.phase.Phase.current == 1") +
            at_tick(60, "t1.grid2d.GridPos.row == 3", "game_phase.phase.Phase.current == 0"),
            "test_win"),
        scenario(
            "no_more_moves_ends_the_game",
            "A full board where nothing can merge ends the game: the board stops answering.",
            30, tap(15, "left"),
            at_tick(30, "board.slide_merge.SlideBoard.no_moves == true",
                    "game_phase.phase.Phase.current == 2",
                    "board.slide_merge.SlideBoard.moves == 0"),
            "test_lost"),
        scenario(
            "new_game_with_r",
            "R starts a new game at any time; the best score is kept.",
            50, tap(5, "left") + tap(20, "new_game"),
            at_tick(15, "scoreboard.records.Record.best >= 4") +
            at_tick(40, "scene is game", "board.slide_merge.SlideBoard.moves == 0",
                    "board.slide_merge.SlideBoard.score == 0",
                    "scoreboard.records.Record.best >= 4",
                    "entities == %d" % (game_size + 3)),
            "test_merge"),
        scenario(
            "new_game_with_the_button",
            "After the last move the New game button starts again.",
            50, click_at(20, *new_button),
            at_tick(15, "game_phase.phase.Phase.current == 2") +
            at_tick(40, "scene is game", "game_phase.phase.Phase.current == 0",
                    "board.slide_merge.SlideBoard.moves == 0"),
            "test_lost"),
    ]


def main():
    write_atlas("tiles", [rounded(color, SIZE, 5) for color in PALETTE], 7, SIZE)
    # Cells of "ui": 0 white, 1 button, 2 button with the focus.
    write_atlas("ui", [solid(rgba("ffffff")), solid(rgba("8f7a66")), solid(rgba("f67c5f"))], 3, 16)
    for name, (comment, data) in prefabs().items():
        write_json(os.path.join(GAME, "prefabs", name + ".json"), data, comment)
    for name, entities in scenes().items():
        write_json(os.path.join(GAME, "scenes", name + ".json"),
                   {"schema_version": 1, "name": name, "entities": entities})
    names = []
    for name, comment, data in scenarios():
        write_json(os.path.join(GAME, "scenarios", name + ".json"), data, comment)
        names.append(name)
    print("wrote %d scenes and %d scenarios" % (len(scenes()), len(names)))
    print(json.dumps(["scenarios/%s.json" % name for name in names]))


if __name__ == "__main__":
    main()
