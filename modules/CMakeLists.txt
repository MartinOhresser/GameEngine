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

# Every directory with a module.json is a module: adding one needs no edit
# here. The `depends` of its module.json become link dependencies.
file(GLOB module_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
     "${CMAKE_CURRENT_SOURCE_DIR}/*/module.json")
foreach(module_file IN LISTS module_files)
  get_filename_component(module_dir "${module_file}" DIRECTORY)
  get_filename_component(module_name "${module_dir}" NAME)
  file(READ "${module_file}" module_text)
  set(module_depends "")
  string(JSON depends_count ERROR_VARIABLE no_depends LENGTH "${module_text}" depends)
  if(NOT no_depends AND depends_count GREATER 0)
    math(EXPR last_depend "${depends_count} - 1")
    foreach(i RANGE ${last_depend})
      string(JSON dependency GET "${module_text}" depends ${i})
      list(APPEND module_depends "${dependency}")
    endforeach()
  endif()
  ge_add_module(${module_name} DEPENDS ${module_depends})
endforeach()
