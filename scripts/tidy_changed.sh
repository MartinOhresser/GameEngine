#!/usr/bin/env bash
# clang-tidy on what a branch changes, instead of on the whole project: minutes instead of the
# better part of an hour (clang-tidy is never cached, unlike the compiler).
#
# Usage: ./scripts/tidy_changed.sh [BASE]          (BASE: origin/dev by default)
# Overrides: CLANG_TIDY (default clang-tidy), GE_TIDY_CMAKE_ARGS (more arguments for the CMake
# configure, for example -DSDL_X11_XTEST=OFF on a machine without the XTest headers).
#
# Checked: the .cpp files changed since the point where the branch left BASE (committed or not,
# and new ones), and the .cpp files that include a changed header directly. A header is checked
# through the .cpp files that include it (HeaderFilterRegex in .clang-tidy). What this misses is
# a file that only reaches a changed header through another header: `TIDY=all ./scripts/check.sh`
# (the default) checks everything, as before a release.
set -euo pipefail

cd "$(dirname "$0")/.."

base="${1:-origin/dev}"
build="build/tidy-changed"
CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"

fork="$(git merge-base "$base" HEAD)"
changed="$({
  git diff --name-only --diff-filter=d "$fork" -- '*.cpp' '*.hpp'
  git ls-files -o --exclude-standard -- '*.cpp' '*.hpp'
} | grep -v '/gen/' | sort -u || true)"

# Where to look for the files that include a header.
roots=(src include modules platform runner tools tests examples)
existing_roots=()
for root in "${roots[@]}"; do
  [ -d "$root" ] && existing_roots+=("$root")
done

files=()
while IFS= read -r path; do
  [ -n "$path" ] || continue
  case "$path" in
    *.cpp) files+=("$path") ;;
    *.hpp)
      # How the header is named in an #include: what follows include/ for a public header,
      # its bare name for a private one (included from its own folder).
      case "$path" in
        */include/*) key="${path#*/include/}" ;;
        include/*) key="${path#include/}" ;;
        *) key="$(basename "$path")" ;;
      esac
      while IFS= read -r user; do
        [ -n "$user" ] && files+=("$user")
      done < <(grep -rlF --include='*.cpp' "\"$key\"" "${existing_roots[@]}" | grep -v '/gen/' || true)
      ;;
  esac
done <<< "$changed"

if [ "${#files[@]}" -eq 0 ]; then
  echo "tidy: no C++ file changed since $base"
  exit 0
fi
mapfile -t files < <(printf '%s\n' "${files[@]}" | sort -u)

# Only the compile commands are needed: configure, do not build. The SDL backend is included so
# that its files have commands too.
# shellcheck disable=SC2086 # GE_TIDY_CMAKE_ARGS is a list of arguments on purpose
cmake --preset tidy -B "$build" -DCMAKE_CXX_CLANG_TIDY= -DGE_BUILD_PLATFORM=ON \
  ${GE_TIDY_CMAKE_ARGS:-} > /dev/null

# A file without a compile command (a test of another platform, a template file) is skipped.
known=()
for file in "${files[@]}"; do
  if grep -qF "/$file\"" "$build/compile_commands.json"; then
    known+=("$file")
  fi
done
echo "tidy: ${#known[@]} file(s) changed since $base, or including a changed header"
printf '  %s\n' "${known[@]}"
[ "${#known[@]}" -gt 0 ] || exit 0

jobs="$(nproc 2> /dev/null || echo 4)"
# clang-tidy counts the warnings it hides (those of system headers) for every file: only the
# real ones are kept.
set +e
printf '%s\n' "${known[@]}" \
  | xargs -P "$jobs" -n 1 "$CLANG_TIDY" -p "$build" --quiet 2>&1 \
  | grep -vE '^[0-9]+ warnings? generated\.$'
statuses=("${PIPESTATUS[@]}")
set -e
if [ "${statuses[1]}" -ne 0 ]; then
  echo "tidy: see the warnings above" >&2
  exit 1
fi
echo "tidy: no warning"
