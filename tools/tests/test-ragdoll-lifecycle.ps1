param(
    [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-lifecycle-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $StageRoot 'src\peds\VrRagdoll.cpp'
$source = [IO.File]::ReadAllText($sourcePath)
$functions = [Collections.Generic.List[string]]::new()
foreach ($name in @('FindSlot','Owns','SetBulletTrace','SkipBulletCollision','AcquireSlot','Thaw','Release')) {
    # Select each lifecycle primitive explicitly. Bounds/render helpers may
    # live between them; they have their own fixtures and engine dependencies.
    $signature = [regex]::Match($source, '(?m)^(?:static\s+)?(?:Slot\s*\*|bool|void)\s*' + [regex]::Escape($name) + '\([^)]*\)\s*\{')
    if (-not $signature.Success) { throw "Cannot find production lifecycle function: $name" }
    $bodyStart = $signature.Index + $signature.Length - 1
    $depth = 1
    $end = $bodyStart + 1
    while ($end -lt $source.Length -and $depth -gt 0) {
        if ($source[$end] -eq '{') { $depth++ }
        elseif ($source[$end] -eq '}') { $depth-- }
        $end++
    }
    if ($depth -ne 0) { throw "Unterminated production lifecycle function: $name" }
    $functions.Add($source.Substring($signature.Index, $end-$signature.Index))
}
if (-not [regex]::IsMatch($source, 'MAX_POSES\s*=\s*NUMPEDS')) { throw 'Persistent body capacity must match the native ped pool.' }
if ([regex]::IsMatch($source, '\bSleepingSlot\s*\(')) { throw 'Admission must not depend on finding a sleeping simulation owner.' }
$generated = Join-Path $PSScriptRoot 'ragdoll-lifecycle-production.inc'
[IO.File]::WriteAllText($generated, ($functions -join "`r`n"), [Text.UTF8Encoding]::new($false))
Write-Host ('VrRagdoll.cpp SHA256: ' + (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-lifecycle.cpp')
