# Builds and tests workflow presets with MSVC, on CircleCI's Windows image.
#
#   ci/build-windows.ps1 -Msvc 14.44 -CMake 4.4.4 -Ninja 1.13.2 `
#       -Presets msvc-debug, msvc-release, msvc-asan
#
# Finds Visual Studio 2022 with vswhere, enters its developer shell for
# amd64, and checks that the shell's MSVC is the toolset given. Installs
# CMake and Ninja from pip, at the versions given, into a virtual
# environment outside the checkout, ahead of Visual Studio's own on the
# PATH. Then runs the workflow of each preset in turn. Before msvc-asan, it
# checks that the toolset has MSVC's AddressSanitizer runtime, which every
# test of that preset loads.
param(
	[Parameter(Mandatory)]
	[ValidatePattern('^14\.\d+$')]
	[string] $Msvc,

	[Parameter(Mandatory)]
	[string] $CMake,

	[Parameter(Mandatory)]
	[string] $Ninja,

	[Parameter(Mandatory)]
	[string[]] $Presets
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Runs a program with the arguments that follow it, and fails if the
# program does. Windows PowerShell turns each line that a program writes to
# stderr into an error, which would stop the script, so only the exit code
# counts.
function Invoke-Native {
	$program = $args[0]
	$arguments = @($args | Select-Object -Skip 1)
	$ErrorActionPreference = 'Continue'
	& $program @arguments
	if ($LASTEXITCODE -ne 0) {
		throw ("build-windows: '$($args -join ' ')' failed with exit " +
			"code $LASTEXITCODE")
	}
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
	'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -version '[17.0,18.0)' `
	-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
	-property installationPath
if (-not $vs) {
	throw ('build-windows: vswhere found no Visual Studio 2022 with the ' +
		'C++ tools')
}

Import-Module (Join-Path $vs `
	'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
	-Arch amd64 -HostArch amd64
if ($env:VCToolsVersion -notlike "$Msvc.*") {
	throw ("build-windows: the developer shell of $vs has MSVC " +
		"$env:VCToolsVersion, not $Msvc")
}
Write-Host "build-windows: MSVC $env:VCToolsVersion, from $vs"

$venv = Join-Path $HOME 'ninefold-venv'
Invoke-Native python -m venv $venv
Invoke-Native (Join-Path $venv 'Scripts\python.exe') -m pip install `
	--disable-pip-version-check "cmake==$CMake" "ninja==$Ninja"
$env:PATH = "$(Join-Path $venv 'Scripts');$env:PATH"
Invoke-Native cmake --version
Invoke-Native ninja --version

foreach ($preset in $Presets) {
	if ($preset -eq 'msvc-asan') {
		$runtime = Join-Path $env:VCToolsInstallDir `
			'bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll'
		if (-not (Test-Path -LiteralPath $runtime -PathType Leaf)) {
			throw ("build-windows: MSVC $env:VCToolsVersion " +
				"has no AddressSanitizer runtime at " +
				"$runtime, so msvc-asan can't run. Install " +
				"the C++ AddressSanitizer component of " +
				"Visual Studio, or leave out msvc-asan.")
		}
	}
	Invoke-Native cmake --workflow --preset $preset
}
