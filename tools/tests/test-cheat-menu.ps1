param(
    [string]$StageRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$MenuSourcePath = '',
    [string]$HeaderSourcePath = '',
    [string]$PadSourcePath = '',
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vc-cheat-menu-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Join-Path $StageRoot 'src'
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'skel\android\vrdebug.cpp'))) {
    $sourceRoot = Join-Path $StageRoot 'overlay\src'
}
$sourcePath = Join-Path $sourceRoot 'skel\android\vrdebug.cpp'
if (-not [string]::IsNullOrWhiteSpace($MenuSourcePath)) { $sourcePath = $MenuSourcePath }
$headerPath = Join-Path $sourceRoot 'skel\android\VrCheatMenu.h'
if (-not [string]::IsNullOrWhiteSpace($HeaderSourcePath)) { $headerPath = $HeaderSourcePath }
$source = [IO.File]::ReadAllText($sourcePath)
if ([string]::IsNullOrWhiteSpace($PadSourcePath)) { $PadSourcePath = Join-Path $sourceRoot 'core\Pad.cpp' }
if (-not (Test-Path -LiteralPath $PadSourcePath)) {
    throw 'Pass -PadSourcePath with the assembled src/core/Pad.cpp when testing an overlay-only kit.'
}
$pad = [IO.File]::ReadAllText($PadSourcePath)
$header = [IO.File]::ReadAllText($headerPath)

# Compare mapping labels to the actual renderer-independent backend order.
# This catches an inserted or reordered native cheat before its action can
# silently become attached to an unrelated submenu row.
$specialMatch = [regex]::Match($pad, '(?s)enum eVrSpecialCheatIndex\s*\{(.*?)\};')
$special = @([regex]::Matches($specialMatch.Groups[1].Value, 'VR_SPECIAL_(\w+)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -ne 'CHEAT_COUNT' })
$legacyMatch = [regex]::Match($pad, '(?s)static const char \*gVrCheatNames\[\]\s*=\s*\{(.*?)\};')
$legacy = @([regex]::Matches($legacyMatch.Groups[1].Value, '"([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
$mappingMatch = [regex]::Match($header, '(?s)kSourceCategories\[\]\s*=\s*\{(.*?)\};')
$labels = @([regex]::Matches($mappingMatch.Groups[1].Value, '// ([^\r\n]+)') | ForEach-Object { $_.Groups[1].Value.Trim() })
if ($special.Count -ne 10 -or $legacy.Count -ne 59 -or $labels.Count -ne $special.Count + $legacy.Count) {
    throw 'The native cheat catalog changed; review the category mapping and its coverage.'
}
if ($pad -match 'VR_SPECIAL_PASS_CURRENT_MISSION|PassCurrentVrMission|VrMissionPassEntry|gVrMissionPassEntries|PASS CURRENT MISSION' -or
    $header -match 'PASS CURRENT MISSION') {
    throw 'The removed mission-pass cheat must leave no name, dispatch, table or local handler.'
}
for ($i = 0; $i -lt $special.Count; ++$i) {
    $label = ($labels[$i] -replace '6','SIX') -replace '[ -]','_'
    if ($label -ne $special[$i]) { throw "Special cheat mapping drift at $i : $label / $($special[$i])" }
}
for ($i = 0; $i -lt $legacy.Count; ++$i) {
    if ($labels[$i + $special.Count] -ne $legacy[$i]) { throw "Legacy cheat mapping drift at $i" }
}

$pieces = [Collections.Generic.List[string]]::new()
$pieces.Add('#include "' + ($headerPath -replace '\\','/') + '"')
$pieces.Add('static const char *nativeNames[] = {' + (($labels | ForEach-Object { '"' + $_ + '"' }) -join ',') + '};')
$pieces.Add($specialMatch.Value)
$pieces.Add($legacyMatch.Value)
$countMatch = [regex]::Match($pad, '(?ms)^int GetVrCheatCount\(void\).*?^\}')
if (-not $countMatch.Success) { throw 'Cannot extract actual backend cheat count.' }
$pieces.Add($countMatch.Value.Replace('GetVrCheatCount', 'ActualBackendCheatCount'))
foreach ($pattern in @(
    '(?ms)^enum \{\r?\n\s*VR_DEBUG_WIDTH.*?^\};',
    '(?ms)^enum \{\r?\n\s*VR_MENU_PAGE_SETTINGS.*?^\};'
)) {
    $match = [regex]::Match($source, $pattern)
    if (-not $match.Success) { throw "Cannot extract declaration: $pattern" }
    $pieces.Add($match.Value)
}
foreach ($match in [regex]::Matches($source, '(?ms)^enum eVr\w*MenuItem \{.*?^\};')) { $pieces.Add($match.Value) }
$functions = [Collections.Generic.List[string]]::new()
foreach ($name in @(
    'MenuRepeatPulse', 'MenuNavigationPulse', 'ResetMenuNavigationRepeat',
    'OpenCheatMenu', 'ReturnFromCheatMenu', 'CycleQuestCheatMenuSelection',
    'ActivateQuestCheatMenuSelection', 'CurrentMenuSelection',
    'CurrentMenuItemCount', 'CurrentMenuValueRepeats', 'ReturnFromRagdollMenu', 'ReturnFromDeformationMenu',
    'ReturnFromCurrentMenuPage', 'DrawQuestCheatPage'
)) {
    $match = [regex]::Match($source, '(?ms)^static [^\r\n]+\r?\n' + $name + '\(.*?^\}')
    if (-not $match.Success) { throw "Cannot extract production function: $name" }
    $functions.Add($match.Value)
}
$functionText = $functions -join "`r`n`r`n"
$variables = @([regex]::Matches($functionText, '\bgVr\w*') | ForEach-Object { $_.Value } | Sort-Object -Unique)
foreach ($variable in $variables) {
    $match = [regex]::Match($source, '(?m)^static (?:int|bool|double|char) ' + $variable + '(?:\[\d+\])?(?:\s*=[^;]+)?;')
    if (-not $match.Success) { throw "Cannot extract variable: $variable" }
    $pieces.Add($match.Value)
}
$pieces.Add($functionText)
$generated = Join-Path $PSScriptRoot 'cheat-menu-production.inc'
[IO.File]::WriteAllText($generated, ($pieces -join "`r`n`r`n") + "`r`n", [Text.UTF8Encoding]::new($false))
Write-Host "Verified all $($labels.Count) native cheat mappings against Pad.cpp."
Write-Host ('vrdebug.cpp SHA256: ' + (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash)
Write-Host ('VrCheatMenu.h SHA256: ' + (Get-FileHash -LiteralPath $headerPath -Algorithm SHA256).Hash)
Write-Host ('Pad.cpp SHA256: ' + (Get-FileHash -LiteralPath $PadSourcePath -Algorithm SHA256).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-cheat-menu.cpp')
