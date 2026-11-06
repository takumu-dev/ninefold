#!/bin/sh
# Copies the logs that a failed CI job leaves in build/ into a directory,
# for CircleCI to keep, each under its path in the checkout: CTest's logs,
# and the configure logs of each build and of each consumer project that
# the tests build. CMake 3.26 and later write CMakeConfigureLog.yaml, and
# older versions CMakeOutput.log and CMakeError.log.
#
#   ci/gather-logs.sh <directory>
set -eu

destination=$1

if [ ! -d build ]; then
	exit 0
fi

find build -type f \( -path '*/Testing/Temporary/*' \
	-o -name CMakeConfigureLog.yaml \
	-o -name CMakeOutput.log \
	-o -name CMakeError.log \) |
	while read -r file; do
		mkdir -p "$destination/$(dirname "$file")"
		cp "$file" "$destination/$file"
	done
