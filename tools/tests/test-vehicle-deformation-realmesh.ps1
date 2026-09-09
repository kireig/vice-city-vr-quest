param(
 [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [Parameter(Mandatory=$true)][string]$GameDataRoot,
 [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-deformation-realmesh-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$header=Join-Path $StageRoot 'src/vehicles/VehicleDeformationMesh.h'
if(-not(Test-Path -LiteralPath $header)){$header=Join-Path $StageRoot 'overlay/src/vehicles/VehicleDeformationMesh.h'}
if(-not(Test-Path -LiteralPath $header)){throw 'StageRoot must contain the source kit overlay or assembled engine.'}
$header=[IO.Path]::GetFullPath($header)
foreach($name in @('gta3.img','gta3.dir')){
 if(-not(Test-Path -LiteralPath (Join-Path $GameDataRoot ('models/'+$name)))){throw 'Supply your own local classic game data with -GameDataRoot.'}
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-mesh-production.inc'),('#include "'+$header.Replace('\','/')+'"'),[Text.UTF8Encoding]::new($false))
$oldDataPath=$env:VC_DEFORMATION_TEST_GAMEDATA
try{
 $env:VC_DEFORMATION_TEST_GAMEDATA=[IO.Path]::GetFullPath($GameDataRoot)
 Write-Host ('Production mesh SHA256: '+(Get-FileHash -LiteralPath $header).Hash)
 & (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-realmesh.cpp')
 if($LASTEXITCODE -ne 0){throw 'Real model deformation test failed.'}
}finally{$env:VC_DEFORMATION_TEST_GAMEDATA=$oldDataPath}
