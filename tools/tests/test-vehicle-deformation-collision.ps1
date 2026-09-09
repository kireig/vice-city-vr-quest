param(
 [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-deformation-collision-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$path=Join-Path $StageRoot 'src/entities/Physical.cpp'
if(-not(Test-Path -LiteralPath $path)){$path=Join-Path $StageRoot 'overlay/src/entities/Physical.cpp'}
$source=[IO.File]::ReadAllText($path)
$parts=[Collections.Generic.List[string]]::new()
$native=[regex]::Match($source,'(?ms)^void\s+CPhysical::SetDamagedPieceRecord\(.*?^\}')
$context=[regex]::Match($source,'(?ms)^struct VehicleDeformationCollisionContext \{.*?^\};')
$helper=[regex]::Match($source,'(?ms)^static inline void\s+RecordVehicleDeformationCollision\(.*?^\}')
$pair=[regex]::Match($source,'(?ms)^static inline void\s+RecordVehicleDeformationPair\(.*?^\}')
if(-not($native.Success -and $context.Success -and $helper.Success -and $pair.Success)){throw 'Production native record/context/helpers missing'}
$parts.Add($native.Value);$parts.Add($context.Value);$parts.Add($helper.Value);$parts.Add($pair.Value)
$snapshots=[regex]::Matches($source,'VehicleDeformationCollisionContext deformationContext\(A,B\);')
if($snapshots.Count -ne 3){throw 'Expected three manifold snapshot scopes (simple, static, dynamic)'}
$accepted='(?ms)if\(!A->(?<method>ApplyCollision|ApplyCollisionAlt)\(B, aColPoints\[i\], impulseA, (?:impulseB|moveSpeed, turnSpeed)\)\)\r?\n[ \t]*continue;\r?\n#if defined\(__ANDROID__\) && defined\(GTA_VR_WEAPONS\)\r?\n[ \t]*RecordVehicleDeformationPair\([^\r\n]+\);\r?\n#endif'
$sites=[regex]::Matches($source,$accepted)
if($sites.Count -ne 10){throw "Expected eight dynamic and two immovable response sites, got$($sites.Count)"}
$names=@();$static=@()
for($i=0;$i -lt $sites.Count;$i++){
 $name='ResponseSite'+$i;$names+=$name;$static+= $(if($sites[$i].Groups['method'].Value -eq 'ApplyCollisionAlt'){'true'}else{'false'})
 $parts.Add("static void $name(CPhysical *A,CPhysical *B,CColPoint *aColPoints,int count) {`nfloat impulseA=0,impulseB=0;CVector moveSpeed,turnSpeed;(void)impulseB;`n"+$snapshots[0].Value+"`nfor(int i=0;i<count;++i){`n"+$sites[$i].Value+"`n}}")
}
$parts.Add('static const Site sites[] = {'+($names -join ',')+'};')
$parts.Add('static const bool immovable[] = {'+($static -join ',')+'};')
# Keep the actual legacy source in this run's generated adapter for regression
# proof; published kits may omit that local snapshot and run current checks.
$baselinePath=Join-Path $StageRoot 'baseline-qbuild/src/entities/Physical.cpp'
if(Test-Path -LiteralPath $baselinePath){
 $baseline=[IO.File]::ReadAllText($baselinePath)
 $legacyHelper=[regex]::Match($baseline,'(?ms)^static inline void\s+RecordVehicleDeformationCollision\(.*?^\}')
 $early=[regex]::Match($baseline,'(?ms)if\(B->bIsInSafePosition\)\{\r?\n#if defined\(__ANDROID__\) && defined\(GTA_VR_WEAPONS\).*?B->UnsetIsInSafePosition\(\);\r?\n[ \t]*\}')
 $gatePattern='(?ms)(?<gate>if\([^\{\};]+\))\{\r?\n(?<indent>[ \t]*)(?<native>(?<actor>[AB])->SetDamagedPieceRecord\([^\r\n]+\);)\r?\n#if defined\(__ANDROID__\) && defined\(GTA_VR_WEAPONS\)\r?\n[ \t]*RecordVehicleDeformationCollision\([^\r\n]+\);\r?\n#endif\r?\n[ \t]*\}'
 $gates=[regex]::Matches($baseline,$gatePattern)
 if($gates.Count -ne 20 -or -not $legacyHelper.Success -or -not $early.Success){throw 'Expected native collision hook structure'}
 $parts.Add('#define HAVE_LEGACY_COLLISION 1')
 $parts.Add("namespace Legacy {`n"+$legacyHelper.Value)
 $parts.Add("static void EarlyRollback(CPhysical *A,CPhysical *B,CColPoint &colpoint,float impulseB){`n"+$early.Value+"`n}")
 $legacyNames=@()
 for($i=0;$i -lt $gates.Count;$i++){
  $name='DamageSite'+$i;$legacyNames+=$name
  $parts.Add("static void $name(CPhysical *A,CPhysical *B,CColPoint *aColPoints,float impulseA,float impulseB){`n(void)A;(void)B;(void)aColPoints;(void)impulseA;(void)impulseB;int i=0;`n"+$gates[$i].Value+"`n}")
 }
 $parts.Add('static const LegacySite sites[] = {'+($legacyNames -join ',')+'}; }')
 # Strip only optional deformation changes from both versions, preserving every
 # native force/torque/standard-damage/ragdoll statement for exact comparison.
 function Strip-Deformation([string]$text){
  $text=[regex]::Replace($text,'(?ms)^// (?:Capture the original contact|Every point in a manifold).*?^namespace \{','namespace {')
  $text=$text.Replace('#include "VehicleDeformation.h"','')
  $text=[regex]::Replace($text,'(?ms)^#if defined\(__ANDROID__\) && defined\(GTA_VR_WEAPONS\)\r?\n(?:(?!^#endif).)*(?:RecordVehicleDeformation|VehicleDeformationCollisionContext)(?:(?!^#endif).)*^#endif\r?\n','')
  $text=[regex]::Replace($text,'(?m)//[^\r\n]*','')
  return [regex]::Replace($text,'\s','')
 }
 if((Strip-Deformation $source) -cne (Strip-Deformation $baseline)){throw 'Native collision/ragdoll source changed outside optional deformation scope'}
 Write-Host 'Native forces, standard damage, rollback and ragdoll code token-identical after stripping optional hooks: PASS'
}
Write-Host ('Physical.cpp SHA256: '+(Get-FileHash -LiteralPath $path).Hash)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-collision-production.inc'),($parts -join "`n"),[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-collision.cpp')
if($LASTEXITCODE -ne 0){throw 'Production collision adapter failed'}
