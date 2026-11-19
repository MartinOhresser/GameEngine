#!/usr/bin/env bash
# Builds the A0 spike (ADR 0012) into an APK:  platform/android/spike/build.sh [static|shared]
#
# Needs the Android SDK (ANDROID_HOME), the NDK (ANDROID_NDK_LATEST_HOME, ANDROID_NDK_ROOT or
# ANDROID_NDK_HOME), a JDK, CMake and Ninja. Writes build/android-spike/<variant>/ and prints the
# path of the APK on its last line.
set -euo pipefail

variant="${1:-static}"
abis="${SPIKE_ABIS:-arm64-v8a x86_64}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out="$root/build/android-spike/$variant"
ndk="${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}}"

case "$variant" in
  static) static=ON; libraries='"main"' ;;
  shared) static=OFF; libraries='"SDL3", "main"' ;;
  *) echo "variant: static or shared" >&2; exit 2 ;;
esac
if [ -z "$ndk" ] || [ -z "${ANDROID_HOME:-}" ]; then
  echo "Set ANDROID_HOME (the SDK) and ANDROID_NDK_ROOT (the NDK)." >&2
  exit 1
fi

rm -rf "$out/project"
mkdir -p "$out"
cp -R "$here/template" "$out/project"
jni="$out/project/app/src/main/jniLibs"
sdl_source=""
started=$(date +%s)
for abi in $abis; do
  cmake -S "$here" -B "$out/native/$abi" -G Ninja --log-level=WARNING \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release -DSPIKE_SDL_STATIC="$static"
  cmake --build "$out/native/$abi"
  mkdir -p "$jni/$abi"
  cp "$out/native/$abi/libmain.so" "$jni/$abi/"
  if [ "$static" = OFF ]; then
    cp "$out/native/$abi/_deps/sdl3-build/libSDL3.so" "$jni/$abi/"
  fi
  sdl_source="$out/native/$abi/_deps/sdl3-src"
done
echo "native build: $(( $(date +%s) - started )) s"

# What SDL provides for an Android project: its Java classes, the icons and styles, and a Gradle
# wrapper (so that no binary has to live in this repository).
android_project="$sdl_source/android-project"
mkdir -p "$out/project/app/src/main/java/org/libsdl" "$out/project/app/src/main/res"
cp -R "$android_project/app/src/main/java/org/libsdl/app" "$out/project/app/src/main/java/org/libsdl/"
cp -R "$android_project/app/src/main/res/." "$out/project/app/src/main/res/"
cp -R "$android_project/gradle" "$android_project/gradlew" "$android_project/gradle.properties" "$out/project/"
sed -i 's/SPIKE_LIBRARIES/'"$(printf '%s' "$libraries" | sed 's/[&/\]/\\&/g')"'/' \
  "$out/project/app/src/main/java/ge/spike/SpikeActivity.java"

compile_sdk="$(ls "$ANDROID_HOME/platforms" | sed -n 's/^android-\([0-9][0-9]*\)$/\1/p' | sort -n | tail -1)"
echo "compileSdk $compile_sdk"
started=$(date +%s)
(cd "$out/project" && chmod +x gradlew && ./gradlew --no-daemon -q -PcompileSdk="$compile_sdk" assembleDebug)
echo "gradle: $(( $(date +%s) - started )) s"
apk="$out/project/app/build/outputs/apk/debug/app-debug.apk"
ls -l "$apk"
echo "$apk"
