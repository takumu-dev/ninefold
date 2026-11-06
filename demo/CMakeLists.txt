# The demo file server, for the tests, the demo program and the benchmark.
# It does not install.
add_library(ninefold_demo STATIC demo.c)
target_include_directories(ninefold_demo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ninefold_demo PUBLIC ninefold::ninefold)
