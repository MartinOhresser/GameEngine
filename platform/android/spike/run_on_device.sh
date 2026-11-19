#!/usr/bin/env bash
# Installs the spike's APK on the running emulator or phone, plays with it, and says what happened:
#   run_on_device.sh app-debug.apk OUTPUT_DIR
# The files written to OUTPUT_DIR are the screenshots, the log of the program and a summary.
set -uo pipefail
apk="$1"
out="$2"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$out"
failures=0
note() { echo "$*" | tee -a "$out/summary.md"; }
fail() { note "FAIL: $*"; failures=$((failures + 1)); }

: > "$out/summary.md"
adb install -r "$apk" > "$out/install.txt" 2>&1 || { cat "$out/install.txt"; fail "the APK does not install"; exit 1; }
adb logcat -c
adb shell am start -W -n ge.spike/.SpikeActivity > "$out/start.txt" 2>&1
sleep 10
adb shell pidof ge.spike > "$out/pid.txt" || true
if [ -s "$out/pid.txt" ]; then note "- the program runs (pid $(cat "$out/pid.txt"))"; else fail "the program is not running ten seconds after the start"; fi

adb exec-out screencap -p > "$out/shot1.png"
read -r width height < <(python3 "$here/pixel.py" "$out/shot1.png" size)
note "- screen: ${width}x${height}"
centre="$(python3 "$here/pixel.py" "$out/shot1.png" 0.5 0.5)"
corner="$(python3 "$here/pixel.py" "$out/shot1.png" 0.5 0.1)"
note "- before the tap: centre=($centre) top=($corner)"
[ "$centre" = "255 128 0" ] && note "- the program draws (orange square)" || fail "no orange square at the centre"
[ "$corner" = "16 96 16" ] && note "- the asset was read (green background)" || fail "the background is not green: the asset was not read"

adb shell input tap $((width / 2)) $((height / 2))
sleep 3
adb exec-out screencap -p > "$out/shot2.png"
after="$(python3 "$here/pixel.py" "$out/shot2.png" 0.5 0.5)"
note "- after the tap: centre=($after)"
[ "$after" = "0 0 255" ] && note "- a touch arrives as a mouse click (blue square)" || fail "the tap did not turn the square blue"

frames_before="$(adb logcat -d | grep -c "SPIKE frames=")"
adb shell input keyevent KEYCODE_BACK
sleep 4
adb shell pidof ge.spike > "$out/pid_after_back.txt" || true
if [ -s "$out/pid_after_back.txt" ]; then note "- after Back: still running"; else fail "after Back the program ended"; fi
adb logcat -d > "$out/logcat.txt"
if grep -q "SPIKE key=AC Back" "$out/logcat.txt"; then note "- Back arrives as the key AC Back"; else fail "Back did not arrive as a key"; fi
frames_after="$(grep -c "SPIKE frames=" "$out/logcat.txt")"
note "- frames logged before Back: $frames_before, after: $frames_after"
grep -E "SPIKE|SDL" "$out/logcat.txt" > "$out/spike.log" || true
note ""
note '```'
cat "$out/spike.log" | sed 's/^.*SDL\/APP *: *//' | head -60 | tee -a "$out/summary.md"
note '```'
exit $((failures > 0 ? 1 : 0))
