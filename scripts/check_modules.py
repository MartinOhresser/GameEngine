#!/usr/bin/env python3
"""Checks the modules directory without building anything.

1. Every modules/*/module.json is valid against schemas/module.schema.json
   (needs the `jsonschema` package; skipped with a notice when it is missing,
   unless --require-schema is given, as in CI).
2. A module only includes what it declares: engine headers (`ge/...`), its own
   headers and the headers of modules listed in its `depends`.
3. A module contains no platform code: no OS, windowing or graphics headers.

Usage: scripts/check_modules.py [--require-schema]
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MODULES = ROOT / "modules"

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
MODULE_HEADER = re.compile(r"^ge/mod/([a-z][a-z0-9_]*)/")
PLATFORM_HEADERS = re.compile(
    r"^(windows\.h|winsock2\.h|unistd\.h|pthread\.h|dlfcn\.h|sys/.*|"
    r"SDL.*|GLFW/.*|GL/.*|OpenGL/.*|vulkan/.*|d3d.*|android/.*|jni\.h|emscripten.*|"
    r"TargetConditionals\.h|mach/.*|Cocoa/.*|UIKit/.*)$"
)
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".cxx", ".inl"}


def load_modules():
    modules = {}
    for manifest in sorted(MODULES.glob("*/module.json")):
        modules[manifest.parent.name] = (manifest, json.loads(manifest.read_text()))
    return modules


def check_schema(modules, required):
    try:
        import jsonschema
    except ImportError:
        if required:
            return ["the jsonschema package is required (pip install jsonschema)"]
        print("note: jsonschema is not installed, skipping the schema check")
        return []
    schema = json.loads((ROOT / "schemas" / "module.schema.json").read_text())
    jsonschema.Draft202012Validator.check_schema(schema)
    validator = jsonschema.Draft202012Validator(schema)
    problems = []
    for name, (manifest, data) in modules.items():
        for error in sorted(validator.iter_errors(data), key=lambda e: list(e.path)):
            where = ".".join(str(part) for part in error.path) or "<root>"
            problems.append(f"{manifest.relative_to(ROOT)}: {where}: {error.message}")
    return problems


def check_includes(modules):
    problems = []
    for name, (manifest, data) in modules.items():
        allowed = {name, *data.get("depends", [])}
        for path in sorted((MODULES / name).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            text = path.read_text()
            for header in INCLUDE.findall(text):
                relative = path.relative_to(ROOT)
                owner = MODULE_HEADER.match(header)
                if owner and owner.group(1) not in allowed:
                    problems.append(
                        f"{relative}: includes {header}, but module '{owner.group(1)}' is not "
                        f"in the depends of '{name}'"
                    )
                if PLATFORM_HEADERS.match(header):
                    problems.append(
                        f"{relative}: includes {header}, which is platform code; modules must "
                        "go through engine interfaces"
                    )
    return problems


def main():
    required = "--require-schema" in sys.argv[1:]
    modules = load_modules()
    problems = check_schema(modules, required) + check_includes(modules)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"{len(problems)} problem(s) found", file=sys.stderr)
        return 1
    print(f"ok: {len(modules)} module(s) checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
