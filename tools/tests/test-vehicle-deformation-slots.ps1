param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [switch]$UseBaseline,
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-deformation-slots-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$directory=Join-Path $StageRoot 'src/vehicles'
if(-not(Test-Path -LiteralPath (Join-Path $directory 'VehicleDeformation.cpp'))){$directory=Join-Path $StageRoot 'overlay/src/vehicles'}
$sourcePath=Join-Path $directory 'VehicleDeformation.cpp'
if($UseBaseline){$sourcePath=Join-Path $StageRoot 'baseline-qbuild/src/vehicles/VehicleDeformation.cpp'}
$source=[IO.File]::ReadAllText($sourcePath)
$start=$source.IndexOf('namespace VehicleDeformation {');$finish=$source.LastIndexOf("`n#else")
if($start -lt 0 -or $finish -le $start){throw 'Missing complete production implementation.'}
$includes=''
$includes+='#define SLOT_TEST_HAS_UPDATE '+[int]($source -match '(?m)^void Update\(\)')+"`n"
foreach($header in @('VehicleDeformation.h','VehicleDeformationMesh.h')){$includes+='#include "'+[IO.Path]::GetFullPath((Join-Path $directory $header)).Replace('\','/')+'"'+"`n"}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-slots-production.inc'),$includes+$source.Substring($start,$finish-$start),[Text.UTF8Encoding]::new($false))
# Reuse the exact full-module native adapters without overwriting the main
# runtime fixture or its generated include while the parent runs other tests.
$fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-vehicle-deformation-runtime.cpp'))
$fixture=$fixture.Replace('#include "vehicle-deformation-runtime-production.inc"','#include "vehicle-deformation-slots-production.inc"')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-slots-runtime-fixture.inc'),$fixture,[Text.UTF8Encoding]::new($false))
Write-Host ('Full production source: '+$sourcePath)
Write-Host ('VehicleDeformation.cpp SHA256: '+(Get-FileHash -LiteralPath $sourcePath).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-slots.cpp')
if($LASTEXITCODE -ne 0){throw 'Vehicle deformation slot starvation regression failed.'}
