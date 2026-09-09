param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-deformation-runtime-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$directory=Join-Path $StageRoot 'src\vehicles'
if (-not (Test-Path -LiteralPath (Join-Path $directory 'VehicleDeformation.cpp'))) {$directory=Join-Path $StageRoot 'overlay\src\vehicles'}
$source=[IO.File]::ReadAllText((Join-Path $directory 'VehicleDeformation.cpp'))
$start=$source.IndexOf('namespace VehicleDeformation {')
$finish=$source.LastIndexOf("`n#else")
if ($start -lt 0 -or $finish -le $start) {throw 'Missing complete production platform implementation.'}
$includes='#include "'+((Join-Path $directory 'VehicleDeformation.h') -replace '\\','/')+'"'+"`r`n"+
 '#include "'+((Join-Path $directory 'VehicleDeformationMesh.h') -replace '\\','/')+'"'+"`r`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-runtime-production.inc'),$includes+$source.Substring($start,$finish-$start),[Text.UTF8Encoding]::new($false))
foreach($file in @('VehicleDeformation.cpp','VehicleDeformationMesh.h','VehicleDeformation.h')) {
 Write-Host ($file+' SHA256: '+(Get-FileHash -LiteralPath (Join-Path $directory $file) -Algorithm SHA256).Hash)
}
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-runtime.cpp')
