# The smoke test of the demo program: it runs PROGRAM and passes when the
# program exits with 0 and prints each line below: one for each node, the
# types of the typed files, and the results of its text writes.
#
#	cmake -DPROGRAM=<path> -P smoke.cmake

set(lines
	"/ dir 0"
	"/world dir 0"
	"/world/components/transform/pos file 36"
	"    type vec3 f32 unit=m"
	"/world/components/transform/ids file 12"
	"/world/components/unit/health file 12"
	"    type i32 range=0..100 message=Health must be from 0 to 100."
	"/world/components/unit/alive file 3"
	"    type bool"
	"/world/components/unit/state file 3"
	"    type enum u8 values=idle:0,walk:1,run:2 message=Pick a known state."
	"/world/entities dir 0"
	"/world/entities/0/components/transform/pos file 12"
	"    text 0 0 0, type vec3 f32 unit=m"
	"/world/entities/0/components/unit/health file 4"
	"    text 0, type i32 range=0..100 message=Health must be from 0 to 100."
	"/world/entities/0/components/unit/alive file 1"
	"    text false, type bool"
	"/world/entities/0/components/unit/state file 1"
	"    text idle, type enum u8 values=idle:0,walk:1,run:2 message=Pick a known state."
	"/world/entities/2/components/transform/pos file 12"
	"    text 2 20 200, type vec3 f32 unit=m"
	"/world/entities/3/components/transform/pos file 12"
	"/sandbox dir 0"
	"/sandbox/world dir 0"
	"/sandbox/world/components/transform/pos file 24"
	"/sandbox/world/entities/0/components/transform/pos file 12"
	"    text 0.5 0.5 0.5, type vec3 f32 unit=m"
	"/sandbox/world/entities/2/components/transform/pos file 12"
	"write 75 to /world/entities/0/components/unit/health: ok"
	"read /world/entities/0/components/unit/health: 75"
	"write 150 to /world/entities/0/components/unit/health: EINVAL: Health must be from 0 to 100.")

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
