add_executable(
  ge_tests
  support/main.cpp
  core/test_hash.cpp
  core/test_random.cpp
  core/test_version.cpp
  game/test_atlas.cpp
  game/test_game.cpp
  game/test_lists.cpp
  game/test_scene_switch.cpp
  game/test_strings.cpp
  game/test_scenario.cpp
  game/test_schema.cpp
  json/test_json.cpp
  module/test_generate.cpp
  module/test_spec.cpp
  modules/test_example_modules.cpp
  ecs/test_commands.cpp
  ecs/test_entity_allocator.cpp
  ecs/test_events.cpp
  ecs/test_snapshot.cpp
  ecs/test_sparse_set.cpp
  ecs/test_world.cpp
  app/test_app.cpp
  app/test_commands_events.cpp
  app/test_input.cpp
  app/test_text.cpp
  app/test_render.cpp
  app/test_determinism.cpp
  app/test_schedule_order.cpp
  app/test_time.cpp
  app/test_plugin_flow.cpp
  platform/test_backend.cpp
  platform/test_input_map.cpp)
target_link_libraries(ge_tests PRIVATE ge::app ge::json ge::module ge::game ge::platform)
target_include_directories(ge_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")

# Every module is linked, and its own tests (modules/<name>/tests/*.cpp) run
# with the rest: a module cannot be added without a place to test it.
get_property(all_modules GLOBAL PROPERTY GE_ALL_MODULES)
foreach(module_name IN LISTS all_modules)
  target_link_libraries(ge_tests PRIVATE ge::mod::${module_name})
  file(GLOB module_tests CONFIGURE_DEPENDS
       "${PROJECT_SOURCE_DIR}/modules/${module_name}/tests/*.cpp")
  target_sources(ge_tests PRIVATE ${module_tests})
endforeach()
target_compile_definitions(ge_tests PRIVATE GE_EXPECTED_VERSION="${PROJECT_VERSION}")
if(GE_BUILD_TOOLS)
  target_sources(ge_tests PRIVATE tools/test_gectl.cpp runner/test_runner.cpp)
  target_link_libraries(ge_tests PRIVATE gectl_lib ge::runner)
  target_include_directories(ge_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools/gectl")
  target_compile_definitions(ge_tests PRIVATE GE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
ge_target_options(ge_tests)

add_test(NAME ge_tests COMMAND ge_tests)
