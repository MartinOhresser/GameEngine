#!/usr/bin/env bash
# Local equivalent of the CI jobs: format, Debug and Release builds with tests,
# sanitizers and clang-tidy. Run it before opening a pull request.
#
# Usage: ./scripts/check.sh
# Overrides: CXX (default g++), CLANG_FORMAT (default clang-format).
set -euo pipefail

cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

echo "==> format"
git ls-files -co --exclude-standard '*.cpp' '*.hpp' \
  | xargs --no-run-if-empty "$CLANG_FORMAT" --dry-run --Werror

for preset in dev release asan; do
  echo "==> ${preset}"
  cmake --preset "$preset" -DCMAKE_CXX_COMPILER="$CXX" > /dev/null
  cmake --build --preset "$preset"
  ctest --preset "$preset"
done

echo "==> tidy"
cmake --preset tidy > /dev/null
cmake --build --preset tidy

echo "All checks passed."
