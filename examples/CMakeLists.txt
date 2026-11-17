add_executable(hello_ecs hello_ecs.cpp)
target_link_libraries(hello_ecs PRIVATE ge::app)
ge_target_options(hello_ecs)

# Running the example as a test keeps it from rotting.
if(GE_BUILD_TESTS)
  add_test(NAME example_hello_ecs COMMAND hello_ecs)
endif()
