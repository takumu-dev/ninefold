# Sets ninefold_sources to every file under SOURCE_DIR with one of the
# extensions given, as paths relative to SOURCE_DIR.
#
# Only the source directories are searched, so build trees inside the
# repository are never checked.

function(ninefold_list_sources)
	set(dirs bench demo include src tests)
	set(patterns)
	foreach(dir IN LISTS dirs)
		foreach(ext IN LISTS ARGN)
			list(APPEND patterns ${SOURCE_DIR}/${dir}/*.${ext})
		endforeach()
	endforeach()

	file(GLOB_RECURSE files RELATIVE ${SOURCE_DIR} ${patterns})
	list(SORT files)
	set(ninefold_sources ${files} PARENT_SCOPE)
endfunction()
