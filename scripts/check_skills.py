#!/usr/bin/env python3
"""Checks the skills in .claude/skills, which tell Claude how to build games.

A skill that mentions a command or a file that does not exist sends Claude down
a dead end, so this keeps them honest:

1. Each SKILL.md starts with a front matter holding `name` (the directory's
   name) and a short `description`.
2. Every `gectl <command>` it mentions is a command gectl has.
3. Every `--option` written on a gectl or gerun line is one of their options.
4. Every repository path it mentions (games/..., docs/...) exists.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKILLS = ROOT / ".claude" / "skills"

NAME = re.compile(r"^[a-z][a-z0-9-]*$")
COMMAND = re.compile(r"\bgectl ([a-z][a-z-]*)")
OPTION = re.compile(r"(?<![\w-])--[a-z][a-z-]*")
INVOCATION = re.compile(r"\b(?:gectl|gerun)\b[^`&|\n]*")
PATH = re.compile(r"(?<![\w/.-])(?:games|modules|docs|schemas|scripts|runner|tools|src|include)/[A-Za-z0-9_./-]*")


def known_commands():
    source = (ROOT / "tools" / "gectl" / "gectl.cpp").read_text()
    return set(re.findall(r'"  gectl ([a-z-]+)', source))


def known_options():
    options = {"--help"}
    for file in (ROOT / "tools" / "gectl" / "gectl.cpp", ROOT / "runner" / "runner.cpp"):
        options |= set(re.findall(r'"(--[a-z-]+)"', file.read_text()))
    return options


def front_matter(text):
    lines = text.split("\n")
    if not lines or lines[0] != "---":
        return None
    try:
        end = lines.index("---", 1)
    except ValueError:
        return None
    fields = {}
    for line in lines[1:end]:
        key, _, value = line.partition(":")
        fields[key.strip()] = value.strip()
    return fields


def check_skill(directory, commands, options):
    problems = []
    file = directory / "SKILL.md"
    where = file.relative_to(ROOT)
    if not file.is_file():
        return [f"{where}: missing"]
    text = file.read_text()
    fields = front_matter(text)
    if fields is None:
        return [f"{where}: no front matter (--- name / description ---)"]
    name = fields.get("name", "")
    if name != directory.name or not NAME.match(name):
        problems.append(f"{where}: name '{name}' must be the directory's name '{directory.name}'")
    description = fields.get("description", "")
    if not description or len(description) > 600:
        problems.append(f"{where}: description must be 1 to 600 characters")

    for command in sorted(set(COMMAND.findall(text))):
        if command not in commands:
            problems.append(f"{where}: gectl has no command '{command}'")
    for invocation in INVOCATION.findall(text):
        for option in OPTION.findall(invocation):
            if option not in options:
                problems.append(f"{where}: unknown option {option}: {invocation.strip()}")
    for token in sorted(set(PATH.findall(text))):
        path = token.rstrip(".,;:)")
        if path.endswith("/") and path.count("/") == 1:
            path = path[:-1]
        if not (ROOT / path).exists():
            problems.append(f"{where}: {path} does not exist")
    return problems


def main():
    directories = sorted(p for p in SKILLS.iterdir() if p.is_dir()) if SKILLS.is_dir() else []
    if not directories:
        print("no skills found in .claude/skills", file=sys.stderr)
        return 1
    commands, options = known_commands(), known_options()
    problems = []
    for directory in directories:
        problems += check_skill(directory, commands, options)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"{len(problems)} problem(s) found", file=sys.stderr)
        return 1
    print(f"ok: {len(directories)} skill(s) checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
