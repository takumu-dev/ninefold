# Copies the logs that a failed CI job leaves in build\ into a directory,
# for CircleCI to keep, as ci/gather-logs.sh does on Linux.
#
#   ci/gather-logs.ps1 <directory>
param(
	[Parameter(Mandatory)]
	[string] $Destination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath build -PathType Container)) {
	return
}

$root = (Get-Location).Path
$names = 'CMakeConfigureLog.yaml', 'CMakeOutput.log', 'CMakeError.log'
Get-ChildItem -LiteralPath build -Recurse -File | Where-Object {
	$names -contains $_.Name -or
		$_.FullName -match '[\\/]Testing[\\/]Temporary[\\/]'
} | ForEach-Object {
	$target = Join-Path $Destination $_.FullName.Substring($root.Length + 1)
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) |
		Out-Null
	Copy-Item -LiteralPath $_.FullName -Destination $target
}
