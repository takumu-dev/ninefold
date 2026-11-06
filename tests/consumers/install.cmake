# Installs the build in BUILD_DIR into a fresh PREFIX, for the find_package
# consumer test.
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
