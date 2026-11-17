add_executable(ge_tests support/main.cpp core/test_version.cpp)
target_link_libraries(ge_tests PRIVATE ge::core)
target_compile_definitions(ge_tests PRIVATE GE_EXPECTED_VERSION="${PROJECT_VERSION}")
ge_target_options(ge_tests)

add_test(NAME ge_tests COMMAND ge_tests)
