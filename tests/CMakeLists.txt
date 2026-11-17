add_executable(
  ge_tests
  support/main.cpp
  core/test_hash.cpp
  core/test_random.cpp
  core/test_version.cpp
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
target_link_libraries(ge_tests PRIVATE ge::app)
target_compile_definitions(ge_tests PRIVATE GE_EXPECTED_VERSION="${PROJECT_VERSION}")
ge_target_options(ge_tests)

add_test(NAME ge_tests COMMAND ge_tests)
