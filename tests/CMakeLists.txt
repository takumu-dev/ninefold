find_package(Threads REQUIRED)

add_library(ninefold_test_support STATIC support/recorder.c support/thread.c)
target_include_directories(ninefold_test_support PUBLIC support)
target_link_libraries(ninefold_test_support
	PUBLIC ninefold::ninefold Threads::Threads)

# One executable per test, so that a crash ends only that test. CTest
# judges each by its exit code.
function(ninefold_add_test name)
	add_executable(test_${name} ${name}.c)
	target_link_libraries(test_${name}
		PRIVATE ninefold::ninefold ninefold_test_support ninefold_demo)
	add_test(NAME ${name} COMMAND test_${name})
endfunction()

ninefold_add_test(tree)
ninefold_add_test(tree_threads)
ninefold_add_test(modes)
ninefold_add_test(mount)
ninefold_add_test(paths)
ninefold_add_test(value_file)

if(PROJECT_IS_TOP_LEVEL)
	add_executable(test_cxx_header cxx/header.cpp)
	set_target_properties(test_cxx_header PROPERTIES
		CXX_STANDARD 11
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF)
	target_link_libraries(test_cxx_header PRIVATE ninefold::ninefold)
	add_test(NAME cxx_header COMMAND test_cxx_header)
endif()

# The consumer projects build with this build's generator, compilers and
# flags, so that a sanitizer build links.
set(consumer_dir ${CMAKE_CURRENT_BINARY_DIR}/consumers)
set(consumer_options
	-DCMAKE_BUILD_TYPE=$<CONFIG>
	-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
	-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
	-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}
	-DCMAKE_COMPILE_WARNING_AS_ERROR=${CMAKE_COMPILE_WARNING_AS_ERROR})
foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
	list(APPEND consumer_options
		-DCMAKE_C_FLAGS_${config}=${CMAKE_C_FLAGS_${config}}
		-DCMAKE_EXE_LINKER_FLAGS_${config}=${CMAKE_EXE_LINKER_FLAGS_${config}})
endforeach()

function(ninefold_add_consumer_test name)
	add_test(NAME consumer_${name}
		COMMAND ${CMAKE_CTEST_COMMAND}
			--build-and-test
				${CMAKE_CURRENT_SOURCE_DIR}/consumers/${name}
				${consumer_dir}/${name}
			--build-generator ${CMAKE_GENERATOR}
			--build-makeprogram ${CMAKE_MAKE_PROGRAM}
			--build-config $<CONFIG>
			--build-options ${consumer_options} ${ARGN}
			--test-command consumer)
endfunction()

ninefold_add_consumer_test(subdirectory
	-DNINEFOLD_SOURCE_DIR=${PROJECT_SOURCE_DIR})

if(NINEFOLD_INSTALL)
	add_test(NAME consumer_install
		COMMAND ${CMAKE_COMMAND}
			-DBUILD_DIR=${PROJECT_BINARY_DIR}
			-DPREFIX=${consumer_dir}/prefix
			-DCONFIG=$<CONFIG>
			-P ${CMAKE_CURRENT_SOURCE_DIR}/consumers/install.cmake)
	set_tests_properties(consumer_install PROPERTIES
		FIXTURES_SETUP consumer_prefix)

	ninefold_add_consumer_test(find-package
		-DCMAKE_PREFIX_PATH=${consumer_dir}/prefix)
	set_tests_properties(consumer_find-package PROPERTIES
		FIXTURES_REQUIRED consumer_prefix)
endif()
