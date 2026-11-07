cmake_minimum_required(VERSION 3.25)

# No language yet: the lint configuration runs the style tools only and
# needs no compiler.
project(ninefold
	VERSION 0.1.0
	DESCRIPTION "An app's live state as a virtual file tree"
	LANGUAGES NONE)

option(NINEFOLD_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(NINEFOLD_BUILD_DEMO "Build the demo program" ${PROJECT_IS_TOP_LEVEL})
option(NINEFOLD_BUILD_BENCH "Build the benchmark" ${PROJECT_IS_TOP_LEVEL})
option(NINEFOLD_INSTALL "Generate the install rules" ${PROJECT_IS_TOP_LEVEL})

if(PROJECT_IS_TOP_LEVEL)
	include(cmake/lint.cmake)

	option(NINEFOLD_LINT_ONLY
		"Configure only the style check targets, with no compiler" OFF)
	mark_as_advanced(NINEFOLD_LINT_ONLY)
	if(NINEFOLD_LINT_ONLY)
		return()
	endif()
endif()

enable_language(C)
# The C++ compile test of the public header is the only C++ code. An app
# that embeds Ninefold stays C only.
if(PROJECT_IS_TOP_LEVEL AND NINEFOLD_BUILD_TESTS)
	enable_language(CXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Warnings stay warnings here. Only the presets turn them into errors, so a
# new compiler warning never breaks the build of an app.
if(MSVC)
	add_compile_options(/W4)
else()
	add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(ninefold STATIC
	src/dirs.c
	src/handle.c
	src/path.c
	src/tree.c)
add_library(ninefold::ninefold ALIAS ninefold)
target_sources(ninefold
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS include
		FILES include/ninefold.h)

# Before the demo directory, which holds the demo program's smoke test.
if(NINEFOLD_BUILD_TESTS)
	enable_testing()
endif()

if(NINEFOLD_BUILD_TESTS OR NINEFOLD_BUILD_DEMO OR NINEFOLD_BUILD_BENCH)
	add_subdirectory(demo)
endif()

if(NINEFOLD_BUILD_TESTS)
	add_subdirectory(tests)
endif()

if(NINEFOLD_BUILD_BENCH)
	add_subdirectory(bench)
endif()

if(NINEFOLD_INSTALL)
	include(GNUInstallDirs)
	include(CMakePackageConfigHelpers)

	set(config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/ninefold)

	install(TARGETS ninefold
		EXPORT ninefold-targets
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
	install(EXPORT ninefold-targets
		NAMESPACE ninefold::
		DESTINATION ${config_dir})

	configure_package_config_file(cmake/ninefold-config.cmake.in
		ninefold-config.cmake
		INSTALL_DESTINATION ${config_dir})
	# Any 0.x release can break the API, so only the same minor version
	# is compatible.
	write_basic_package_version_file(ninefold-config-version.cmake
		COMPATIBILITY SameMinorVersion)
	install(FILES
			${PROJECT_BINARY_DIR}/ninefold-config.cmake
			${PROJECT_BINARY_DIR}/ninefold-config-version.cmake
		DESTINATION ${config_dir})
endif()
