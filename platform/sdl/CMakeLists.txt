# The window backend, built on SDL3. Optional (-DGE_BUILD_PLATFORM=ON): the
# engine, its modules and its tests build and run without it.
#
# SDL3 comes from the system when it is installed, and is built from source
# (statically) otherwise, so a fresh checkout needs nothing but a compiler.

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.30
  GIT_SHALLOW TRUE
  SYSTEM
  FIND_PACKAGE_ARGS 3.2 CONFIG)
FetchContent_MakeAvailable(SDL3)

if(TARGET SDL3::SDL3)
  set(GE_SDL_TARGET SDL3::SDL3)
elseif(TARGET SDL3::SDL3-static)
  set(GE_SDL_TARGET SDL3::SDL3-static)
else()
  message(FATAL_ERROR "SDL3 was found but offers no SDL3::SDL3 target")
endif()

# stb_image decodes the PNG atlases. Third-party code: no engine warnings, no
# clang-tidy.
add_library(ge_stb_image STATIC third_party/stb_image_impl.cpp)
target_include_directories(ge_stb_image SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set_target_properties(ge_stb_image PROPERTIES CXX_CLANG_TIDY "" CXX_STANDARD 20)

add_library(ge_platform_sdl STATIC src/sdl_backend.cpp)
add_library(ge::platform_sdl ALIAS ge_platform_sdl)
target_include_directories(ge_platform_sdl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ge_platform_sdl PUBLIC ge::platform PRIVATE ${GE_SDL_TARGET} ge_stb_image)
ge_target_options(ge_platform_sdl)

# What plays whole games on a window: one game on the backend (play.cpp, shared by geplay and the
# launcher), geplay itself and the launcher. Needs the runner library (the modules).
if(TARGET ge_runner)
  add_library(ge_play STATIC src/play.cpp src/geplay.cpp src/launcher.cpp)
  target_include_directories(ge_play PUBLIC "${PROJECT_SOURCE_DIR}/runner")
  target_link_libraries(ge_play PUBLIC ge_runner ge::platform_sdl)
  ge_target_options(ge_play)

  add_executable(geplay src/geplay_main.cpp)
  target_link_libraries(geplay PRIVATE ge_play)
  ge_target_options(geplay)

  add_executable(gelauncher src/gelauncher_main.cpp)
  target_link_libraries(gelauncher PRIVATE ge_play)
  ge_target_options(gelauncher)
endif()

if(GE_BUILD_TESTS)
  add_executable(ge_platform_tests "${PROJECT_SOURCE_DIR}/tests/support/main.cpp"
                                   "${PROJECT_SOURCE_DIR}/tests/platform/sdl/test_sdl_backend.cpp")
  if(TARGET ge_runner)
    target_sources(ge_platform_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/platform/sdl/test_launcher.cpp")
  endif()
  target_include_directories(ge_platform_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/support")
  target_link_libraries(ge_platform_tests PRIVATE ge::platform_sdl ge::game)
  target_compile_definitions(ge_platform_tests PRIVATE GE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  if(TARGET ge_runner)
    target_link_libraries(ge_platform_tests PRIVATE ge_play)
    target_compile_definitions(ge_platform_tests PRIVATE GE_HAS_GEPLAY=1)
  endif()
  ge_target_options(ge_platform_tests)
  add_test(NAME ge_platform_tests COMMAND ge_platform_tests)
endif()
