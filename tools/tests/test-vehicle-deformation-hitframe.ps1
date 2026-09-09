param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [switch]$UseBaseline,
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-deformation-hitframe-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$directory=Join-Path $StageRoot 'src/vehicles'
if($UseBaseline){$directory=Join-Path $StageRoot 'baseline-qbuild/src/vehicles'}
$sourcePath=Join-Path $directory 'VehicleDeformation.cpp';$source=[IO.File]::ReadAllText($sourcePath)
$start=$source.IndexOf('namespace VehicleDeformation {');$finish=$source.LastIndexOf("`n#else")
if($start-lt0-or$finish-le$start){throw 'Missing complete production implementation.'}
$includes='#define HITFRAME_HAS_COLLISION_POSE '+[int]($source -match 'const\s+CollisionPose\s*\*')+"`n"
foreach($header in @('VehicleDeformation.h','VehicleDeformationMesh.h')){$includes+='#include "'+[IO.Path]::GetFullPath((Join-Path $directory $header)).Replace('\','/')+'"'+"`n"}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-hitframe-production.inc'),$includes+$source.Substring($start,$finish-$start),[Text.UTF8Encoding]::new($false))
$fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-vehicle-deformation-runtime.cpp'))
$fixture=$fixture.Replace('#include "vehicle-deformation-runtime-production.inc"','#include "vehicle-deformation-hitframe-production.inc"')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-hitframe-runtime-fixture.inc'),$fixture,[Text.UTF8Encoding]::new($false))
Write-Host ('Full module SHA256: '+(Get-FileHash -LiteralPath $sourcePath).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-hitframe.cpp')
