param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-ragdoll-visibility-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
function Extract-Definition([string]$text,[string]$pattern){
 $signature=[regex]::Match($text,$pattern)
 if(-not $signature.Success){throw "Cannot find production definition: $pattern"}
 $bodyStart=$signature.Index+$signature.Length-1;$depth=1;$end=$bodyStart+1
 while($end-lt$text.Length-and$depth-gt0){if($text[$end]-eq'{'){$depth++}elseif($text[$end]-eq'}'){$depth--};$end++}
 if($depth-ne0){throw "Unterminated production definition: $pattern"}
 return $text.Substring($signature.Index,$end-$signature.Index)
}
$ragPath=Join-Path $StageRoot 'src\peds\VrRagdoll.cpp';$rag=[IO.File]::ReadAllText($ragPath)
$entityPath=Join-Path $StageRoot 'src\entities\Entity.cpp';$entity=[IO.File]::ReadAllText($entityPath)
$physical=[IO.File]::ReadAllText((Join-Path $StageRoot 'src\entities\Physical.cpp'))
$visibilityPath=Join-Path $StageRoot 'src\rw\VisibilityPlugins.cpp';$visibility=[IO.File]::ReadAllText($visibilityPath)
$populationPath=Join-Path $StageRoot 'src\peds\Population.cpp';$population=[IO.File]::ReadAllText($populationPath)
$parts=[Collections.Generic.List[string]]::new();$parts.Add('namespace VrRagdoll {')
foreach($name in @('FindSlot','Presentation','GetBounds','GetClumpBounds','RefreshBounds','LinkBounds','Release')){
 $parts.Add((Extract-Definition $rag ('(?m)^(?:static\s+)?(?:Slot\s*\*|VrRagdollSupport::Transform|bool|void)\s*'+[regex]::Escape($name)+'\([^)]*\)\s*\{')))
}
$finish=[regex]::Match($rag,'(?ms)\tfor\(int s = 0; s < MAX_POSES; s\+\+\)\{\s+Slot \*slot = &slots\[s\];\s+if\(slot->ped == nil\) continue;\s+LinkBounds\(slot\);\s+\}')
if(-not$finish.Success){throw 'Cannot locate final all-pose bounds update.'}
$update=Extract-Definition $rag '(?m)^void\s+Update\(void\)\s*\{'
if([regex]::Matches($update,'RefreshBounds\(slot\);').Count-ne1-or-not[regex]::IsMatch($update,'RefreshBounds\(slot\);\s+slot->waitTime = 0.0f;')){throw 'Only selected simulation updates must rebuild skeleton bounds.'}
$parts.Add('static void ProductionFinishBoundsUpdate(){'+"`r`n"+$finish.Value+"`r`n}")
$getup=[regex]::Match($rag,'(?ms)CPed \*ped = slot->ped;\s+slot->ped = nil;\s+active--;\s+if\(ped->m_entryInfoList.first\) ped->RemoveAndAdd\(\);\s+continue;')
if(-not$getup.Success){throw 'Live getup must relinquish ownership before native sector relink.'}
$parts.Add('static void ProductionLiveGetup(Slot *slot){for(int once=0;once<1;once++){'+"`r`n"+$getup.Value+"`r`n}}")
$parts.Add('}')
foreach($pattern in @('(?m)^CVector\s+CEntity::GetBoundCentre\(void\)\s*\{','(?m)^void\s+CEntity::GetBoundCentre\(CVector &out\)\s*\{','(?m)^float\s+CEntity::GetBoundRadius\(void\)\s*\{','(?m)^bool\s+CEntity::GetIsOnScreen\(void\)\s*\{')){$parts.Add((Extract-Definition $entity $pattern))}
$parts.Add((Extract-Definition $physical '(?m)^CRect\s+CPhysical::GetBoundRect\(void\)\s*\{'))
$parts.Add((Extract-Definition $visibility '(?m)^RpAtomic\*\s+CVisibilityPlugins::RenderPedCB\(RpAtomic \*atomic\)\s*\{'))
foreach($name in @('IsVrPedSpotObservable','VrPedSpatialPosition','IsVrPedObservable')){$parts.Add((Extract-Definition $population ('(?m)^static\s+(?:CVector|bool)\s+'+$name+'\([^)]*\)\s*\{')))}
# Integration properties beyond the mocked scene/sector surfaces above.
if(-not[regex]::IsMatch($rag,'RefreshBounds\(slot\);\s+slot->boundsLinked = false;\s+slot->ped = ped;')){throw 'Capture must initialize bounds before publishing ragdoll owner.'}
if([regex]::Matches($rag,'LinkBounds\(slot\);').Count-ne1){throw 'Sector relink must remain in the deferred update, outside capture/hit/render callbacks.'}
if([regex]::Matches($population,'VrPedSpatialPosition\(ped\)').Count-lt2){throw 'Normal and emergency population cleanup must use actual ragdoll position.'}
$world=[IO.File]::ReadAllText((Join-Path $StageRoot 'src\core\World.cpp'))
if(-not[regex]::IsMatch($world,'VrRagdoll::Update\(\);\s+for\(CPtrNode \*node = ms_listMovingEntityPtrs.first;')){throw 'Ragdoll update must precede world traversal.'}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-visibility-production.inc'),($parts-join"`r`n"),[Text.UTF8Encoding]::new($false))
foreach($path in @($ragPath,$entityPath,$visibilityPath,$populationPath)){Write-Host ([IO.Path]::GetFileName($path)+' SHA256: '+(Get-FileHash -LiteralPath $path).Hash)}
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-visibility.cpp')
