param(
 [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-cop-aim-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$parts=[Collections.Generic.List[string]]::new()
function Extract-Production([string]$relative,[string]$pattern) {
 $path=Join-Path $StageRoot $relative
 $text=[IO.File]::ReadAllText($path)
 $match=[regex]::Match($text,$pattern)
 if(-not $match.Success){throw "Cannot extract production method from $relative"}
 Write-Host ($relative + ' SHA256: ' + (Get-FileHash -LiteralPath $path).Hash)
 return $match.Value
}
$held=Extract-Production 'src\vr\QuestWeaponVR.cpp' '(?ms)^bool\s+GetHeldTrackedWeaponAim\(CVector \*source, CVector \*direction\)\s*\{.*?^\}'
$parts.Add('namespace OculusVR {' + "`r`n" + $held + "`r`n}")
$parts.Add((Extract-Production 'src\peds\Ped.cpp' '(?ms)^CPed \*\s+CPed::CheckForAimedGun\(void\)\s*\{.*?^\}'))
$parts.Add((Extract-Production 'src\peds\PedAI.cpp' '(?ms)^void\s+CPed::ReactToPointGun\(CEntity \*entWithGun\)\s*\{.*?^\}'))
$parts.Add((Extract-Production 'src\peds\CopPed.cpp' '(?ms)^void\s+CCopPed::ScanForCrimes\(void\)\s*\{.*?^\}'))
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vr-cop-aim-production.inc'),($parts -join "`r`n"),[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vr-cop-aim.cpp')
