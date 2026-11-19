#!/usr/bin/env bash
# Builds the engine and opens the launcher: ./gameEngine.sh [arguments of gelauncher]
#
#   ./gameEngine.sh                       build (the first time takes a few minutes), then open
#   ./gameEngine.sh --headless --frames 5 any argument goes to the launcher
#   ./gameEngine.sh --build-only          build and stop
#
# Needs CMake and a C++ compiler (Visual Studio or its Build Tools on Windows, run from Git Bash;
# gcc or clang on Linux; Xcode tools on macOS). The build goes to build/launcher.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
build_dir="build/launcher"
config="Release"

build_only=0
args=()
for arg in "$@"; do
  if [ "$arg" = "--build-only" ]; then
    build_only=1
  else
    args+=("$arg")
  fi
done

if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake was not found. Install it from https://cmake.org/download/ (or with your package manager) and try again." >&2
  exit 1
fi

# Ninja builds faster when there is one; the generator is only chosen the first time, because
# CMake refuses to change it in an existing build folder.
generator=()
if [ ! -f "$build_dir/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
  generator=(-G Ninja)
fi

if ! cmake -S . -B "$build_dir" --log-level=WARNING "${generator[@]+"${generator[@]}"}" \
  -DCMAKE_BUILD_TYPE="$config" \
  -DGE_BUILD_PLATFORM=ON -DGE_BUILD_TOOLS=ON -DGE_BUILD_TESTS=OFF -DGE_BUILD_EXAMPLES=OFF; then
  echo "The build could not be prepared. Is a C++ compiler installed (on Windows: Visual Studio or its Build Tools with \"Desktop development with C++\")?" >&2
  exit 1
fi
cmake --build "$build_dir" --config "$config" --target gelauncher

# One-configuration generators put the program in the folder, Visual Studio in a Release folder.
launcher=""
for candidate in \
  "$build_dir/platform/sdl/gelauncher" \
  "$build_dir/platform/sdl/gelauncher.exe" \
  "$build_dir/platform/sdl/$config/gelauncher.exe"; do
  if [ -f "$candidate" ]; then
    launcher="$candidate"
    break
  fi
done
if [ -z "$launcher" ]; then
  echo "The build worked but gelauncher was not found in $build_dir/platform/sdl." >&2
  exit 1
fi

if [ "$build_only" = 1 ]; then
  echo "Built: $launcher"
  exit 0
fi
exec "$launcher" "${args[@]+"${args[@]}"}"
