#!/usr/bin/env bash
# Installs an exported game on the running emulator or phone, starts it, and checks that it
# lives and draws:
#   run_game_on_device.sh game.apk OUTPUT_DIR
# The package name is read from the APK. Writes screenshots, the log and a summary to OUTPUT_DIR.
set -uo pipefail
apk="$1"
out="$2"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$out"
failures=0
note() { echo "$*" | tee -a "$out/summary.md"; }
fail() { note "FAIL: $*"; failures=$((failures + 1)); }

: > "$out/summary.md"
aapt2="$(find "$ANDROID_HOME/build-tools" -name aapt2 | sort -V | tail -1)"
package="$("$aapt2" dump packagename "$apk")"
note "- package: $package"

adb install -r "$apk" > "$out/install.txt" 2>&1 || { cat "$out/install.txt"; fail "the APK does not install"; exit 1; }
adb logcat -c
adb shell monkey -p "$package" -c android.intent.category.LAUNCHER 1 > "$out/start.txt" 2>&1
sleep 12
adb shell pidof "$package" > "$out/pid.txt" || true
if [ -s "$out/pid.txt" ]; then note "- the game runs (pid $(cat "$out/pid.txt"))"; else fail "the game is not running twelve seconds after the start"; fi

adb exec-out screencap -p > "$out/shot1.png"
read -r width height < <(python3 "$here/spike/pixel.py" "$out/shot1.png" size)
note "- screen: ${width}x${height}"
# A game that draws is not one flat colour: count the distinct colours of a grid of points.
colours="$(python3 "$here/spike/pixel.py" "$out/shot1.png" colours)"
note "- distinct colours on screen: $colours"
[ "${colours:-0}" -ge 4 ] && note "- the game draws" || fail "the screen is blank (fewer than 4 colours)"

sleep 3
adb exec-out screencap -p > "$out/shot2.png"
adb shell pidof "$package" > "$out/pid2.txt" || true
[ -s "$out/pid2.txt" ] && note "- still running after the second look" || fail "the game ended by itself"
adb logcat -d > "$out/logcat.txt"
if grep -E "FATAL EXCEPTION|SIGSEGV|signal 11|Fatal signal" "$out/logcat.txt" | head -3 | grep -q .; then
  fail "the log shows a crash"
fi
grep -E "SDL|$package|DEBUG" "$out/logcat.txt" | head -80 > "$out/game.log" || true
exit $((failures > 0 ? 1 : 0))
