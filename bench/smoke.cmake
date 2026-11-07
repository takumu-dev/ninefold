# The smoke test of the benchmark: it runs PROGRAM for one batch, and passes
# when the program exits with 0, prints nothing on stderr, and prints a row
# for each measurement. It never judges the timings. The benchmark always
# exits with 0, and reports a failure to set up or a wrong read on stderr.
#
#	cmake -DPROGRAM=<path> -P smoke.cmake

# Each measurement, followed by its order.
set(rows
	"baseline read" id
	"baseline read" shuffled
	"baseline write" id
	"baseline write" shuffled
	"handle read" id
	"handle read" shuffled
	"handle write" id
	"handle write" shuffled
	"batched read" id
	"batched read" shuffled
	"alternating read" id
	"alternating read" shuffled
	"path lookup" id
	"path lookup" shuffled
	"memcpy out" -
	"memcpy in" -
	"bulk read" -
	"bulk write" -
	"kernel file" -)

execute_process(COMMAND ${PROGRAM} 1
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT result EQUAL 0)
	message(FATAL_ERROR "The benchmark exited with ${result}")
endif()
if(NOT errors STREQUAL "")
	message(FATAL_ERROR "The benchmark reported an error")
endif()

string(REPLACE "\r\n" "\n" output "\n${output}")
while(rows)
	list(POP_FRONT rows name order)
	if(NOT output MATCHES "\n${name} +${order} ")
		message(FATAL_ERROR
			"The benchmark printed no row \"${name}\", \"${order}\"")
	endif()
endwhile()
