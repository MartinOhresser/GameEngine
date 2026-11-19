#!/usr/bin/env bash
# Installs an exported game on the running emulator or phone, starts it, and checks that it
# lives and draws:
#   run_game_on_device.sh game.apk OUTPUT_DIR
# The package name is read from the APK. Writes screenshots, the log and a summary to OUTPUT_DIR.
#
# With GAME_WINDOW="480 720" (the game's window) and MENU_TAP="240 320" (where its Play button is,
# in the pixels of the game), it also plays: it taps Play, checks that the menu is gone, holds a
# finger on the stick at the bottom left and checks that the controls show, then presses Back and
# checks that the game ends.
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

# A point of the game's window, as a fraction of the screen (the window is letterboxed).
fraction() { # x y -> "fx fy"
  python3 - "$width" "$height" $GAME_WINDOW "$1" "$2" <<'PY'
import sys
w, h, gw, gh, x, y = map(float, sys.argv[1:7])
scale = min(w / gw, h / gh)
print((w - gw * scale) / 2 / w + x * scale / w, (h - gh * scale) / 2 / h + y * scale / h)
PY
}
pixel() { python3 "$here/spike/pixel.py" "$1" $2; }

if [ -n "${GAME_WINDOW:-}" ] && [ -n "${MENU_TAP:-}" ]; then
  read -r px py < <(fraction $MENU_TAP)
  menu_before="$(pixel "$out/shot1.png" "$px $py")"
  note "- Play button before the tap: ($menu_before)"
  tap_x="$(python3 -c "print(int($px * $width))")"
  tap_y="$(python3 -c "print(int($py * $height))")"
  adb shell input tap "$tap_x" "$tap_y"
  sleep 4
  adb exec-out screencap -p > "$out/shot3.png"
  menu_after="$(pixel "$out/shot3.png" "$px $py")"
  note "- same place after the tap: ($menu_after)"
  [ "$menu_before" != "$menu_after" ] && note "- the tap on Play was understood (touch is a click)" || fail "the menu is still there after the tap"

  # A finger held on the stick (bottom left of the window), moved a little but inside its dead
  # zone so that the jumper stays on its platform; the swipe lasts 5 s in the background.
  read -r sx sy < <(fraction 100 610)
  read -r ex ey < <(fraction 104 610)
  sx_px="$(python3 -c "print(int($sx * $width))")"; sy_px="$(python3 -c "print(int($sy * $height))")"
  ex_px="$(python3 -c "print(int($ex * $width))")"; ey_px="$(python3 -c "print(int($ey * $height))")"
  adb shell input swipe "$sx_px" "$sy_px" "$ex_px" "$ey_px" 5000 &
  sleep 2
  adb exec-out screencap -p > "$out/shot4.png"
  wait
  # The stick is a grey disc, darker than the paper (250 247 225), at the left of its knob.
  read -r bx by < <(fraction 70 610)
  base="$(pixel "$out/shot4.png" "$bx $by")"
  note "- stick base while a finger is on it: ($base)"
  read -r r g b <<< "$base"
  if [ "${r:-255}" -lt 235 ]; then note "- the touch controls show"; else fail "the touch controls do not show"; fi
  adb shell pidof "$package" > "$out/pid3.txt" || true
  [ -s "$out/pid3.txt" ] && note "- the game plays on after the swipe" || fail "the game ended during the swipe"
fi

adb shell input keyevent KEYCODE_BACK
sleep 4
# The process may stay around after the game ends: look at what is on the screen instead.
adb shell dumpsys activity activities > "$out/activities.txt" || true
if grep -E "ResumedActivity" "$out/activities.txt" | grep -q "$package"; then
  fail "after Back the game is still on the screen"
else
  note "- Back leaves the game"
fi

adb logcat -d > "$out/logcat.txt"
grep -E "SDL|$package|DEBUG" "$out/logcat.txt" | head -120 > "$out/game.log" || true
exit $((failures > 0 ? 1 : 0))
