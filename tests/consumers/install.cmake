# Installs the build in BUILD_DIR into a fresh PREFIX, for the find_package
# consumer test, and checks that the licences went with it.
#
#   cmake -DBUILD_DIR=<dir> -DPREFIX=<dir> [-DCONFIG=<config>]
#         -P install.cmake

cmake_minimum_required(VERSION 3.25)

file(REMOVE_RECURSE ${PREFIX})

set(config_args)
if(CONFIG)
	set(config_args --config ${CONFIG})
endif()

execute_process(
	COMMAND ${CMAKE_COMMAND} --install ${BUILD_DIR} --prefix ${PREFIX}
		${config_args}
	COMMAND_ERROR_IS_FATAL ANY)

# The library's licence, and that of the code copied into it.
foreach(file IN ITEMS
		share/doc/ninefold/LICENSE
		share/doc/ninefold/third_party/README.md
		share/doc/ninefold/third_party/ryu/LICENSE-Boost
		share/doc/ninefold/third_party/ffc/LICENSE-BOOST)
	if(NOT EXISTS ${PREFIX}/${file})
		message(FATAL_ERROR "The install has no ${file}")
	endif()
endforeach()
