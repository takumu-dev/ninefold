# The demo file server, for the tests, the demo program and the benchmark.
# It does not install.
add_library(ninefold_demo STATIC demo.c)
target_include_directories(ninefold_demo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ninefold_demo PUBLIC ninefold::ninefold)

if(NINEFOLD_BUILD_DEMO)
	add_executable(ninefold_walk walk.c)
	target_link_libraries(ninefold_walk PRIVATE ninefold_demo)

	if(NINEFOLD_BUILD_TESTS)
		add_test(NAME demo_walk
			COMMAND ${CMAKE_COMMAND}
				-DPROGRAM=$<TARGET_FILE:ninefold_walk>
				-P ${CMAKE_CURRENT_SOURCE_DIR}/smoke.cmake)
	endif()
endif()
