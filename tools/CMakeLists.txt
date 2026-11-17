add_library(gectl_lib STATIC gectl/gectl.cpp)
target_include_directories(gectl_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gectl")
target_link_libraries(gectl_lib PUBLIC ge::module ge::core)
ge_target_options(gectl_lib)

add_executable(gectl gectl/main.cpp)
target_link_libraries(gectl PRIVATE gectl_lib)
ge_target_options(gectl)
