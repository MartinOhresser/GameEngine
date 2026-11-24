#!/usr/bin/env bash
# Local equivalent of the CI jobs: format, Debug and Release builds with tests,
# sanitizers and clang-tidy. Run it before opening a pull request.
#
# Usage: ./scripts/check.sh
# Overrides: CXX (default g++), CLANG_FORMAT (default clang-format), TIDY (all, the default: every
# file; changed: only what the branch changes since TIDY_BASE, origin/dev by default, see
# scripts/tidy_changed.sh).
set -euo pipefail

cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

echo "==> format"
git ls-files -co --exclude-standard '*.cpp' '*.hpp' \
  | grep -v '/gen/' \
  | xargs --no-run-if-empty "$CLANG_FORMAT" --dry-run --Werror

if command -v python3 > /dev/null; then
  echo "==> modules"
  python3 scripts/check_modules.py
  echo "==> skills"
  python3 scripts/check_skills.py
fi

for preset in dev release asan; do
  echo "==> ${preset}"
  cmake --preset "$preset" -DCMAKE_CXX_COMPILER="$CXX" > /dev/null
  cmake --build --preset "$preset"
  ctest --preset "$preset"
done

echo "==> generated code is up to date"
./build/dev/tools/gectl generate --check
./build/dev/tools/gectl schema --check

echo "==> games are valid"
for game in games/*/game.json tools/launcher/game.json; do
  ./build/dev/tools/gectl validate "$game"
done

if [ "${TIDY:-all}" = "changed" ]; then
  echo "==> tidy (what the branch changes)"
  ./scripts/tidy_changed.sh "${TIDY_BASE:-origin/dev}"
else
  echo "==> tidy"
  cmake --preset tidy > /dev/null
  cmake --build --preset tidy
fi

echo "All checks passed."
