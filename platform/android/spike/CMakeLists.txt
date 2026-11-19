# A0 of the Android export (ADR 0012): the smallest program that answers the questions the plan
# depends on, built for Android with the NDK. Not part of the engine's build.
#
#   cmake -S platform/android/spike -B build -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static
#
# SPIKE_SDL_STATIC=ON links SDL3 into libmain.so (what the ADR decides); OFF makes libSDL3.so.
cmake_minimum_required(VERSION 3.25)
project(ge_android_spike LANGUAGES C CXX)

option(SPIKE_SDL_STATIC "Link SDL3 statically into libmain.so" ON)

include(FetchContent)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
if(SPIKE_SDL_STATIC)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
else()
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.30
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

add_library(main SHARED main.cpp)
if(SPIKE_SDL_STATIC)
  # SDL's JNI entry points (called by SDLActivity) are referenced by nothing in the program: take
  # the whole archive, or the linker leaves them out.
  target_link_libraries(main PRIVATE -Wl,--whole-archive SDL3::SDL3-static -Wl,--no-whole-archive)
else()
  target_link_libraries(main PRIVATE SDL3::SDL3)
endif()
target_compile_options(main PRIVATE -Wall -Wextra -Werror)
# What a release APK holds: no symbols (they are 10 of the 14 MB of the static library).
target_link_options(main PRIVATE -Wl,--strip-all)
