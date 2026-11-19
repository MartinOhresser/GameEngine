#!/usr/bin/env python3
"""Rebuilds the generated files of the launcher: the picture of its interface and its scene.

    python3 tools/launcher/tools/make_assets.py

The launcher is a game like the others, made of JSON files with the `ui`, `text2d` and `host`
modules. Its list of games and the lines about the selected one are run-time lists that the
host (gelauncher) fills; this script only places the texts and the buttons that show them.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))
from make_png import write_png  # noqa: E402

GAME = os.path.join(HERE, "..")
ROWS = 8          # games shown at once
ROW_WIDTH = 330
ROW_HEIGHT = 40
ROW_GAP = 6
LIST_LEFT = 40    # window pixels
LIST_TOP = 124
PANEL_LEFT = 440
DETAIL_LINES = 10
TARGET_ROWS = 3   # platforms shown at once on the export screen
EXPORT_LINES = 10 # the export screen's lines: heading, state, then the last 8 lines of output


def rgba(hex_color, alpha=255):
    return [int(hex_color[i : i + 2], 16) for i in (0, 2, 4)] + [alpha]


def solid(color):
    return [[color] * 16 for _ in range(16)]


def write_json(path, data):
    with open(path, "w") as out:
        json.dump(data, out, indent=2)
        out.write("\n")


def write_ui_atlas():
    """Cells: 0 white, 1 button, 2 button with the focus, 3 nothing (a row without a game)."""
    cells = [solid(rgba("ffffff")), solid(rgba("4a8a3a")), solid(rgba("e0a030")),
             solid([0, 0, 0, 0])]
    pixels = [[[0, 0, 0, 0]] * 32 for _ in range(32)]
    for index, cell in enumerate(cells):
        ox, oy = (index % 2) * 16, (index // 2) * 16
        for y in range(16):
            for x in range(16):
                pixels[oy + y][ox + x] = cell[y][x]
    write_png(os.path.join(GAME, "art", "ui.png"), 32, 32,
              [[c for pixel in row for c in pixel] for row in pixels])
    write_json(os.path.join(GAME, "art", "ui.json"),
               {"schema_version": 1, "name": "ui", "image": "ui.png",
                "cell_width": 16, "cell_height": 16})


def anchored(x, y):
    return {"transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 0, "anchor_y": 0, "offset_x": x, "offset_y": y}}


def label(string, size, x, y, pivot_x=0.0, **extra):
    components = anchored(x, y)
    components["text2d.Text"] = dict({"string": string, "font": "font", "size": size,
                                      "screen": True, "pivot_x": pivot_x, "pivot_y": 0.5}, **extra)
    return components


def button(order, action, width, height, x, y, **text):
    components = anchored(x, y)
    components["sprite2d.Sprite"] = {"atlas": "ui", "frame": 1, "width": width, "height": height,
                                     "screen": True, "layer": 10}
    components["text2d.Text"] = {"font": "font", "size": 20, "pivot_x": 0.5, "pivot_y": 0.5,
                                 "screen": True, **text}
    components["ui.Button"] = {"menu": "main_menu", "order": order, "action": action,
                               "width": width, "height": height, "normal_frame": 1,
                               "focus_frame": 2, "disabled_frame": 3}
    return components


def main_scene():
    out = [
        {"name": "paper", "components": {
            "transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": 4000, "height": 4000,
                                "screen": True, "layer": -10, "red": 250, "green": 247,
                                "blue": 225}}},
        {"name": "title", "components": label("launcher.title", 40, LIST_LEFT, 44,
                                              red=60, green=120, blue=40)},
        {"name": "hint", "components": label("launcher.hint", 14, LIST_LEFT, 88,
                                             red=90, green=90, blue=90)},
        {"name": "controller", "components": {"ui.Menu": {
            "group": "main_menu", "up": "menu_up", "down": "menu_down", "accept": "menu_accept",
            "use_pointer": True, "pointer_x": "pointer_x", "pointer_y": "pointer_y",
            "click": "click"}}},
        {"name": "pages", "components": {"ui.Pager": {
            "list": "games", "menu": "main_menu", "page_size": ROWS,
            "previous": "page_prev", "next": "page_next"}}},
    ]
    for row in range(ROWS):
        y = LIST_TOP + row * (ROW_HEIGHT + ROW_GAP)
        components = button(row, "play_game", ROW_WIDTH, ROW_HEIGHT, LIST_LEFT + ROW_WIDTH // 2, y)
        components["text2d.Text"]["string"] = 0
        components["text2d.Dynamic"] = {}
        components["ui.Row"] = {"list": "games", "row": row}
        out.append({"name": "row%d" % row, "components": components})
    bottom = LIST_TOP + ROWS * (ROW_HEIGHT + ROW_GAP) + 14
    out.append({"name": "button_prev", "components": button(
        ROWS, "page_prev", 56, 36, LIST_LEFT + 28, bottom, string="button.prev")})
    out.append({"name": "button_next", "components": button(
        ROWS + 1, "page_next", 56, 36, LIST_LEFT + 90, bottom, string="button.next")})
    out.append({"name": "button_export", "components": button(
        ROWS + 2, "open_export", 100, 36, LIST_LEFT + 174, bottom, string="button.export")})
    out.append({"name": "button_quit", "components": button(
        ROWS + 3, "quit_game", 100, 36, LIST_LEFT + 280, bottom, string="button.quit")})
    out.append({"name": "go_export", "components": {"scene_switch.GoToScene": {
        "scene": "export", "use_action": True, "action": "open_export"}}})
    out.append({"name": "quit_game", "components": {"host.Quit": {"action": "quit_game"}}})
    out.append({"name": "play_it", "components": {"host.Call": {
        "command": "play", "action": "play_game", "list": "games"}}})
    # The lines about the selected game: the host writes them in the list "details".
    for line in range(DETAIL_LINES):
        if line == 0:
            y, size, colour = 134, 28, dict(red=60, green=120, blue=40)
        elif line == 1:
            y, size, colour = 170, 14, dict(red=120, green=120, blue=120)
        elif line < 8:
            y, size, colour = 204 + (line - 2) * 24, 16, dict(red=60, green=60, blue=60)
        else:
            y, size, colour = 204 + 6 * 24 + 20 + (line - 8) * 24, 14, dict(
                red=120, green=120, blue=120)
        components = label("launcher.hint", size, PANEL_LEFT, y, **colour)
        components["text2d.Dynamic"] = {"list": "details", "index": line}
        out.append({"name": "detail%d" % line, "components": components})
    return out


def export_scene():
    """Choose a platform for the selected game and watch the export: the host writes the
    platforms in the list "targets" and the lines of the screen in the list "export"."""
    out = [
        {"name": "paper", "components": {
            "transform2d.Position": {},
            "ui.Anchor": {"anchor_x": 0.5, "anchor_y": 0.5},
            "sprite2d.Sprite": {"atlas": "ui", "frame": 0, "width": 4000, "height": 4000,
                                "screen": True, "layer": -10, "red": 250, "green": 247,
                                "blue": 225}}},
        {"name": "title", "components": label("export.title", 40, LIST_LEFT, 44,
                                              red=60, green=120, blue=40)},
        {"name": "hint", "components": label("export.hint", 14, LIST_LEFT, 88,
                                             red=90, green=90, blue=90)},
        {"name": "controller", "components": {"ui.Menu": {
            "group": "export_menu", "up": "menu_up", "down": "menu_down",
            "accept": "menu_accept", "use_pointer": True, "pointer_x": "pointer_x",
            "pointer_y": "pointer_y", "click": "click"}}},
        {"name": "pages", "components": {"ui.Pager": {
            "list": "targets", "menu": "export_menu", "page_size": TARGET_ROWS,
            "previous": "targets_prev", "next": "targets_next"}}},
    ]
    for row in range(TARGET_ROWS):
        y = LIST_TOP + row * (ROW_HEIGHT + ROW_GAP)
        components = button(row, "start_export", ROW_WIDTH, ROW_HEIGHT,
                            LIST_LEFT + ROW_WIDTH // 2, y)
        components["text2d.Text"]["string"] = 0
        components["text2d.Dynamic"] = {}
        components["ui.Row"] = {"list": "targets", "row": row}
        components["ui.Button"]["menu"] = "export_menu"
        out.append({"name": "row%d" % row, "components": components})
    back = button(TARGET_ROWS, "back_main", 120, 36, LIST_LEFT + ROW_WIDTH + 80,
                  LIST_TOP, string="button.back")
    back["ui.Button"]["menu"] = "export_menu"
    out.append({"name": "button_back", "components": back})
    out.append({"name": "go_main", "components": {"scene_switch.GoToScene": {
        "scene": "main", "use_action": True, "action": "back_main"}}})
    out.append({"name": "start_it", "components": {"host.Call": {
        "command": "export", "action": "start_export", "list": "targets"}}})
    top = LIST_TOP + TARGET_ROWS * (ROW_HEIGHT + ROW_GAP) + 20
    for line in range(EXPORT_LINES):
        if line == 0:
            y, size, colour = top, 26, dict(red=60, green=120, blue=40)
        elif line == 1:
            y, size, colour = top + 34, 16, dict(red=120, green=120, blue=120)
        else:
            y, size, colour = top + 66 + (line - 2) * 20, 13, dict(red=70, green=70, blue=70)
        components = label("launcher.hint", size, LIST_LEFT, y, **colour)
        components["text2d.Dynamic"] = {"list": "export", "index": line}
        out.append({"name": "line%d" % line, "components": components})
    return out


def main():
    write_ui_atlas()
    write_json(os.path.join(GAME, "scenes", "main.json"),
               {"schema_version": 1, "name": "main", "entities": main_scene()})
    write_json(os.path.join(GAME, "scenes", "export.json"),
               {"schema_version": 1, "name": "export", "entities": export_scene()})
    print("wrote 2 scenes")


if __name__ == "__main__":
    main()
