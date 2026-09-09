param(
    [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-melee-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$path = Join-Path $StageRoot 'src\vr\QuestWeaponVR.cpp'
if (-not (Test-Path -LiteralPath $path)) { $path = Join-Path $StageRoot 'overlay\src\vr\QuestWeaponVR.cpp' }
$source = [IO.File]::ReadAllText($path)
$parts = @(
    '(?ms)^struct MeleeStrike\r?\n.*?(?=^struct ManualReloadState)',
    '(?ms)^static bool\r?\nIsPhysicalMelee\(.*?(?=^static bool\r?\nIsPhysicalThrowable)',
    '(?ms)^static bool\r?\nBuildTrackedWeaponHandBasis\(.*?(?=^static bool\r?\nBuildCanonicalSupportFrame)',
    '(?ms)^static CVector\r?\nMeleeModelTip\(.*?(?=^static bool\r?\nBuildAim)',
    '(?ms)^bool ConsumePhysicalMeleeStrike\(.*?(?=^int\r?\nGetQuestWeaponSettingCount)'
)
$definitions = @()
foreach ($pattern in $parts) {
    $match = [regex]::Match($source, $pattern)
    if (-not $match.Success) { throw "Cannot extract melee production code: $pattern" }
    $definitions += $match.Value
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'quest-melee-types.inc'), $definitions[0], [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'quest-melee-production.inc'), ($definitions[1..4] -join "`n"), [Text.UTF8Encoding]::new($false))
$weapons = Join-Path $StageRoot 'src\weapons\WeaponType.h'
if (-not (Test-Path -LiteralPath $weapons)) { throw 'Use an assembled source tree for this production test.' }
Copy-Item -LiteralPath $weapons -Destination (Join-Path $PSScriptRoot 'quest-melee-weapon-types.inc')
Write-Host ('QuestWeaponVR.cpp SHA256: ' + (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-quest-melee.cpp')
