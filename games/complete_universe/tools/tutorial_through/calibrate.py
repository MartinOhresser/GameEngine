"""Runs the tutorial scenario in probe mode until the start ticks of its tests settle (each run
moves every test to where its step was seen to start), then prints them."""
import json
import re
import subprocess
import sys

ROOT = "/mnt/c/Users/marti/Desktop/GameEngine"
STEPS = ["landing", "entity_show", "entity_black", "entity_white", "entity_balance",
         "black_show", "black", "black_charge", "black_charge_test", "white_show", "white",
         "white_cost", "white_protect_show", "white_protect", "harvest_show", "harvest",
         "inventory", "walk_show", "walk", "jump_show", "jump", "obstacles", "obstacles_test",
         "lack_white", "lack_white_test", "lack_black", "lack_black_test", "death",
         "after_death", "departure", "orbit", "orbit_test"]
extra = sys.argv[1:]
for attempt in range(8):
    subprocess.run(["python3", "games/complete_universe/tools/tutorial_through/generate.py",
                    "games/complete_universe/tools/tutorial_through/starts.json",
                    "games/complete_universe/scenarios/tutorial_through.json", "--probe", *extra],
                   cwd=ROOT, check=True)
    run = subprocess.run([f"{ROOT}/build/dev/runner/gerun.exe",
                          "games/complete_universe/game.json", "--test", "--scenario",
                          "tutorial_through"], cwd=ROOT, capture_output=True, text=True)
    open(f"{ROOT}/build/tutorial_through_probe.log", "w").write(run.stdout + run.stderr)
    seen = {}
    for match in re.finditer(r"tick (\d+): \"story.phase.Phase.current == 99\" is false "
                             r"\(the value is (\d+)\)", run.stdout):
        tick, phase = int(match.group(1)), int(match.group(2))
        seen.setdefault(phase, tick)
    starts = json.load(open(f"{ROOT}/games/complete_universe/tools/tutorial_through/starts.json"))
    changed = False
    for step in list(starts):
        index = STEPS.index(step)
        if index in seen and abs(seen[index] - starts[step]) > 3:
            starts[step] = seen[index]
            changed = True
    json.dump(starts, open(f"{ROOT}/games/complete_universe/tools/tutorial_through/starts.json", "w"), indent=1)
    reached = max(seen) if seen else 0
    print(attempt, "reached", reached, STEPS[reached], json.dumps(starts), flush=True)
    if not changed:
        break
for line in (run.stdout + run.stderr).splitlines():
    if "== 99" not in line:
        print(line)
