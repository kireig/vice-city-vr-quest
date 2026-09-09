param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$ProductionRoot=$StageRoot,
 [Parameter(Mandatory=$true)][string]$GameDataRoot,
 [string]$OutDir=(Join-Path $StageRoot 'checks-colmesh')
)
$ErrorActionPreference='Stop'
$header=Join-Path $ProductionRoot 'src/vehicles/VehicleDeformationMesh.h'
if(-not(Test-Path -LiteralPath $header)){$header=Join-Path $ProductionRoot 'overlay/src/vehicles/VehicleDeformationMesh.h'}
if(-not(Test-Path -LiteralPath $header)){throw 'ProductionRoot must contain the intended production deformation helper.'}
$reader=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-vehicle-deformation-selection.cpp'))
if(-not $reader.Contains('int main(){try{')){throw 'Expected standalone selection main for read-only parser reuse.'}
$reader=$reader.Replace('int main(){try{','int PreviousSelectionFixtureMain(){try{')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-selection-reader.inc'),$reader,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-mesh-production.inc'),('#include "'+[IO.Path]::GetFullPath($header).Replace('\','/')+'"'),[Text.UTF8Encoding]::new($false))
$previous=$env:VC_DEFORMATION_TEST_GAMEDATA
try{
 $env:VC_DEFORMATION_TEST_GAMEDATA=[IO.Path]::GetFullPath($GameDataRoot)
 Write-Host ('Deformation helper SHA256: '+(Get-FileHash -LiteralPath $header).Hash)
 & (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-colmesh.cpp')
 if($LASTEXITCODE -ne 0){throw 'Actual COL-vs-render diagnostic failed.'}
}finally{$env:VC_DEFORMATION_TEST_GAMEDATA=$previous}
