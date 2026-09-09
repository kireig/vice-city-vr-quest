param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$MenuSourcePath='',
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-deformation-menu-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
if ([string]::IsNullOrWhiteSpace($MenuSourcePath)) {
 $MenuSourcePath=Join-Path $StageRoot 'src\skel\android\vrdebug.cpp'
 if (-not (Test-Path -LiteralPath $MenuSourcePath)) { $MenuSourcePath=Join-Path $StageRoot 'overlay\src\skel\android\vrdebug.cpp' }
}
$source=[IO.File]::ReadAllText($MenuSourcePath)
function Definition([string]$text,[string]$pattern) {
 $match=[regex]::Match($text,$pattern)
 if (-not $match.Success) { throw "Missing source definition: $pattern" }
 $end=$match.Index+$match.Length; $depth=1
 while ($end -lt $text.Length -and $depth -gt 0) {
  if ($text[$end] -eq '{') {$depth++} elseif ($text[$end] -eq '}') {$depth--}
  $end++
 }
 if ($depth -ne 0) {throw 'Unterminated source definition.'}
 return $text.Substring($match.Index,$end-$match.Index)
}
$pieces=[Collections.Generic.List[string]]::new()
foreach ($pattern in @('(?ms)^enum eVrTrafficMenuItem \{.*?^\};','(?ms)^enum eVrDeformationMenuItem \{.*?^\};','(?ms)^struct DebugGlyph.*?^\};')) {
 $match=[regex]::Match($source,$pattern)
 if (-not $match.Success) {throw "Missing source declaration: $pattern"}
 $pieces.Add($match.Value)
}
foreach ($name in @('SaveVrInteger','LoadVehicleDeformationSettings','MenuRepeatPulse','ResetMenuNavigationRepeat','OpenDeformationMenu','ReturnFromDeformationMenu','SaveDeformationTuning','AdjustDeformationMenuValue','DrawQuestDeformationPage')) {
 $pieces.Add((Definition $source ('(?m)^static (?:void|bool)\s+'+$name+'\([^)]*\)\s*\{')))
}
$loader=Definition $source '(?m)^static void\s+LoadVrSettings\(void\)\s*\{'
if (-not [regex]::IsMatch($loader,'(?s)if\(gVrSettingsLoaded\)\s*return;.*LoadVehicleDeformationSettings\(\);.*gVrSettingsLoaded = true;')) {
 throw 'Deformation setting must load through the existing once-only settings path.'
}
if ([regex]::Matches($source,'LoadVehicleDeformationSettings\(\);').Count -ne 1) {throw 'Settings loader must have exactly one call site.'}
$dispatch=Definition $source '(?s)else if\(gVrMenuPage == VR_MENU_PAGE_TRAFFIC &&\s*\(positivePulse \|\| decreasePulse\)\)\{'
$pieces.Add('static void DispatchTraffic(bool positivePulse,bool decreasePulse) {'+($dispatch -replace '^else if','if')+'}')
$dispatch=Definition $source '(?s)else if\(gVrMenuPage == VR_MENU_PAGE_DEFORMATION &&\s*\(positivePulse \|\| decreasePulse\)\)\{'
$pieces.Add('static void DispatchDeformation(bool positivePulse,bool decreasePulse) {'+($dispatch -replace '^else if','if')+'}')
$back=Definition $source '(?s)if\(gVrMenuPage == VR_MENU_PAGE_DEFORMATION\)\{'
$pieces.Add('static void DispatchBack() {'+$back+'}')
$repeat=[regex]::Match($source,'(?s)if\(gVrMenuPage == VR_MENU_PAGE_DEFORMATION\)\s*(return[^;]+;)')
if (-not $repeat.Success) {throw 'Missing deformation repeat predicate.'}
$pieces.Add('static bool DeformationValueRepeats() {'+$repeat.Groups[1].Value+'}')
foreach($mapping in @('case VR_MENU_PAGE_DEFORMATION: return &gVrDeformationSelection;','case VR_MENU_PAGE_DEFORMATION: return VR_DEFORMATION_ITEM_COUNT;')) {
 if(-not $source.Contains($mapping)){throw "Missing production routing: $mapping"}
}
if(-not [regex]::IsMatch($source,'case VR_MENU_PAGE_DEFORMATION:\s*DrawQuestDeformationPage\(\);\s*break;')){throw 'Missing deformation draw dispatch'}
$shortcut=[regex]::Match($source,'(?s)\tconst bool cheatShortcut = modifier && in\.b;.*?\tgVrCheatShortcutDown = cheatShortcut;')
if(-not $shortcut.Success){throw 'Missing actual cheat shortcut path'}
$pieces.Add('static void DispatchShortcut(const PadInput &in,bool modifier) {'+$shortcut.Value+'}')
$pieces.Add((Definition $source '(?m)^bool\s+VrMenuConsumesInput\(void\)\s*\{'))
$repeat=[regex]::Match($source,'(?s)if\(gVrMenuPage == VR_MENU_PAGE_TRAFFIC\)\s*(return[^;]+;)')
if (-not $repeat.Success) {throw 'Missing traffic repeat predicate.'}
$pieces.Add('static bool TrafficValueRepeats() {'+$repeat.Groups[1].Value+'}')
$draw=Definition $source '(?m)^static void\s+DrawQuestTrafficPage\(void\)\s*\{'
$rowsEnd=$draw.IndexOf("`tchar status[112];")
if ($rowsEnd -lt 0) {throw 'Missing traffic status boundary.'}
$pieces.Add($draw.Substring(0,$rowsEnd)+'}')
$status=[regex]::Match($draw,'DrawVrMenuText\(status, VR_MENU_WIDTH/2, (\d+), 2,')
if (-not $status.Success) {throw 'Missing traffic status position.'}
$pieces.Add('static const int trafficStatusY='+$status.Groups[1].Value+';')
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'vehicle-deformation-menu-production.inc'),($pieces -join "`r`n`r`n"),[Text.UTF8Encoding]::new($false))
Write-Host ('vrdebug.cpp SHA256: '+(Get-FileHash -LiteralPath $MenuSourcePath -Algorithm SHA256).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-vehicle-deformation-menu.cpp')
