#!/usr/bin/env bash
# Installs what the engine needs on Linux (Debian, Ubuntu, and WSL, which is Ubuntu):
#
#   scripts/setup_linux.sh              build and play: compiler, CMake, Ninja, the libraries of SDL
#   scripts/setup_linux.sh --android    ... and export Android packages: JDK, Android SDK and NDK
#   scripts/setup_linux.sh --dev        ... and what a contributor runs: clang-format, clang-tidy
#   scripts/setup_linux.sh --dry-run    say what would be installed, install nothing
#
# Options: --android  --dev  --yes (do not ask apt for confirmation)  --dry-run
#          --sdk-dir DIR (where the Android SDK is or goes, default $ANDROID_HOME or ~/Android/Sdk)
#          --no-env (do not write ANDROID_HOME and JAVA_HOME into the shell's startup file)
#
# It can be run again: what is there is left alone. The system packages come from apt (it asks
# for your password with sudo); the Android SDK is downloaded from Google into your home, no
# root needed. Other distributions: the script lists the packages to install by hand and still
# sets up the Android SDK.
set -euo pipefail

android=0
dev=0
assume_yes=0
dry_run=0
write_env=1
sdk_dir="${ANDROID_HOME:-$HOME/Android/Sdk}"

# The Android command-line tools, from Google. GE_CMDLINE_TOOLS_URL replaces the address when a
# newer build is wanted. The NDK version is the one the engine is tried with; the newest one is
# taken when it is not offered any more.
cmdline_tools_url="${GE_CMDLINE_TOOLS_URL:-https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip}"
preferred_ndk="${GE_NDK_VERSION:-27.2.12479018}"

usage() {
  sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

say() { printf '==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case "$1" in
    --android) android=1 ;;
    --dev) dev=1 ;;
    --yes | -y) assume_yes=1 ;;
    --dry-run) dry_run=1 ;;
    --no-env) write_env=0 ;;
    --sdk-dir)
      [ $# -ge 2 ] || die "--sdk-dir needs a folder"
      sdk_dir="$2"
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      die "unknown option $1"
      ;;
  esac
  shift
done

[ "$(uname -s)" = "Linux" ] || die "this script is for Linux (and WSL); on other systems see README.md"

# Runs a command, or only says it in a dry run.
run() {
  if [ "$dry_run" = 1 ]; then
    printf '+ %s\n' "$*"
  else
    "$@"
  fi
}

as_root() {
  if [ "$(id -u)" = 0 ]; then
    run "$@"
  elif command -v sudo > /dev/null 2>&1; then
    run sudo "$@"
  else
    die "root rights are needed for: $* (install sudo, or run this script as root)"
  fi
}

is_debian_like() {
  [ -r /etc/os-release ] || return 1
  # shellcheck disable=SC1091
  . /etc/os-release
  case " ${ID:-} ${ID_LIKE:-} " in
    *" debian "* | *" ubuntu "*) command -v apt-get > /dev/null 2>&1 ;;
    *) return 1 ;;
  esac
}

# --- system packages -------------------------------------------------------------------------

base_packages=(
  build-essential cmake ninja-build git pkg-config python3
  # a compiler cache: CMake uses it on its own, a second build of the same code takes seconds
  ccache
  # what SDL3 needs to open a window, play sounds and read a pad (XTest: since SDL 3.4)
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxtst-dev libxss-dev
  libxkbcommon-dev libwayland-dev libasound2-dev libgl1-mesa-dev libegl1-mesa-dev libdrm-dev
  libgbm-dev libudev-dev libdbus-1-dev
)
android_packages=(openjdk-17-jdk-headless unzip wget ca-certificates)
dev_packages=(clang clang-tidy clang-format)

wanted=("${base_packages[@]}")
[ "$android" = 1 ] && wanted+=("${android_packages[@]}")
[ "$dev" = 1 ] && wanted+=("${dev_packages[@]}")

install_system_packages() {
  if ! is_debian_like; then
    say "This is not Debian or Ubuntu: install these packages with your package manager"
    say "(the names are Debian's; look for the equivalents), then run this script again:"
    printf '    %s\n' "${wanted[*]}"
    return 0
  fi
  local missing=() package
  for package in "${wanted[@]}"; do
    dpkg -s "$package" > /dev/null 2>&1 || missing+=("$package")
  done
  if [ ${#missing[@]} -eq 0 ]; then
    say "System packages: all there"
    return 0
  fi
  say "System packages to install: ${missing[*]}"
  local yes=()
  [ "$assume_yes" = 1 ] && yes=(-y)
  # A fresh Ubuntu (WSL included) often runs unattended-upgrades, which holds the dpkg lock for
  # a few minutes: wait for it instead of failing.
  local apt_options=(-o Acquire::Retries=5 -o DPkg::Lock::Timeout=600)
  as_root apt-get "${apt_options[@]}" update
  as_root apt-get "${apt_options[@]}" install ${yes[@]+"${yes[@]}"} --no-install-recommends \
    "${missing[@]}"
}

check_cmake() {
  command -v cmake > /dev/null 2>&1 || return 0
  local version
  version="$(cmake --version | head -n 1 | sed -E 's/[^0-9]*([0-9]+\.[0-9]+).*/\1/')"
  if [ "$(printf '%s\n3.25\n' "$version" | sort -V | head -n 1)" != "3.25" ]; then
    warn "CMake $version is older than the 3.25 the engine needs. Install a newer one" \
      "(Ubuntu 24.04 has it; otherwise: sudo snap install cmake --classic, or pipx install cmake)."
  fi
}

# --- the Android SDK -------------------------------------------------------------------------

java_home() {
  local javac
  javac="$(command -v javac 2> /dev/null || true)"
  [ -n "$javac" ] || return 0
  dirname "$(dirname "$(readlink -f "$javac")")"
}

# The highest of the lines of stdin, by version number.
highest() { sort -V | tail -n 1; }

install_android_sdk() {
  local sdkmanager="$sdk_dir/cmdline-tools/latest/bin/sdkmanager"
  if [ ! -x "$sdkmanager" ]; then
    say "Android command-line tools: download into $sdk_dir"
    if [ "$dry_run" = 1 ]; then
      printf '+ download %s and unpack it into %s/cmdline-tools/latest\n' \
        "$cmdline_tools_url" "$sdk_dir"
    else
      local work
      work="$(mktemp -d)"
      wget -q --show-progress -O "$work/tools.zip" "$cmdline_tools_url" ||
        die "the download of $cmdline_tools_url failed"
      unzip -q "$work/tools.zip" -d "$work/unpacked"
      mkdir -p "$sdk_dir/cmdline-tools"
      rm -rf "$sdk_dir/cmdline-tools/latest"
      mv "$work/unpacked/cmdline-tools" "$sdk_dir/cmdline-tools/latest"
      rm -rf "$work"
    fi
  else
    say "Android command-line tools: already there"
  fi
  if [ "$dry_run" = 1 ]; then
    printf '+ accept the SDK licences, then install the newest platform and Build-Tools,\n'
    printf '+ NDK %s (or the newest) and the platform-tools (adb)\n' "$preferred_ndk"
    return 0
  fi
  # The licences are a text to accept: `yes` ends when sdkmanager stops reading.
  yes | "$sdkmanager" --sdk_root="$sdk_dir" --licenses > /dev/null 2>&1 || true
  local listing platform build_tools ndk
  listing="$("$sdkmanager" --sdk_root="$sdk_dir" --list 2> /dev/null | awk -F'|' '{gsub(/ /, "", $1); print $1}' || true)"
  platform="$(printf '%s\n' "$listing" | grep -E '^platforms;android-[0-9]+$' | sed 's/.*-//' | sort -n | tail -n 1 || true)"
  build_tools="$(printf '%s\n' "$listing" | grep -E '^build-tools;[0-9]+\.[0-9]+\.[0-9]+$' | highest || true)"
  if printf '%s\n' "$listing" | grep -qx "ndk;$preferred_ndk"; then
    ndk="ndk;$preferred_ndk"
  else
    ndk="$(printf '%s\n' "$listing" | grep -E '^ndk;[0-9.]+$' | highest || true)"
  fi
  [ -n "$platform" ] && [ -n "$build_tools" ] && [ -n "$ndk" ] ||
    die "sdkmanager does not list a platform, Build-Tools and NDK (no network?)"
  say "Android SDK: platform $platform, $build_tools, $ndk, platform-tools"
  "$sdkmanager" --sdk_root="$sdk_dir" "platform-tools" "platforms;android-$platform" \
    "$build_tools" "$ndk" > /dev/null
}

# --- the shell's startup file ----------------------------------------------------------------

begin_mark="# >>> gameengine >>>"
end_mark="# <<< gameengine <<<"

startup_files() {
  local file found=0
  for file in "$HOME/.bashrc" "$HOME/.zshrc"; do
    if [ -f "$file" ]; then
      printf '%s\n' "$file"
      found=1
    fi
  done
  [ "$found" = 1 ] || printf '%s\n' "$HOME/.bashrc"
}

write_environment() {
  local jdk block file
  jdk="$(java_home)"
  block="$begin_mark
export ANDROID_HOME=\"$sdk_dir\""
  [ -n "$jdk" ] && block="$block
export JAVA_HOME=\"$jdk\""
  block="$block
export PATH=\"\$ANDROID_HOME/platform-tools:\$PATH\"
$end_mark"
  while IFS= read -r file; do
    if [ "$dry_run" = 1 ]; then
      printf '+ write into %s:\n%s\n' "$file" "$block"
      continue
    fi
    touch "$file"
    # Replaces the block of an earlier run instead of repeating it.
    local kept
    kept="$(awk -v b="$begin_mark" -v e="$end_mark" '$0 == b {skip = 1} !skip {print} $0 == e {skip = 0}' "$file")"
    printf '%s\n\n%s\n' "$kept" "$block" > "$file.new"
    cat "$file.new" > "$file" # keeps the file's rights, and its place when it is a link
    rm -f "$file.new"
    say "Environment written into $file"
  done < <(startup_files)
}

# --- go --------------------------------------------------------------------------------------

install_system_packages
check_cmake
if [ "$android" = 1 ]; then
  if [ "$dry_run" = 0 ] && [ -z "$(java_home)" ]; then
    die "no JDK (javac) found: install openjdk-17-jdk-headless, then run this script again"
  fi
  install_android_sdk
  if [ "$write_env" = 1 ]; then
    write_environment
  fi
fi

say "Done."
if [ "$dry_run" = 1 ]; then
  say "That was a dry run: nothing was installed."
  exit 0
fi
cat << 'EOF'

Next:
  cmake --preset dev && cmake --build --preset dev --target gectl
  ./build/dev/tools/gectl export --targets    # Android must say "ready"
  ./gameEngine.sh                             # builds and opens the launcher
EOF
if [ "$android" = 1 ] && [ "$write_env" = 1 ]; then
  printf '\nOpen a new terminal first (or run: source ~/.bashrc / source ~/.zshrc) so that\nANDROID_HOME and JAVA_HOME are set.\n'
fi
