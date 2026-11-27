"""Writes games/complete_universe/scenarios/tutorial_through.json from the start ticks of the
tutorial's tests (STARTS), or, with --probe, a run that prints the phase every 5 ticks."""
import json
import sys

STEPS = ["landing", "entity_show", "entity_black", "entity_white", "entity_balance",
         "black_show", "black", "black_charge", "black_charge_test", "white_show", "white",
         "white_cost", "white_protect_show", "white_protect", "harvest_show", "harvest",
         "inventory", "walk_show", "walk", "jump_show", "jump", "obstacles", "obstacles_test",
         "lack_white", "lack_white_test", "lack_black", "lack_black_test", "death",
         "after_death", "departure", "orbit", "orbit_test"]
STARTS = json.load(open(sys.argv[1]))  # {step: tick it starts}

# The player's inputs in each test, from the tick it starts.
SEGMENTS = {
    "black": [(5, "pointer_x", 1215), (5, "pointer_y", 542), (10, "hand_right", 1),
              (190, "hand_right", 0)],
    "black_charge_test": [(5, "pointer_x", 1215), (5, "pointer_y", 330), (10, "hand_right", 1),
                          (130, "hand_right", 0)],
    "white": [(5, "pointer_x", 782), (5, "pointer_y", 310), (7, "hand_left", 1),
              (21, "hand_left", 0), (60, "pointer_x", 1190), (60, "pointer_y", 242),
              (62, "hand_left", 1), (70, "pointer_x", 1444), (90, "hand_left", 0)],
    "white_protect": [(5, "pointer_x", 960), (5, "pointer_y", 470), (7, "hand_left", 1),
                      (60, "hand_left", 0)],
    "harvest": [(5, "pointer_x", 411), (5, "pointer_y", 541), (10, "harvest", 1),
                (150, "harvest", 0)],
    "walk": [(5, "move_x", 1), (175, "move_x", 0)],
    "jump": [(5, "move_x", 1), (30, "move_x", 0), (90, "jump", 1), (135, "move_x", 1),
             (141, "jump", 0), (205, "jump", 1), (255, "jump", 0), (455, "move_x", 0)],
    "obstacles_test": [(120, "pointer_x", 975), (120, "pointer_y", 710),
                       (122, "hand_right", 1), (240, "hand_right", 0),
                       (260, "pointer_x", 975), (260, "pointer_y", 644),
                       (262, "hand_right", 1), (380, "hand_right", 0),
                       (400, "pointer_x", 1034), (400, "pointer_y", 677),
                       (402, "hand_right", 1), (520, "hand_right", 0),
                       (540, "move_x", 1), (600, "move_x", 0),
                       (723, "pointer_x", 1025), (723, "pointer_y", 763),
                       (725, "hand_left", 1), (732, "pointer_x", 1390),
                       (750, "hand_left", 0), (760, "move_x", 1), (822, "move_x", 0),
                       (930, "pointer_x", 1031), (930, "pointer_y", 763),
                       (932, "hand_left", 1), (942, "pointer_x", 1380),
                       (960, "hand_left", 0), (970, "move_x", 1), (1040, "jump", 1),
                       (1052, "jump", 0), (1170, "move_x", 0)],
    "lack_white_test": [(10, "pointer_x", 983), (10, "pointer_y", 736), (15, "harvest", 1),
                        (135, "harvest", 0)],
    "lack_black_test": [(10, "pointer_x", 1208), (10, "pointer_y", 736), (15, "harvest", 1),
                        (135, "harvest", 0)],
    "departure": [(27, "move_x", -1), (77, "move_x", 0), (97, "jump", 1),
                  (400, "pointer_x", 780), (400, "pointer_y", 315), (402, "hand_left", 1),
                  (410, "pointer_x", 945), (430, "hand_left", 0), (512, "jump", 0),
                  (532, "move_x", 1), (592, "move_x", 0), (612, "jump", 1),
                  (1100, "jump", 0)],
    "orbit_test": [(10, "move_x", 1), (10, "move_y", -1), (75, "move_y", 0),
                   (260, "move_x", 0)],
}

inputs = []
for step, segment in SEGMENTS.items():
    if step in STARTS:
        inputs += [{"tick": STARTS[step] + dt, "action": action, "value": value}
                   for dt, action, value in segment]
inputs.sort(key=lambda item: item["tick"])
last = max(STARTS.values())
end = STARTS.get("orbit_test", last) + 600

if "--probe" in sys.argv:
    expect = [{"at": tick, "check": "story.phase.Phase.current == 99"}
              for tick in range(0, end, 5)]
    extra = sys.argv[sys.argv.index("--probe") + 1:]
    for item in extra:
        tick, check = item.split("@", 1)
        expect.append({"at": int(tick), "check": check})
else:
    s = dict(json.load(open("games/complete_universe/tools/tutorial_through/all_starts.json")), **STARTS)
    expect = [
        "// Nothing happens before its step: the hero cannot walk yet.",
        {"at": 160, "check": "story.phase.Phase.current == 0"},
        {"at": 160, "check": "hero.transform2d.Position.x == 600"},
        "// He is shown, seen close: his two halves, his reserves.",
        {"at": s["entity_show"] + 60, "check": "camera.camera2d.Camera.zoom > 2.5"},
        {"at": s["entity_white"] + 100, "check": "demo_self.barrier2d.Barrier.up == true"},
        {"at": s["entity_balance"] + 60, "check": "inventory.toggle.Toggle.on == true"},
        "// The black shown: a black hole swallows a rock by itself; then the player's turn.",
        {"at": s["black"] - 6, "check": "rock_demo gone"},
        "// The black held grows (and wears matter); the player holds it.",
        "// The white: a bubble and a wall; their cost; a shelter from falling rocks.",
        {"at": s["white_protect_show"] + 200, "check": "demo_self.barrier2d.Barrier.up == true"},
        {"at": s["harvest_show"] - 6, "check": "barrier_1.barrier2d.Barrier.held > 2.9"},
        "// The harvest, the reserves, walking, jumping (seen close), the obstacles.",
        {"at": s["obstacles"] + 60, "check": "camera.camera2d.Camera.zoom == 1.5"},
        {"at": s["obstacles"] + 120, "check": "demo_bridge.barrier2d.Barrier.up == true"},
        "// The carbon low, the white out; then the matter; death on purpose, then back.",
        {"at": s["lack_white"] + 60, "check": "kit.loadout.Kit.unlocked == 1"},
        {"at": s["lack_black"] + 60, "check": "kit.loadout.Kit.unlocked == 2"},
        {"at": s["death"] + 120, "check": "matter.reserve.Reserve.amount < 20"},
        {"at": s["after_death"] + 10, "check": "checkpoint.checkpoint.Checkpoint.loads == 1"},
        {"at": s["after_death"] + 90, "check": "carbon_glimpse.sprite2d.Sprite.alpha > 200"},
        "// The departure, in the hole he dug: the jump held three seconds, its black hole",
        "// growing; a wall of white over his head: he hits it and falls back; again under the",
        "// open sky: out of the atmosphere, then in orbit.",
        {"at": s["departure"] + 560, "check": "story.phase.Phase.current == 29"},
        {"at": s["departure"] + 560, "check": "hero.transform2d.Position.y > 600"},
        {"at": s["orbit"] + 10, "check": "hero.sprite2d.Animator.set == 1"},
        {"at": s["orbit"] + 30, "check": "ground gone"},
        "// He is shown how to fly, then flies to the platform; then 'to be continued'.",
        {"at": s["orbit"] + 50, "check": "hero.transform2d.Velocity.x > 50"},
        {"at": s["orbit_test"] + 120, "check": "hero.sprite2d.Animator.clip == 104"},
        "story.phase.Phase.current == 31",
    ] + [
        {"at": s[step] + 10, "check": f"story.phase.Phase.current == {index}"}
        for index, step in enumerate(STEPS) if step in s
    ] + [
        "to_follow.text2d.Text.alpha > 200",
    ]

scenario = {"schema_version": 1, "name": "tutorial_through", "scene": "tutorial", "ticks": end,
            "inputs": inputs, "expect": expect}
lines = json.dumps(scenario, indent=2, ensure_ascii=False).split("\n")
out = ["// The whole tutorial (D31), played by a script: the steps are numbered in",
       "// story.phase.Phase.current in the order of the game's phase list (landing 0 ... departure",
       "// 29, orbit 30, orbit_test 31). Generated by tools/tutorial_through/generate.py from the ticks its steps start."]
text = "\n".join(lines)
# Comments are strings in the list above: make them comments.
import re
text = re.sub(r'^(\s*)"(// [^"]*)",$', r"\1\2", text, flags=re.M)
open(sys.argv[2], "w", encoding="utf-8").write("\n".join(out) + "\n" + text + "\n")
