# Checks the licence of the third-party files copied into third_party/.
#
#   cmake -DSOURCE_DIR=<dir> -P licences.cmake
#
# The core takes copied code only under the Boost Software License 1.0,
# which puts no condition on object code. Each .c and .h file under
# third_party/ must grant that licence in its opening comment, and each
# project's directory there must hold the licence text, in a file named
# LICENSE-Boost in any case. It fails, naming each file at fault, if not.

cmake_minimum_required(VERSION 3.25)

set(third_party ${SOURCE_DIR}/third_party)

# The two ways the copied projects name the licence: "the Boost Software
# License, Version 1.0" and "Boost Software License - Version 1.0".
set(grant "Boost Software License[ ,-]+Version 1\\.0")

# The SHA-256 of the licence text, as boost.org gives it.
set(licence_sha256
	c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566)

# Sets out to the opening comment of the file at path: a /* */ comment, or
# the run of // lines and blank lines at its start.
function(opening_comment path out)
	file(READ ${path} text LIMIT 16384)
	if(text MATCHES "^[ \t\r\n]*/\\*")
		string(FIND "${text}" "*/" end)
		string(SUBSTRING "${text}" 0 ${end} comment)
	else()
		string(REGEX MATCH "^(([ \t]*//[^\n]*)?\r?\n)*" comment "${text}")
	endif()
	set(${out} "${comment}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE copied RELATIVE ${SOURCE_DIR}
	${third_party}/*.c ${third_party}/*.h)
list(SORT copied)
if(NOT copied)
	message(FATAL_ERROR "No copied .c or .h files in ${third_party}")
endif()

set(bad)
foreach(file IN LISTS copied)
	opening_comment(${SOURCE_DIR}/${file} comment)
	if(NOT comment MATCHES "${grant}")
		list(APPEND bad ${file})
	endif()
endforeach()

file(GLOB projects LIST_DIRECTORIES true ${third_party}/*)
set(missing)
foreach(project IN LISTS projects)
	if(NOT IS_DIRECTORY ${project})
		continue()
	endif()

	set(found FALSE)
	file(GLOB files ${project}/*)
	foreach(path IN LISTS files)
		get_filename_component(name ${path} NAME)
		string(TOLOWER "${name}" name)
		if(name STREQUAL "license-boost")
			file(SHA256 ${path} actual)
			if(actual STREQUAL licence_sha256)
				set(found TRUE)
			endif()
		endif()
	endforeach()
	if(NOT found)
		file(RELATIVE_PATH project ${SOURCE_DIR} ${project})
		list(APPEND missing ${project})
	endif()
endforeach()

if(bad)
	list(JOIN bad "\n  " lines)
	message(SEND_ERROR
		"These copied files do not grant the Boost Software License "
		"1.0 in their opening comment:\n  ${lines}")
endif()
if(missing)
	list(JOIN missing "\n  " lines)
	message(SEND_ERROR
		"These directories hold no LICENSE-Boost with the text of the "
		"Boost Software License 1.0:\n  ${lines}")
endif()
if(bad OR missing)
	message(FATAL_ERROR "The licence check failed")
endif()

list(LENGTH copied count)
message(STATUS "${count} copied files, each under the Boost licence")
