# Runs the kernel's checkpatch.pl on every .c and .h file.
#
#   cmake -DPERL=<path> -DSOURCE_DIR=<dir> -DCHECKPATCH_DIR=<dir>
#         -P checkpatch.cmake
#
# checkpatch.pl and its two data files are GPL-2.0, so they never enter the
# repository. They are downloaded at one kernel release into CHECKPATCH_DIR,
# inside the build directory, and each is checked against its SHA-256.
# checkpatch.pl reads its options from .checkpatch.conf in SOURCE_DIR.

cmake_minimum_required(VERSION 3.25)

# The commit of the kernel's v7.3 release tag.
set(kernel_commit c03c1a14507dfaf8b856c2608f9ac216021fc222)
set(kernel_url
	https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain)

# Each file name, followed by its SHA-256.
set(downloads
	checkpatch.pl
	2553cc1a601e70522e03fbce633d4e79fa5936f7f56a66de1899b7ddd247820a
	spelling.txt
	4095d4a8810f115bae1b7c0d8a1946beb3435f6e22d9a48ac009bb024bad1e68
	const_structs.checkpatch
	ea064f6916a74763468037494aeb270aae34b7c97617e84d424ca5b8733539b2)

if(NOT PERL)
	message(FATAL_ERROR "Perl not found. Install Perl and configure again.")
endif()

file(MAKE_DIRECTORY ${CHECKPATCH_DIR})

while(downloads)
	list(POP_FRONT downloads name hash)
	set(path ${CHECKPATCH_DIR}/${name})

	if(EXISTS ${path})
		file(SHA256 ${path} actual)
		if(actual STREQUAL hash)
			continue()
		endif()
		file(REMOVE ${path})
	endif()

	# Download beside the final path, so that an interrupted or bad
	# download never leaves a file that looks complete.
	message(STATUS "Downloading ${name} at kernel commit ${kernel_commit}")
	file(DOWNLOAD
		"${kernel_url}/scripts/${name}?id=${kernel_commit}"
		${path}.part
		EXPECTED_HASH SHA256=${hash}
		TLS_VERIFY ON
		STATUS status)
	list(GET status 0 code)
	if(NOT code EQUAL 0)
		file(REMOVE ${path}.part)
		list(GET status 1 reason)
		message(FATAL_ERROR "Downloading ${name} failed: ${reason}")
	endif()
	file(RENAME ${path}.part ${path})
endwhile()

include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
ninefold_list_sources(c h)

# .checkpatch.conf ignores SPDX_LICENSE_TAG, because checkpatch wants the
# // form in .c files. That also turns off its check for a missing tag, so
# the first line of each file is checked here instead.
set(spdx "/* SPDX-License-Identifier: Zlib */")
string(LENGTH "${spdx}\n" spdx_length)
set(missing)
foreach(file IN LISTS ninefold_sources)
	file(READ ${SOURCE_DIR}/${file} first LIMIT ${spdx_length})
	if(NOT first STREQUAL "${spdx}\n")
		list(APPEND missing ${file})
	endif()
endforeach()

execute_process(
	COMMAND ${PERL} ${CHECKPATCH_DIR}/checkpatch.pl -f ${ninefold_sources}
	WORKING_DIRECTORY ${SOURCE_DIR}
	RESULT_VARIABLE result)

if(missing)
	list(JOIN missing "\n  " lines)
	message(SEND_ERROR
		"These files do not start with the line ${spdx}:\n  ${lines}")
endif()
if(NOT result EQUAL 0)
	message(FATAL_ERROR "checkpatch found problems")
endif()
