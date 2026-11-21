# The test of the licence check: it passes on the real third_party/, and it
# fails on copies of it where one file has another licence or the licence
# text is missing.
#
#	cmake -DSOURCE_DIR=<dir> -DWORK_DIR=<dir> -P licences.cmake

set(check ${SOURCE_DIR}/cmake/licences.cmake)

# Runs the check on the tree at dir, and sets result and output.
function(run_check dir)
	execute_process(COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${dir} -P ${check}
		RESULT_VARIABLE result
		OUTPUT_VARIABLE output
		ERROR_VARIABLE output)
	set(result ${result} PARENT_SCOPE)
	set(output "${output}" PARENT_SCOPE)
endfunction()

# Sets dir to a new copy of the real third_party/, under a tree of its own.
function(make_copy name)
	set(tree ${WORK_DIR}/${name})
	file(REMOVE_RECURSE ${tree})
	file(MAKE_DIRECTORY ${tree})
	file(COPY ${SOURCE_DIR}/third_party DESTINATION ${tree})
	set(dir ${tree} PARENT_SCOPE)
endfunction()

# The check of the tree at dir must fail and name what.
function(expect_failure dir what)
	run_check(${dir})
	if(result EQUAL 0)
		message(FATAL_ERROR "The check passed ${dir}:\n${output}")
	endif()
	string(FIND "${output}" "${what}" at)
	if(at EQUAL -1)
		message(FATAL_ERROR "The check did not name ${what}:\n${output}")
	endif()
endfunction()

run_check(${SOURCE_DIR})
if(NOT result EQUAL 0)
	message(FATAL_ERROR "The check failed on the real files:\n${output}")
endif()

# Ryu's file with its Apache licence only, without the Boost alternative.
make_copy(apache)
set(path ${dir}/third_party/ryu/ryu/f2s.c)
file(READ ${path} text)
string(REGEX REPLACE "// Alternatively.*LICENSE_1_0\\.txt\\)\n//\n" ""
	text "${text}")
file(WRITE ${path} "${text}")
expect_failure(${dir} "third_party/ryu/ryu/f2s.c")

# A file under the MIT licence only.
make_copy(mit)
file(WRITE ${dir}/third_party/ffc/ffc.h
	"// Copyright (c) 2021 The authors\n"
	"//\n"
	"// Licensed under the MIT License.\n"
	"\n"
	"int x;\n")
expect_failure(${dir} "third_party/ffc/ffc.h")

# A file that names the Boost licence, but only after its opening comment.
make_copy(late)
file(WRITE ${dir}/third_party/ffc/extra.h
	"// Licensed under the MIT License.\n"
	"\n"
	"/* Not the Boost Software License, Version 1.0. */\n")
expect_failure(${dir} "third_party/ffc/extra.h")

# A project with no licence text.
make_copy(no-text)
file(REMOVE ${dir}/third_party/ryu/LICENSE-Boost)
expect_failure(${dir} "third_party/ryu")

# A project whose licence text is not the Boost licence.
make_copy(other-text)
file(WRITE ${dir}/third_party/ffc/LICENSE-BOOST "MIT License\n")
expect_failure(${dir} "third_party/ffc")
