#!/bin/sh
# Installs the tools a Linux CI job needs, as root, in the Debian or Ubuntu
# image it runs in: Ninja, CMake, and the packages named on the command line.
#
#   ci/install-linux-tools.sh [--llvm <major>] [--cmake <version> <sha256>]
#           [--pip <requirement>]... [<package>...]
#
# --llvm installs Clang at that LLVM major from apt.llvm.org, and the
# packages named then come from there too. apt.llvm.org is signed with the
# key beside this script (fingerprint 6084 F3CF 814B 57C1 CF12 EFD5 15CF
# 4D18 AF4F 7421) and keeps no old builds, so a job gets the newest release
# of that major. Its bin directory goes first on the PATH, so that the
# presets' plain `clang` is that version.
#
# --cmake installs Kitware's build of that CMake version, checked against
# the SHA-256 given, in place of the distribution's.
#
# --pip installs a requirement, such as clang-format==22.1.8, into a virtual
# environment in /opt/venv, whose bin directory goes on the PATH.
#
# Each directory added to the PATH also goes into BASH_ENV, which CircleCI
# reads at the start of each later step.
# The package lists are split on spaces, but never globbed.
set -euf

here=$(dirname "$0")
llvm=
cmake_version=
cmake_sha256=
requirements=

while [ $# -gt 0 ]; do
	case $1 in
	--llvm)
		llvm=$2
		shift 2
		;;
	--cmake)
		cmake_version=$2
		cmake_sha256=$3
		shift 3
		;;
	--pip)
		requirements="$requirements $2"
		shift 2
		;;
	*)
		break
		;;
	esac
done

export DEBIAN_FRONTEND=noninteractive
. /etc/os-release

apt_install() {
	apt-get install --yes --no-install-recommends "$@"
}

path=
add_path() {
	path="$path$1:"
}

base="ca-certificates ninja-build"
if [ -n "$cmake_version" ]; then
	base="$base curl"
else
	base="$base cmake"
fi
if [ -n "$requirements" ]; then
	base="$base python3-venv"
fi

apt-get update
apt_install $base

if [ -n "$llvm" ]; then
	mkdir -p /etc/apt/keyrings
	cp "$here/apt.llvm.org.asc" /etc/apt/keyrings/apt.llvm.org.asc
	cat > /etc/apt/sources.list.d/apt.llvm.org.sources <<EOF
Types: deb
URIs: https://apt.llvm.org/$VERSION_CODENAME/
Suites: llvm-toolchain-$VERSION_CODENAME-$llvm
Components: main
Signed-By: /etc/apt/keyrings/apt.llvm.org.asc
EOF
	# The distribution may carry the same major with a higher build
	# number, so apt.llvm.org always wins.
	cat > /etc/apt/preferences.d/apt.llvm.org.pref <<EOF
Package: *
Pin: origin apt.llvm.org
Pin-Priority: 1001
EOF
	apt-get update
	set -- "clang-$llvm" "$@"
	add_path "/usr/lib/llvm-$llvm/bin"
fi

if [ $# -gt 0 ]; then
	apt_install "$@"
fi

if [ -n "$cmake_version" ]; then
	archive=cmake-$cmake_version-linux-x86_64.tar.gz
	releases=https://github.com/Kitware/CMake/releases/download
	curl --fail --silent --show-error --location --output "/tmp/$archive" \
		"$releases/v$cmake_version/$archive"
	echo "$cmake_sha256  /tmp/$archive" | sha256sum --check --strict
	mkdir -p /opt/cmake
	tar --extract --gzip --file "/tmp/$archive" --directory /opt/cmake \
		--strip-components 1
	rm "/tmp/$archive"
	add_path /opt/cmake/bin
fi

if [ -n "$requirements" ]; then
	python3 -m venv /opt/venv
	/opt/venv/bin/pip install --no-cache-dir --disable-pip-version-check \
		$requirements
	add_path /opt/venv/bin
fi

if [ -n "$path" ]; then
	export PATH="$path$PATH"
	if [ -n "${BASH_ENV:-}" ]; then
		echo "export PATH=\"$path\$PATH\"" >> "$BASH_ENV"
	fi
fi

cmake --version | head -n 1
echo "ninja $(ninja --version)"
if [ -n "$llvm" ]; then
	clang --version | head -n 1
fi
if [ -n "$requirements" ]; then
	/opt/venv/bin/pip freeze --all
fi
