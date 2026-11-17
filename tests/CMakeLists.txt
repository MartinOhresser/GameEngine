add_executable(
  ge_tests
  support/main.cpp
  core/test_hash.cpp
  core/test_random.cpp
  core/test_version.cpp
  game/test_game.cpp
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
  app/test_determinism.cpp
  app/test_schedule_order.cpp
  app/test_time.cpp
  app/test_plugin_flow.cpp)
target_link_libraries(ge_tests PRIVATE ge::app ge::json ge::module ge::game ge::mod::transform2d
                      ge::mod::lifetime ge::mod::screen_wrap)
target_compile_definitions(ge_tests PRIVATE GE_EXPECTED_VERSION="${PROJECT_VERSION}")
if(GE_BUILD_TOOLS)
  target_sources(ge_tests PRIVATE tools/test_gectl.cpp)
  target_link_libraries(ge_tests PRIVATE gectl_lib)
  target_include_directories(ge_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools/gectl")
  target_compile_definitions(ge_tests PRIVATE GE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
ge_target_options(ge_tests)

add_test(NAME ge_tests COMMAND ge_tests)
