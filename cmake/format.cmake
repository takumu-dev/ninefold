# Runs clang-format on every .c, .h and .cpp file.
#
#   cmake -DMODE=fix|check -DCLANG_FORMAT=<path> -DSOURCE_DIR=<dir>
#         -P format.cmake
#
# MODE=fix rewrites the files in place. MODE=check changes nothing, lists
# each file that is not formatted, and fails if there is one.

cmake_minimum_required(VERSION 3.25)

set(required_version 22.1.8)

if(NOT CLANG_FORMAT)
	message(FATAL_ERROR
		"clang-format not found. Install clang-format ${required_version} "
		"and configure again.")
endif()

# Other versions format some code differently, so only one is accepted.
execute_process(COMMAND ${CLANG_FORMAT} --version
	OUTPUT_VARIABLE version_text
	COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "." "\\." version_regex ${required_version})
if(NOT version_text MATCHES "version ${version_regex}([^0-9.]|$)")
	string(STRIP "${version_text}" version_text)
	message(FATAL_ERROR
		"${CLANG_FORMAT} is not clang-format ${required_version}:\n"
		"  ${version_text}")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
ninefold_list_sources(c h cpp)

if(MODE STREQUAL "fix")
	execute_process(COMMAND ${CLANG_FORMAT} -i ${ninefold_sources}
		WORKING_DIRECTORY ${SOURCE_DIR}
		COMMAND_ERROR_IS_FATAL ANY)
	return()
elseif(NOT MODE STREQUAL "check")
	message(FATAL_ERROR "MODE must be fix or check, not '${MODE}'")
endif()

set(unformatted)
foreach(file IN LISTS ninefold_sources)
	execute_process(COMMAND ${CLANG_FORMAT} --dry-run --Werror ${file}
		WORKING_DIRECTORY ${SOURCE_DIR}
		RESULT_VARIABLE result)
	if(NOT result EQUAL 0)
		list(APPEND unformatted ${file})
	endif()
endforeach()

if(unformatted)
	list(JOIN unformatted "\n  " lines)
	message(FATAL_ERROR
		"These files are not formatted:\n  ${lines}\n"
		"Run the format target to fix them.")
endif()
