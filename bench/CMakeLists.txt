# The benchmark. Its timings mean something only in a release build with
# link-time optimisation off, as the release presets build it: then each
# handle read is a real call into the static library, as in an app.
#
#	cmake --workflow --preset gcc-release
#	build/gcc-release/bench/ninefold_bench
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
	message(WARNING "Link-time optimisation is on, so the benchmark's "
		"calls into the library can be inlined and its timings are "
		"not those of an app.")
endif()

add_executable(ninefold_bench bench.c kernel_file.c)
target_link_libraries(ninefold_bench PRIVATE ninefold_demo)

# One batch of each measurement, so that a crash fails the tests but the
# timings never do.
if(NINEFOLD_BUILD_TESTS)
	add_test(NAME bench
		COMMAND ${CMAKE_COMMAND}
			-DPROGRAM=$<TARGET_FILE:ninefold_bench>
			-P ${CMAKE_CURRENT_SOURCE_DIR}/smoke.cmake)
endif()
