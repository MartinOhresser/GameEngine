# The universal runner: every module of the repository linked into one program,
# so any game described by JSON files can be played during development.
# (An exported game links only the modules it uses: see ge_add_player.)

get_property(all_modules GLOBAL PROPERTY GE_ALL_MODULES)

ge_write_registry("${CMAKE_CURRENT_BINARY_DIR}/registry.cpp" ${all_modules})

add_library(ge_runner STATIC runner.cpp "${CMAKE_CURRENT_BINARY_DIR}/registry.cpp")
add_library(ge::runner ALIAS ge_runner)
target_include_directories(ge_runner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ge_runner PUBLIC ge::game ge::app)
foreach(name IN LISTS all_modules)
  target_link_libraries(ge_runner PUBLIC ge::mod::${name})
endforeach()
ge_target_options(ge_runner)

add_executable(gerun main.cpp)
target_link_libraries(gerun PRIVATE ge_runner)
ge_target_options(gerun)
