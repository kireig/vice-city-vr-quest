param(
    [string]$StageRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-ray-resolver-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $StageRoot 'src\weapons\Weapon.cpp'
$source = [IO.File]::ReadAllText($sourcePath)
$match = [regex]::Match($source, '(?ms)^bool\r?\nCWeapon::ProcessLineOfSight\(.*?(?=^void\r?\nCWeapon::CheckForShootingVehicleOccupant\()')
if (-not $match.Success) { throw 'Cannot extract the production weapon ray resolver.' }
$generated = Join-Path $PSScriptRoot 'ragdoll-ray-resolver-production.inc'
[IO.File]::WriteAllText($generated, $match.Value, [Text.UTF8Encoding]::new($false))
Write-Host ('Weapon.cpp SHA256: ' + (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-ray-resolver.cpp')
