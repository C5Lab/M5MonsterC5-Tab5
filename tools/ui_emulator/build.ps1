[CmdletBinding()]
param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
if ($Jobs -lt 1) { throw 'Jobs must be a positive integer.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$image = 'tab5-ui-emulator:emsdk-4.0.14'
docker build --tag $image --file (Join-Path $PSScriptRoot 'Dockerfile') $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'Emulator compiler image build failed.' }
docker run --rm --mount "type=bind,source=$repoRoot,target=/repo" --env "EMULATOR_JOBS=$Jobs" $image
if ($LASTEXITCODE -ne 0) { throw 'Emulator build failed.' }
Write-Host "Static emulator: $PSScriptRoot/dist/index.html"
