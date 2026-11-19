#!/usr/bin/env python3
"""Rebuilds the generated files of doodle_jump: the atlas pictures and the scenes.

    python3 games/doodle_jump/tools/make_assets.py

The pictures are drawn here, pixel by pixel, so the game ships no binary that cannot be
reviewed (the font comes from scripts/make_font.py). Scenes are written here too, because
the menus, the HUD and the test benches share a lot of lines.

The test benches (scenes/test_*.json) are tiny scenes that only the scenarios play: one
thing at a time (a spring, a brittle platform, a monster) with the real prefabs, so that
each rule of the game is checked without waiting for the random world to produce it.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
from make_png import write_png  # noqa: E402

GAME = os.path.join(HERE, "..")


# --- pictures ------------------------------------------------------------------------------

def rgba(hex_color, alpha=255):
    return [int(hex_color[i : i + 2], 16) for i in (0, 2, 4)] + [alpha]


CLEAR = [0, 0, 0, 0]
WHITE = rgba("ffffff")
DARK = rgba("303040")


def blank():
    return [[CLEAR] * 16 for _ in range(16)]


def solid(color):
    return [[color] * 16 for _ in range(16)]


def platform():
    """A rounded bar, white with a darker underside: the Sprite's colour tints it."""
    tile = blank()
    for y in range(4, 12):
        for x in range(16):
            corner = x in (0, 15) and y in (4, 11)
            if not corner:
                tile[y][x] = rgba("ffffff") if y < 9 else rgba("c8c8c8")
    for x in range(16):
        tile[4][x] = rgba("ffffff")
    return tile


def jumper():
    """A green blob with two eyes, a snout and little legs."""
    tile = blank()
    body, belly = rgba("9bd64a"), rgba("c9ee8a")
    for y in range(1, 12):
        for x in range(2, 13):
            if (x - 7) ** 2 / 25 + (y - 6.5) ** 2 / 30 <= 1:
                tile[y][x] = body if y < 8 else belly
    for x, y in ((5, 4), (9, 4)):
        tile[y][x] = WHITE
        tile[y + 1][x] = WHITE
        tile[y + 1][x + 1 if x == 5 else x - 1] = DARK
    for x in range(11, 15):  # the snout
        tile[7][x] = body
        tile[8][x] = body
    for x in (4, 5, 9, 10):  # the legs
        tile[12][x] = DARK
        tile[13][x] = DARK
    for x in (3, 4, 5, 8, 9, 10):  # the feet
        tile[14][x] = DARK
    return tile


def spring():
    tile = blank()
    for i, y in enumerate(range(3, 14)):
        for x in range(4, 12):
            if (i // 2) % 2 == 0 and 4 <= x < 12:
                tile[y][x] = rgba("b0b0b8")
            elif 5 <= x < 11:
                tile[y][x] = rgba("707080")
    for x in range(3, 13):
        tile[14][x] = rgba("505060")
        tile[2][x] = rgba("e0e0e8")
    return tile


def monster():
    """A blob with a big mouth and horns; the Sprite's colour tints it."""
    tile = blank()
    for y in range(2, 15):
        for x in range(1, 15):
            if (x - 7.5) ** 2 / 42 + (y - 8.5) ** 2 / 38 <= 1:
                tile[y][x] = rgba("e8e8e8")
    for x in (3, 4, 11, 12):  # horns
        tile[1][x] = rgba("ffffff")
        tile[2][x] = rgba("ffffff")
    for x in (4, 5, 10, 11):  # eyes
        tile[6][x] = DARK
        tile[7][x] = DARK
    for x in range(4, 12):  # mouth
        tile[10][x] = DARK
        tile[11][x] = DARK if x % 2 == 0 else rgba("ffffff")
    return tile


def bullet():
    tile = blank()
    for y in range(2, 14):
        for x in range(5, 11):
            if (x - 7.5) ** 2 / 9 + (y - 8) ** 2 / 36 <= 1:
                tile[y][x] = rgba("ffffff") if abs(x - 7.5) < 1.6 else rgba("ffd0a0")
    return tile


def atlas(cells, columns):
    rows = (len(cells) + columns - 1) // columns
    pixels = [[CLEAR] * (columns * 16) for _ in range(rows * 16)]
    for index, cell in enumerate(cells):
        ox, oy = (index % columns) * 16, (index // columns) * 16
        for y in range(16):
            for x in range(16):
                pixels[oy + y][ox + x] = cell[y][x]
    return pixels


def save_png(path, pixels):
    height, width = len(pixels), len(pixels[0])
    write_png(path, width, height, [[c for pixel in row for c in pixel] for row in pixels])


def write_json(path, data):
    with open(path, "w") as out:
        json.dump(data, out, indent=2)
        out.write("\n")


def write_atlas(name, cells, columns):
    save_png(os.path.join(GAME, "art", name + ".png"), atlas(cells, columns))
    write_json(os.path.join(GAME, "art", name + ".json"),
               {"schema_version": 1, "name": name, "image": name + ".png",
                "cell_width": 16, "cell_height": 16})


# --- scenes --------------------------------------------------------------------------------

def text(string, size, x, y, anchor_x=0.0, anchor_y=0.0, pivot_x=0.0, pivot_y=0.0, **extra):
    """A text in window pixels, anchored to the window: it follows its size."""
    return {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": anchor_x, "anchor_y": anchor_y, "offset_x": x, "offset_y": y},
        "text2d.Text": dict({"string": string, "font": "font", "size": size, "screen": True,
                             "pivot_x": pivot_x, "pivot_y": pivot_y}, **extra),
    }


def bound(components, source, field):
    """The same entity, showing a number of the game."""
    return dict(components, **{"ui.Bind": {"source": source, "field": field}})


def paper():
    """The page: one big screen-space sprite behind everything."""
    return {"name": "paper", "components": {
        "transform2d.Position": {},
        "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5},
        "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": 4000, "height": 4000,
                            "screen": True, "layer": -10, "red": 250, "green": 247, "blue": 225}}}


def menu_of(group_menu, group_cells, buttons, offset_y):
    """The controller of a menu, the grid that places its buttons, and the buttons."""
    out = [
        {"name": group_menu + "_input", "components": {"ui.Menu": {
            "group": group_menu, "up": "menu_up", "down": "menu_down", "accept": "menu_accept",
            "use_pointer": True, "pointer_x": "pointer_x", "pointer_y": "pointer_y",
            "click": "click"}}},
        {"name": group_cells + "_grid", "components": {
            "transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5, "offset_y": offset_y},
            "ui.Grid": {"group": group_cells, "columns": 1, "cell_width": 240,
                        "cell_height": 48, "spacing_y": 14}}},
    ]
    for index, (name, label, action) in enumerate(buttons):
        out.append({"name": "button_" + name, "prefab": "button", "components": {
            "ui.Cell": {"group": group_cells, "index": index},
            "ui.Button": {"menu": group_menu, "order": index, "action": action},
            "text2d.Text": {"string": label}}})
    return out


def go(name, scene, **trigger):
    return {"name": name, "components": {"scene_switch.GoToScene": dict({"scene": scene},
                                                                        **trigger)}}


def scoreboard():
    """Follows the altitude score of the player: keeps the best (saved) and the last, and stays
    from scene to scene so that the game over screen can show them."""
    return {"name": "scoreboard", "components": {
        "scene_switch.Persistent": {},
        "ui.Tag": {"tag": "scoreboard"},
        "records.Record": {"key": "best_height", "source": "player",
                           "field": "altitude.Altitude.score"}}}


def boot_scene():
    """Only runs once: it makes the scoreboard that stays for the whole session."""
    return [scoreboard(), go("go_menu", "menu", after_ticks=2)]


def menu_scene():
    out = [paper(),
           {"name": "menu_title", "components": text("menu.title", 48, 0, -230, 0.5, 0.5, 0.5, 0.5,
                                                      red=60, green=120, blue=40)},
           {"name": "menu_best", "components": bound(
               text("menu.best", 24, 0, -170, 0.5, 0.5, 0.5, 0.5, red=60, green=60, blue=60),
               "scoreboard", "records.Record.best")},
           {"name": "menu_help1", "components": text("menu.help1", 16, 0, 120, 0.5, 0.5, 0.5, 0.5,
                                                      red=90, green=90, blue=90)},
           {"name": "menu_help2", "components": text("menu.help2", 16, 0, 146, 0.5, 0.5, 0.5, 0.5,
                                                      red=90, green=90, blue=90)}]
    out += menu_of("main_menu", "menu_buttons",
                   [("play", "menu.play", "start_game"), ("quit", "menu.quit", "quit_game")], -40)
    out.append(go("go_play", "game", use_action=True, action="start_game"))
    # The "Quit" button raises quit_game; the host (launcher, player) then closes the game.
    out.append({"name": "quit_game", "components": {"host.Quit": {"action": "quit_game"}}})
    return out


def gameover_scene():
    dark = dict(red=60, green=60, blue=60)
    out = [paper(),
           {"name": "over_title", "components": text("over.title", 48, 0, -230, 0.5, 0.5, 0.5, 0.5,
                                                      red=200, green=60, blue=50)},
           {"name": "over_score", "components": bound(
               text("over.score", 30, 0, -160, 0.5, 0.5, 0.5, 0.5, **dark),
               "scoreboard", "records.Record.last")},
           {"name": "over_best", "components": bound(
               text("over.best", 24, 0, -120, 0.5, 0.5, 0.5, 0.5, **dark),
               "scoreboard", "records.Record.best")}]
    out += menu_of("gameover_menu", "gameover_buttons",
                   [("again", "over.again", "restart_game"), ("menu", "over.menu", "show_menu")],
                   40)
    out.append(go("go_again", "game", use_action=True, action="restart_game"))
    out.append(go("go_again_key", "game", use_action=True, action="again"))
    out.append(go("go_menu", "menu", use_action=True, action="show_menu"))
    return out


def generators():
    """What builds the world above the jumper, more and more difficult with the height."""
    return [
        {"name": "platforms", "components": {"spawner2d.Generator": {
            "prefab": "platform",
            "alt_prefab": "platform_moving", "alt_chance": 0.0, "alt_chance_end": 0.3,
            "alt2_prefab": "platform_vanishing", "alt2_chance": 0.0, "alt2_chance_end": 0.15,
            "extra_prefab": "spring", "extra_chance": 0.12, "extra_chance_end": 0.06,
            "extra_across": 22, "extra_along": 14, "extra_on_alt": False,
            "across_min": 40, "across_max": 440,
            "gap_min": 50, "gap_max": 90, "gap_min_end": 90, "gap_max_end": 170,
            "ramp_distance": 12000, "origin": 690, "next": 560}}},
        {"name": "breakables", "components": {"spawner2d.Generator": {
            "prefab": "platform_breakable", "across_min": 40, "across_max": 440,
            "gap_min": 90, "gap_max": 200, "row_chance": 0.5, "row_chance_end": 0.8,
            "ramp_distance": 12000, "origin": 690, "from_distance": 600, "next": 400}}},
        {"name": "monsters", "components": {"spawner2d.Generator": {
            "prefab": "monster", "alt_prefab": "monster_flying",
            "alt_chance": 0.2, "alt_chance_end": 0.6,
            "across_min": 60, "across_max": 420,
            "gap_min": 500, "gap_max": 900, "gap_min_end": 330, "gap_max_end": 500,
            "row_chance": 0.6, "row_chance_end": 0.9,
            "ramp_distance": 12000, "origin": 690, "from_distance": 1800, "next": 560}}},
    ]


def touch_layer():
    """The controls of a touch screen: a stick on the left (steers), a button on the right
    (shoots). They only show on a phone, or once a finger has touched the window."""
    def screen_sprite(frame, size, red, green, blue):
        return {"atlas": "ui", "frame": frame, "width": size, "height": size, "screen": True,
                "layer": 20, "red": red, "green": green, "blue": blue}
    return [
        {"name": "touch_stick", "components": {
            "transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 0.0, "anchor_y": 1.0, "offset_x": 100, "offset_y": -110},
            "sprite2d.Sprite": screen_sprite(0, 130, 90, 90, 90),
            "touch_controls.TouchStick": {"group": "touch_stick", "action_x": "move_x",
                                          "radius": 50, "zone": 110, "opacity": 90}}},
        {"name": "touch_knob", "components": {
            "transform2d.Position": {},
            "sprite2d.Sprite": screen_sprite(0, 56, 60, 60, 60),
            "touch_controls.TouchKnob": {"group": "touch_stick", "opacity": 200}}},
        {"name": "touch_shoot", "components": {
            "transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 1.0, "anchor_y": 1.0, "offset_x": -80, "offset_y": -110},
            "sprite2d.Sprite": screen_sprite(1, 90, 255, 255, 255),
            "touch_controls.TouchButton": {"action": "shoot", "width": 110, "height": 110,
                                           "normal_frame": 1, "pressed_frame": 2,
                                           "opacity": 130}}},
    ]


def game_scene():
    out = [
        paper(),
        {"name": "start", "prefab": "platform", "components": {
            "transform2d.Position": {"x": 240, "y": 690}}},
        {"name": "player", "prefab": "player", "components": {
            "transform2d.Position": {"x": 240, "y": 640}}},
        {"name": "camera", "prefab": "camera", "components": {}},
    ]
    out += generators()
    out += [
        {"name": "hud_score", "components": bound(
            text("hud.score", 30, 14, 8, red=60, green=60, blue=60),
            "player", "altitude.Altitude.score")},
        {"name": "hud_best", "components": bound(
            text("hud.best", 16, -14, 14, 1.0, 0.0, 1.0, 0.0, red=120, green=120, blue=120),
            "scoreboard", "records.Record.best")},
        go("end_of_game", "gameover", use_action=True, action="died"),
    ]
    out += touch_layer()
    return out


def bench(*things, player_at=(240, 640), player_speed=None):
    """A test bench: the real player and camera, the end of game, and the things to test."""
    player = {"transform2d.Position": {"x": player_at[0], "y": player_at[1]}}
    if player_speed is not None:
        player["transform2d.Velocity"] = {"x": 0, "y": player_speed}
    out = [{"name": "player", "prefab": "player", "components": player},
           {"name": "camera", "prefab": "camera", "components": {}},
           go("end_of_game", "gameover", use_action=True, action="died"),
           scoreboard()]  # the benches start without the boot scene
    return out + list(things)


def thing(name, prefab, x, y, **components):
    return {"name": name, "prefab": prefab,
            "components": dict({"transform2d.Position": {"x": x, "y": y}}, **components)}


def scenes():
    return {
        "boot": boot_scene(),
        "menu": menu_scene(),
        "game": game_scene(),
        "gameover": gameover_scene(),
        # A jumper next to the edge of the world, with nothing under it.
        "test_wrap": bench(player_at=(470, 560)),
        # A platform with a spring on it, under a jumper that falls onto the spring.
        "test_spring": bench(thing("ground", "platform", 240, 690), thing("spring", "spring", 240, 676),
                             player_at=(240, 560)),
        # A brittle platform under a jumper that falls onto it.
        "test_breaks": bench(thing("brittle", "platform_breakable", 240, 690), player_at=(240, 640)),
        # A platform that vanishes after one bounce.
        "test_vanishes": bench(thing("cloud", "platform_vanishing", 240, 690), player_at=(240, 640)),
        # A moving platform between x = 100 and x = 200 (no player: it only moves).
        "test_moving": [{"name": "camera", "prefab": "camera", "components": {}},
                        thing("mover", "platform_moving", 150, 400,
                              **{"transform2d.Velocity": {"x": 100},
                                 "patrol2d.Patrol": {"min_x": 100, "max_x": 200}})],
        # A monster above a jumper that rises into it.
        "test_monster_touch": bench(thing("monster", "monster", 240, 420), player_at=(240, 600),
                                    player_speed=-900),
        # A monster under a jumper that falls onto it.
        "test_monster_stomp": bench(thing("monster", "monster", 240, 650), player_at=(240, 560)),
        # A monster high above a jumper that bounces on the ground: shoot it.
        "test_monster_shoot": bench(thing("ground", "platform", 240, 690),
                                    thing("monster", "monster", 240, 300), player_at=(240, 640)),
    }


def main():
    # Cells of "doodle": 0 white, 1 platform, 2 jumper, 3 spring, 4 monster, 5 bullet.
    write_atlas("doodle", [solid(WHITE), platform(), jumper(), spring(), monster(), bullet()], 3)
    # Cells of "ui": 0 white, 1 button, 2 button with the focus.
    write_atlas("ui", [solid(WHITE), solid(rgba("4a8a3a")), solid(rgba("e0a030"))], 3)
    for name, entities in scenes().items():
        write_json(os.path.join(GAME, "scenes", name + ".json"),
                   {"schema_version": 1, "name": name, "entities": entities})
    print("wrote %d scenes" % len(scenes()))


if __name__ == "__main__":
    main()
