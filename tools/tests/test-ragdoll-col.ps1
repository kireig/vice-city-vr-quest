param(
    [Parameter(Mandatory=$true)][string]$VehicleCol,
    [string]$KitRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-col-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$VehicleCol = [IO.Path]::GetFullPath($VehicleCol)
if (-not (Test-Path -LiteralPath $VehicleCol -PathType Leaf)) { throw 'Provide your own original Vice City vehicles.col.' }
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $KitRoot -OutDir $OutDir -Sources @('test-ragdoll-col.cpp')
$exe = Join-Path $OutDir 'test-ragdoll-col.exe'
& $exe $VehicleCol | Tee-Object -FilePath (Join-Path $OutDir 'classic-col-results.txt')
if ($LASTEXITCODE -ne 0) { throw "Classic COL probe failed: $LASTEXITCODE" }
Write-Output ('User-provided COL SHA256: ' + (Get-FileHash -LiteralPath $VehicleCol -Algorithm SHA256).Hash)
