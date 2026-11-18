# Build options shared by every target of the engine.

option(GE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(GE_COVERAGE "Instrument code for coverage (GCC or Clang)" OFF)
set(GE_SANITIZE "" CACHE STRING "Sanitizers to enable, for example: address;undefined")

if(NOT MSVC)
  if(GE_SANITIZE)
    string(REPLACE ";" "," _ge_sanitizers "${GE_SANITIZE}")
    add_compile_options(-fsanitize=${_ge_sanitizers} -fno-omit-frame-pointer
                        -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${_ge_sanitizers})
  endif()

  if(GE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
  endif()
elseif(GE_SANITIZE OR GE_COVERAGE)
  message(WARNING "GE_SANITIZE and GE_COVERAGE are ignored with MSVC")
endif()

# Applies the engine's warning policy to a target.
function(ge_target_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(GE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(
      ${target}
      PRIVATE -Wall
              -Wextra
              -Wpedantic
              -Wshadow
              -Wconversion
              -Wsign-conversion
              -Wold-style-cast
              -Wnon-virtual-dtor
              -Woverloaded-virtual
              -Wdouble-promotion
              -Wformat=2
              # No fused multiply-add: the same simulation must give the same
              # floating point results on every compiler and processor.
              -ffp-contract=off
              # The engine runs on mobile and consoles where games disable both.
              -fno-exceptions
              -fno-rtti)
    # GCC 13 reports false positives at -O2/-O3 on std::vector internals and on
    # branches it cannot prove dead, so these are enabled for Clang only (the
    # sanitizers and clang-tidy still cover real bugs).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE -Wnull-dereference)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${target} PRIVATE -Wno-stringop-overflow)
    endif()
    if(GE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
