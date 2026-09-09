param(
 [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-entry-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$src=Join-Path $StageRoot 'src'
if(-not(Test-Path -LiteralPath $src)){$src=Join-Path $StageRoot 'overlay/src'}
$ped=[IO.File]::ReadAllText((Join-Path $src 'peds/Ped.cpp'))
$ai=[IO.File]::ReadAllText((Join-Path $src 'peds/PedAI.cpp'))
$prologue=[regex]::Match($ped,'(?ms)(?<=CPed::ProcessControl\(void\)\r?\n\{\r?\n).*?(?=\tCColPoint foundCol;)').Value
if($prologue -notmatch 'vrEntryCancelInput.Update'){throw 'Missing production Quest entry observer.'}
$cancel=[regex]::Match($ped,'(?ms)^#ifdef CANCELLABLE_CAR_ENTER\r?\n\s*static bool cancelJack.*?^#endif(?=\r?\n\r?\n\s*switch \(m_nPedState\))').Value
if(-not $cancel){throw 'Missing production cancellation block.'}
$production='void CPed::ProcessEntryCancellation() {'+"`n"+$prologue+$cancel+"`n}`n"
# The exact former block supplies an executable regression demonstration.
$baseline=Join-Path $StageRoot 'baseline-qbuild/src/peds/Ped.cpp'
if(Test-Path -LiteralPath $baseline){
 $baselineText=[IO.File]::ReadAllText($baseline)
 $old=[regex]::Match($baselineText,'(?ms)^#ifdef CANCELLABLE_CAR_ENTER\r?\n\s*static bool cancelJack.*?^#endif(?=\r?\n\r?\n\s*switch \(m_nPedState\))').Value
 if(-not $old){throw 'Missing baseline cancellation block.'}
 $production+='void CPed::ProcessBaselineCancellation() {'+"`n"+$old+"`n}`n"
 $preserved=$ped.Replace($prologue,'').Replace($cancel,$old)
 $preserved=$preserved -replace '(?m)^#if defined\(__ANDROID__\) && defined\(GTA_VR_WEAPONS\) && defined\(CANCELLABLE_CAR_ENTER\)\r?\n#include "VrVehicleEntry.h"\r?\n#endif\r?\n',''
 if(($preserved -replace '\s','') -cne ($baselineText -replace '\s','')){throw 'Unexpected edit outside the Quest entry input gate.'}
 Write-Host 'Native Ped/ragdoll source preservation: PASS after removing only the new input gate.'
}else{
 # Public kits have no prior checkout. Retain an explicit baseline predicate
 # only for the old-behaviour demonstration; all fixed assertions use source.
 $old=$cancel -replace '(?ms)^#if defined\(__ANDROID__\) && defined\(GTA_VR_WEAPONS\)\r?\n\s*if \(vrCancelEntryMovement\) \{\r?\n#else\r?\n(.*?)^#endif','$1'
 $production+='void CPed::ProcessBaselineCancellation() {'+"`n"+$old+"`n}`n"
}
foreach($name in @('PedAnimAlignCB','PedAnimDoorOpenCB','PedAnimGetInCB','PedAnimDoorCloseCB')){
 $method=[regex]::Match($ai,'(?ms)^void\r?\nCPed::'+$name+'\([^\n]*\)\r?\n\{.*?^\}').Value
 if(-not $method){throw ('Missing native callback '+$name)}
 $production+=$method+"`n"
}
$enumNames=[regex]::Matches($production,'\b(?:ANIM_\w+|ASSOCGRP_\w+|PEDTYPE_\w+|PED_\w+|OBJECTIVE_\w+|VEHICLE_APPEARANCE_\w+|DOOR_STATUS_\w+|CARLOCK_\w+|CAR_DOOR_\w+|CAR_WINDSCREEN|STATUS_\w+|MISSION_\w+|SOUND_\w+)\b') | ForEach-Object {$_.Value}
$enumNames=@($enumNames)+@('PEDTYPE_PLAYER1','PED_IDLE','PED_ENTER_CAR','CARLOCK_LOCKED','STATUS_WRECKED','ANIM_STD_CAR_ALIGN_DOOR_LHS') | Sort-Object -Unique
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vr-vehicle-entry-enums.inc'),('enum { '+($enumNames -join ',')+' };'),[Text.UTF8Encoding]::new($false))
$header=[IO.Path]::GetFullPath((Join-Path $src 'peds/VrVehicleEntry.h')).Replace('\','/')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vr-vehicle-entry-production.inc'),$production,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vr-vehicle-entry-helper.inc'),('#include "'+$header+'"'),[Text.UTF8Encoding]::new($false))
Write-Host ('Production Ped.cpp SHA256: '+(Get-FileHash -LiteralPath (Join-Path $src 'peds/Ped.cpp')).Hash)
Write-Host ('Production callbacks SHA256: '+(Get-FileHash -LiteralPath (Join-Path $src 'peds/PedAI.cpp')).Hash)
Write-Host ('Production input latch SHA256: '+(Get-FileHash -LiteralPath $header).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vr-vehicle-entry.cpp')
if($LASTEXITCODE -ne 0){throw 'Quest vehicle entry test failed.'}
