cmake_minimum_required(VERSION 3.25)

project(game_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CROSSCOMPILING)
  set(GE_DEFAULT_TOOLS OFF)
else()
  set(GE_DEFAULT_TOOLS ON)
endif()

# Only build tests and examples when this is the top-level project, so the
# engine can be consumed with add_subdirectory() or FetchContent.
if(PROJECT_IS_TOP_LEVEL)
  option(GE_BUILD_TESTS "Build the test suite" ON)
  option(GE_BUILD_EXAMPLES "Build the examples" ON)
  # The tools run on the build machine, so they are skipped when cross-compiling.
  option(GE_BUILD_TOOLS "Build gectl and the tools" ${GE_DEFAULT_TOOLS})
else()
  option(GE_BUILD_TESTS "Build the test suite" OFF)
  option(GE_BUILD_EXAMPLES "Build the examples" OFF)
  option(GE_BUILD_TOOLS "Build gectl and the tools" OFF)
endif()

include(cmake/GeOptions.cmake)

add_subdirectory(src)
add_subdirectory(modules)

if(GE_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(GE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(GE_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
  add_subdirectory(examples)
endif()
