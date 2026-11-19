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

# What plays whole games on a window: one game on the backend (play.cpp, shared by geplay, the
# launcher and the players), geplay, the launcher and the player of an exported game. It does not
# link any module: each program brings its own registry (every module for geplay and the
# launcher, only the game's for a player).
add_library(ge_play STATIC src/play.cpp src/geplay.cpp src/launcher.cpp src/player.cpp)
target_include_directories(ge_play PUBLIC "${PROJECT_SOURCE_DIR}/runner")
target_link_libraries(ge_play PUBLIC ge::game ge::app ge::platform_sdl)
ge_target_options(ge_play)

if(TARGET ge_runner)
  add_executable(geplay src/geplay_main.cpp)
  target_link_libraries(geplay PRIVATE ge_play ge_runner)
  ge_target_options(geplay)

  add_executable(gelauncher src/gelauncher_main.cpp)
  target_link_libraries(gelauncher PRIVATE ge_play ge_runner)
  ge_target_options(gelauncher)
endif()

# The launcher's export panel runs `gectl export` in a thread of its own program, so it needs the
# tools' library (and hence every module). Without the tools the launcher has no export.
if(TARGET ge_runner AND TARGET gectl_lib)
  find_package(Threads REQUIRED)
  add_library(ge_tool_export STATIC src/tool_export.cpp)
  target_link_libraries(ge_tool_export PUBLIC ge_play gectl_lib Threads::Threads)
  ge_target_options(ge_tool_export)
  target_link_libraries(gelauncher PRIVATE ge_tool_export)
  target_compile_definitions(gelauncher PRIVATE GE_LAUNCHER_EXPORT=1)
endif()

# The program of an exported game: ge_add_player(NAME MODULES a b c [ICON file.ico]).
# Only the registry and the link change from a game to another: the modules are static
# libraries, built once. On Windows the program has no console (a GUI program that keeps
# `main`) and carries the icon; the C runtime is linked statically by the caller
# (-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded, which gectl export passes).
function(ge_add_player name)
  cmake_parse_arguments(PLAYER "" "ICON" "MODULES" ${ARGN})
  ge_write_registry("${CMAKE_CURRENT_BINARY_DIR}/${name}_registry.cpp" ${PLAYER_MODULES})
  set(sources src/player_main.cpp "${CMAKE_CURRENT_BINARY_DIR}/${name}_registry.cpp")
  if(PLAYER_ICON AND MSVC)
    enable_language(RC)
    file(TO_CMAKE_PATH "${PLAYER_ICON}" icon_path)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.rc" "1 ICON \"${icon_path}\"\n")
    list(APPEND sources "${CMAKE_CURRENT_BINARY_DIR}/${name}.rc")
  endif()
  add_executable(${name} ${sources})
  target_link_libraries(${name} PRIVATE ge_play)
  foreach(module IN LISTS PLAYER_MODULES)
    target_link_libraries(${name} PRIVATE ge::mod::${module})
  endforeach()
  if(MSVC)
    target_link_options(${name} PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  elseif(WIN32)
    target_link_options(${name} PRIVATE -mwindows)
  endif()
  ge_target_options(${name})
endfunction()

# `gectl export` configures the engine with the modules of one game.
if(GE_PLAYER_MODULES)
  ge_add_player(ge_player MODULES ${GE_PLAYER_MODULES} ICON "${GE_PLAYER_ICON}")
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
    target_link_libraries(ge_platform_tests PRIVATE ge_play ge_runner)
    target_compile_definitions(ge_platform_tests PRIVATE GE_HAS_GEPLAY=1)
  endif()
  if(TARGET ge_tool_export)
    target_sources(ge_platform_tests PRIVATE
                   "${PROJECT_SOURCE_DIR}/tests/platform/sdl/test_tool_export.cpp")
    target_link_libraries(ge_platform_tests PRIVATE ge_tool_export)
  endif()
  ge_target_options(ge_platform_tests)
  add_test(NAME ge_platform_tests COMMAND ge_platform_tests)
endif()
