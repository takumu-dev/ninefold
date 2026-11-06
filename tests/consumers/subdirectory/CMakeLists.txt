# An app that adds a copy of Ninefold's source with add_subdirectory.
cmake_minimum_required(VERSION 3.25)
project(consumer LANGUAGES C)

add_subdirectory(${NINEFOLD_SOURCE_DIR} ninefold)

# Embedded this way, Ninefold builds only its library, in C only.
foreach(option TESTS DEMO BENCH)
	if(NINEFOLD_BUILD_${option})
		message(FATAL_ERROR "NINEFOLD_BUILD_${option} is on")
	endif()
endforeach()
if(NINEFOLD_INSTALL)
	message(FATAL_ERROR "NINEFOLD_INSTALL is on")
endif()

get_directory_property(targets
	DIRECTORY ${NINEFOLD_SOURCE_DIR} BUILDSYSTEM_TARGETS)
get_directory_property(subdirs
	DIRECTORY ${NINEFOLD_SOURCE_DIR} SUBDIRECTORIES)
if(NOT targets STREQUAL "ninefold" OR subdirs)
	message(FATAL_ERROR "Ninefold added more than its library: "
		"targets '${targets}', subdirectories '${subdirs}'")
endif()

get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
if(CXX IN_LIST languages)
	message(FATAL_ERROR "Ninefold enabled C++")
endif()

add_executable(consumer ../main.c)
target_link_libraries(consumer PRIVATE ninefold::ninefold)
