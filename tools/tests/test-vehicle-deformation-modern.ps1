param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [Parameter(Mandatory=$true)][string]$GameDataRoot,
 [switch]$UseBaseline,
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-real-body-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$dir=Join-Path $StageRoot 'src/vehicles';if($UseBaseline){$dir=Join-Path $StageRoot 'baseline-qbuild/src/vehicles'}
if($UseBaseline -and -not(Test-Path -LiteralPath (Join-Path $dir 'VehicleDeformation.cpp'))){$dir=Join-Path $StageRoot 'baseline/src/vehicles'}
if(-not $UseBaseline -and -not(Test-Path -LiteralPath (Join-Path $dir 'VehicleDeformation.cpp'))){$dir=Join-Path $StageRoot 'overlay/src/vehicles'}
if(-not(Test-Path -LiteralPath (Join-Path $dir 'VehicleDeformation.cpp'))){throw 'StageRoot must contain the source kit overlay or assembled engine; UseBaseline additionally requires baseline-qbuild/src/vehicles or baseline/src/vehicles.'}
$source=[IO.File]::ReadAllText((Join-Path $dir 'VehicleDeformation.cpp'));$start=$source.IndexOf('namespace VehicleDeformation {');$end=$source.LastIndexOf("`n#else")
if($start-lt0-or$end-le$start){throw 'Missing full production module'}
$includes='';foreach($h in @('VehicleDeformation.h','VehicleDeformationMesh.h')){$includes+='#include "'+[IO.Path]::GetFullPath((Join-Path $dir $h)).Replace('\','/')+'"'+"`n"}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-body-production.inc'),$includes+$source.Substring($start,$end-$start),[Text.UTF8Encoding]::new($false))
$native=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-vehicle-deformation-runtime.cpp'))
$native=$native.Substring(0,$native.IndexOf('static void DenseSkinny(')).Replace('vehicle-deformation-runtime-production.inc','vehicle-deformation-body-production.inc').Replace('NUM_FIRST_MATERIALS=8,NUM_SECOND_MATERIALS=8','NUM_FIRST_MATERIALS=64,NUM_SECOND_MATERIALS=64')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-body-native.inc'),$native,[Text.UTF8Encoding]::new($false))
$reader=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-vehicle-deformation-realmesh.cpp'));$reader=$reader.Substring(0,$reader.IndexOf('static float Area(')).Replace('#include "vehicle-deformation-mesh-production.inc"','')
$selection=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-vehicle-deformation-selection.cpp'));$selection=$selection.Substring($selection.IndexOf('struct Bound {'),$selection.IndexOf('static std::vector<Selected> RankPanels(')-$selection.IndexOf('struct Bound {'))
$col=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-vehicle-deformation-colmesh.cpp'));$col=$col.Substring($col.IndexOf('struct SphereContact'),$col.IndexOf('static int CollapsedFrame(')-$col.IndexOf('struct SphereContact'))
$reader=($reader+"`n"+$selection+"`n"+$col).Replace('namespace D=VehicleDeformationDetail;','namespace M=VehicleDeformationDetail;').Replace('D::','M::').Replace('Check(','DataCheck(').Replace('static unsigned checks;','static unsigned dataChecks;').Replace('++checks','++dataChecks')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-body-reader.inc'),$reader,[Text.UTF8Encoding]::new($false))
$previous=$env:VC_DEFORMATION_TEST_GAMEDATA
try{
 $env:VC_DEFORMATION_TEST_GAMEDATA=[IO.Path]::GetFullPath($GameDataRoot)
 foreach($h in @('VehicleDeformation.cpp','VehicleDeformationMesh.h')){Write-Host ($h+' SHA256: '+(Get-FileHash -LiteralPath (Join-Path $dir $h)).Hash)}
 & (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-modern.cpp')
}finally{$env:VC_DEFORMATION_TEST_GAMEDATA=$previous}
