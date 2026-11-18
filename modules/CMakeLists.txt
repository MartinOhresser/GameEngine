# Gameplay modules. Each one is a static library built from its generated code
# (modules/<name>/gen, produced by `gectl generate` and committed) and its
# hand-written systems (modules/<name>/src). Modules contain no platform code, so
# they build for every target the engine supports.

# ge_add_module(<name> [DEPENDS <module>...])
function(ge_add_module name)
  cmake_parse_arguments(MODULE "" "" "DEPENDS" ${ARGN})
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/gen/${name}.gen.cpp")
    message(WARNING "Module ${name} has no generated code: run `gectl generate`")
    return()
  endif()
  set(target ge_mod_${name})
  file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/src/*.cpp")
  add_library(${target} STATIC "${name}/gen/${name}.gen.cpp" ${sources})
  add_library(ge::mod::${name} ALIAS ${target})
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${name}/gen")
  target_link_libraries(${target} PUBLIC ge::app)
  foreach(dependency IN LISTS MODULE_DEPENDS)
    target_link_libraries(${target} PUBLIC ge::mod::${dependency})
  endforeach()
  ge_target_options(${target})
  # The universal runner links every module listed here.
  set_property(GLOBAL APPEND PROPERTY GE_ALL_MODULES ${name})
endfunction()

ge_add_module(transform2d)
ge_add_module(lifetime)
ge_add_module(screen_wrap DEPENDS transform2d)
ge_add_module(action_control DEPENDS transform2d)
