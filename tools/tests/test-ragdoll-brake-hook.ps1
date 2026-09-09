param(
    [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-brake-hook-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $StageRoot 'src\peds\VrRagdoll.cpp'
if (-not (Test-Path -LiteralPath $sourcePath)) {
    $sourcePath = Join-Path $StageRoot 'overlay\src\peds\VrRagdoll.cpp'
}
$source = [IO.File]::ReadAllText($sourcePath)
$hookPath = Join-Path $StageRoot 'src\peds\PedFight.cpp'
if (-not (Test-Path -LiteralPath $hookPath)) {
    throw 'The brake hook fixture requires -StageRoot pointing to assembled source containing src\peds\PedFight.cpp.'
}
$hook = [IO.File]::ReadAllText($hookPath)
if (-not [regex]::IsMatch($hook, 'VrRagdoll::VehicleImpact\(this,\s*car,\s*incomingRagdollSpeed,\s*impulse,\s*contactPoint,\s*preCollisionTurn\);')) {
    throw 'KillPedWithCar does not pass its vehicle and confirmed collision impulse to the production callback.'
}
$callback = [regex]::Match($source, '(?ms)^void\s+VehicleImpact\(CPed \*ped,\s*CVehicle \*car,.*?^\}')
if (-not $callback.Success) { throw 'Cannot extract production vehicle brake callback.' }
$getter = [regex]::Match($source, '(?m)^int\s+GetBrakePercent\(void\)\s*\{[^\r\n]*\}')
if (-not $getter.Success) { throw 'Cannot extract production braking setting getter.' }
$generated = Join-Path $PSScriptRoot 'ragdoll-brake-hook-production.inc'
[IO.File]::WriteAllText($generated, $getter.Value + "`r`n" + $callback.Value + "`r`n", [Text.UTF8Encoding]::new($false))
Write-Host ('VrRagdoll.cpp SHA256: ' + (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash)
Write-Host ('PedFight.cpp hook SHA256: ' + (Get-FileHash -LiteralPath $hookPath -Algorithm SHA256).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-brake-hook.cpp')
