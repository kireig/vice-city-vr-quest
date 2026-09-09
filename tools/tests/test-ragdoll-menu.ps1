param(
    [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$MenuSourcePath = '',
    [switch]$DevTools,
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-ragdoll-menu-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $StageRoot 'src\skel\android\vrdebug.cpp'
if (-not (Test-Path -LiteralPath $sourcePath)) {
    $sourcePath = Join-Path $StageRoot 'overlay\src\skel\android\vrdebug.cpp'
}
$headerPath = Join-Path (Split-Path -Parent $sourcePath) 'VrCheatMenu.h'
if (-not [string]::IsNullOrWhiteSpace($MenuSourcePath)) { $sourcePath = $MenuSourcePath }
$source = [IO.File]::ReadAllText($sourcePath)
$pieces = [Collections.Generic.List[string]]::new()
$pieces.Add('#define MIAMIVR_DEV_TOOLS '+[int][bool]$DevTools)
$pieces.Add('#include "' + ($headerPath -replace '\\','/') + '"')
foreach ($pattern in @(
    '(?ms)^enum \{\r?\n\s*VR_DEBUG_WIDTH.*?^\};',
    '(?ms)^enum \{\r?\n\s*VR_MENU_PAGE_SETTINGS.*?^\};',
    '(?ms)^struct DebugGlyph.*?^\};'
)) {
    $match = [regex]::Match($source, $pattern)
    if (-not $match.Success) { throw "Cannot extract menu declaration: $pattern" }
    $pieces.Add($match.Value)
}
foreach ($match in [regex]::Matches($source, '(?ms)^enum eVr\w*MenuItem \{.*?^\};')) {
    $pieces.Add($match.Value)
}
$functions = [Collections.Generic.List[string]]::new()
foreach ($name in @(
    'LoadRagdollSettings', 'MenuRepeatPulse', 'ResetMenuNavigationRepeat',
    'OpenRagdollMenu', 'ReturnFromRagdollMenu', 'AdjustRagdollMenuValue',
    'ReturnFromCheatMenu', 'ReturnFromDeformationMenu',
    'CurrentMenuSelection', 'CurrentMenuItemCount', 'CurrentMenuValueRepeats',
    'ReturnFromCurrentMenuPage', 'QuestMenuPageColour', 'DrawQuestRagdollPage'
)) {
    $match = [regex]::Match($source, '(?ms)^static [^\r\n]+\r?\n' + $name + '\(.*?^\}')
    if (-not $match.Success) { throw "Cannot extract production menu function: $name" }
    $functions.Add($match.Value)
}
$functionText = $functions -join "`r`n`r`n"
$variables = @([regex]::Matches($functionText, '\bg(?:Vr|Ragdolls)\w*') | ForEach-Object { $_.Value } | Sort-Object -Unique)
foreach ($variable in $variables) {
    $match = [regex]::Match($source, '(?m)^static (?:int|bool|double) ' + $variable + '(?:\s*=[^;]+)?;')
    if (-not $match.Success) { throw "Cannot extract production menu variable: $variable" }
    $pieces.Add($match.Value)
}
$pieces.Add($functionText)
$generated = Join-Path $PSScriptRoot 'ragdoll-menu-production.inc'
[IO.File]::WriteAllText($generated, ($pieces -join "`r`n`r`n") + "`r`n", [Text.UTF8Encoding]::new($false))
Write-Host ('vrdebug.cpp SHA256: ' + (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-ragdoll-menu.cpp')
