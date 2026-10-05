[CmdletBinding()]
param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
if ($Jobs -lt 1) { throw 'Jobs must be a positive integer.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$image = 'tab5-ui-emulator:emsdk-4.0.14'
# Docker writes normal progress to stderr; Windows PowerShell 5 otherwise
# promotes it to a terminating NativeCommandError under Stop.
$ErrorActionPreference = 'Continue'
docker build --tag $image --file (Join-Path $PSScriptRoot 'Dockerfile') $PSScriptRoot
$imageBuildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($imageBuildExit -ne 0) { throw 'Emulator compiler image build failed.' }
$ErrorActionPreference = 'Continue'
docker run --rm --mount "type=bind,source=$repoRoot,target=/repo" --env "EMULATOR_JOBS=$Jobs" $image
$emulatorBuildExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($emulatorBuildExit -ne 0) { throw 'Emulator build failed.' }
Write-Host "Static emulator: $PSScriptRoot/dist/index.html"
