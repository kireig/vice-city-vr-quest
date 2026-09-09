param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-ragdoll-off-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$sourcePath=Join-Path $StageRoot 'src/peds/VrRagdoll.cpp'
if(-not(Test-Path -LiteralPath $sourcePath)){$sourcePath=Join-Path $StageRoot 'overlay/src/peds/VrRagdoll.cpp'}
$source=[IO.File]::ReadAllText($sourcePath)
$output='#define MIAMIVR_DEV_TOOLS 0'+"`n"
foreach($name in @('SetEnabled','FindSlot','Owns','ForgetAnimatedPose','Release')){
 $match=[regex]::Match($source,'(?m)^(?:static\s+)?(?:Slot\s*\*|bool|void)\s*'+$name+'\([^)]*\)\s*\{')
 if(-not $match.Success){throw ('Missing method '+$name)}
 $end=$match.Index+$match.Length;$depth=1
 while($depth -gt 0 -and $end -lt $source.Length){if($source[$end] -eq '{'){$depth++}elseif($source[$end] -eq '}'){$depth--};$end++}
 $output+=$source.Substring($match.Index,$end-$match.Index)+"`n"
}
$prologue=[regex]::Match($source,'(?ms)^void\r?\nUpdate\(void\)\r?\n\{.*?(?=\tfloat dt = CTimer::GetTimeStepInSeconds\(\);)').Value
if(-not $prologue){throw 'Missing actual update pre-solver gate.'}
$output+=$prologue+"`n ++enteredDynamicUpdate;`n}`n"
$bullet=[regex]::Match($source,'(?ms)^CPed \*\r?\nBulletHit\(.*?(?=\tVrRagdollMetrics::Scope bulletTime)').Value
if(-not $bullet){throw 'Missing actual bullet entry gate.'}
$output+=$bullet+"`n (void)source; (void)end; (void)weaponType; (void)point; (void)shooter; ++bulletEntry; return nil;`n}`n"
$vehicle=[regex]::Match($source,'(?ms)^void VehicleImpact\(.*?(?=\tif\(slot && !slot->vehicleSeeded\))').Value
if(-not $vehicle){throw 'Missing actual vehicle entry gate.'}
$output+=$vehicle+"`n (void)slot; (void)car; (void)incomingGameSpeed; (void)collisionImpulse; (void)contactPoint; (void)incomingGameTurn; ++vehicleEntry;`n}`n"
$wake=[regex]::Match($source,'(?m)^\t(?:if\(enabled\) )?WakeSavedByVehicles\(\);').Value
if(-not $wake){throw 'Missing actual vehicle wake gate.'}
$output+="void RunWakeGate(){`n"+$wake+"`n}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'ragdoll-off-cost-production.inc'),$output,[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-off-cost.cpp')
if($LASTEXITCODE -ne 0){throw 'OFF gate test failed.'}
Write-Host ('Production VrRagdoll SHA256: '+(Get-FileHash -LiteralPath $sourcePath).Hash)
