# A compiler cache (ccache), used when it is installed: a fresh build folder, a branch switch or
# the CI rebuild in seconds what was already compiled once, SDL included. -DGE_CCACHE=OFF turns it
# off; a launcher given by the caller (CMAKE_CXX_COMPILER_LAUNCHER) is left alone.
#
# The Visual Studio generator has no launcher: there, nothing changes.

if(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

option(GE_CCACHE "Use ccache when it is installed" ON)
if(NOT GE_CCACHE
   OR CMAKE_CXX_COMPILER_LAUNCHER
   OR DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER}
   OR CMAKE_GENERATOR MATCHES "Visual Studio")
  return()
endif()

find_program(GE_CCACHE_PROGRAM ccache)
if(NOT GE_CCACHE_PROGRAM)
  return()
endif()

message(STATUS "Compiler cache: ${GE_CCACHE_PROGRAM}")
set(CMAKE_C_COMPILER_LAUNCHER "${GE_CCACHE_PROGRAM}")
set(CMAKE_CXX_COMPILER_LAUNCHER "${GE_CCACHE_PROGRAM}")

# ccache cannot keep what MSVC writes into a separate .pdb (/Zi): the debug information goes into
# the objects instead (/Z7), for SDL too (its own CMake policies would keep /Zi otherwise).
if(MSVC)
  set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()
