# The smoke test of the demo program: it runs PROGRAM and passes when the
# program exits with 0 and prints a line for each node below.
#
#	cmake -DPROGRAM=<path> -P smoke.cmake

set(lines
	"/ dir 0"
	"/world dir 0"
	"/world/components/transform/pos file 36"
	"/world/components/transform/ids file 12"
	"/world/entities dir 0"
	"/world/entities/0/components/transform/pos file 12"
	"/world/entities/2/components/transform/pos file 12"
	"/world/entities/3/components/transform/pos file 12"
	"/sandbox dir 0"
	"/sandbox/world dir 0"
	"/sandbox/world/components/transform/pos file 24"
	"/sandbox/world/entities/0/components/transform/pos file 12"
	"/sandbox/world/entities/2/components/transform/pos file 12")

execute_process(COMMAND ${PROGRAM}
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT result EQUAL 0)
	message(FATAL_ERROR "The demo program exited with ${result}")
endif()

# Each line must be a whole line of the output.
string(REPLACE "\r\n" "\n" output "\n${output}")
foreach(line IN LISTS lines)
	string(FIND "${output}" "\n${line}\n" at)
	if(at EQUAL -1)
		message(FATAL_ERROR "The demo program printed no line \"${line}\"")
	endif()
endforeach()

# The destroyed entity is gone.
string(FIND "${output}" "\n/world/entities/1 " at)
if(NOT at EQUAL -1)
	message(FATAL_ERROR "The demo program printed destroyed entity 1")
endif()
