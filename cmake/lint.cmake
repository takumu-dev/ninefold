# Style check targets: format, check-format and checkpatch. Each runs a
# script in this directory, so the file list is gathered when the target
# runs and a new source file needs no reconfigure.

find_program(CLANG_FORMAT NAMES clang-format-22 clang-format)
find_package(Perl QUIET)

set(lint_dir ${CMAKE_CURRENT_LIST_DIR})

add_custom_target(format
	COMMAND ${CMAKE_COMMAND}
		-DMODE=fix
		-DCLANG_FORMAT=${CLANG_FORMAT}
		-DSOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${lint_dir}/format.cmake
	COMMENT "Formatting the sources"
	VERBATIM
	USES_TERMINAL)

add_custom_target(check-format
	COMMAND ${CMAKE_COMMAND}
		-DMODE=check
		-DCLANG_FORMAT=${CLANG_FORMAT}
		-DSOURCE_DIR=${PROJECT_SOURCE_DIR}
		-P ${lint_dir}/format.cmake
	COMMENT "Checking the formatting"
	VERBATIM
	USES_TERMINAL)

add_custom_target(checkpatch
	COMMAND ${CMAKE_COMMAND}
		-DPERL=${PERL_EXECUTABLE}
		-DSOURCE_DIR=${PROJECT_SOURCE_DIR}
		-DCHECKPATCH_DIR=${PROJECT_BINARY_DIR}/checkpatch
		-P ${lint_dir}/checkpatch.cmake
	COMMENT "Running checkpatch"
	VERBATIM
	USES_TERMINAL)
