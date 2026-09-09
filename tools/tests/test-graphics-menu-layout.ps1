param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$MenuSourcePath='',
 [switch]$DevTools,
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-graphics-layout-'+[Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($MenuSourcePath)){
 $MenuSourcePath=Join-Path $StageRoot 'src/skel/android/vrdebug.cpp'
 if(-not(Test-Path -LiteralPath $MenuSourcePath)){$MenuSourcePath=Join-Path $StageRoot 'overlay/src/skel/android/vrdebug.cpp'}
}
$source=[IO.File]::ReadAllText($MenuSourcePath)
function Definition([string]$text,[string]$name){
 $match=[regex]::Match($text,'(?ms)^static [^\r\n]+\r?\n'+$name+'\(.*?^\}')
 if(-not $match.Success){throw "Missing production function: $name"}
 return $match.Value
}
$pieces=[Collections.Generic.List[string]]::new()
$pieces.Add('#define MIAMIVR_DEV_TOOLS '+[int][bool]$DevTools)
$enum=[regex]::Match($source,'(?ms)^enum eVrGraphicsMenuItem \{.*?^\};')
if(-not $enum.Success){throw 'Graphics item enum missing'}
$pieces.Add($enum.Value)
$pieces.Add((Definition $source 'QuestCpuPerformanceModeName'))
$pieces.Add((Definition $source 'BeginFullVrMenuPage'))
$pieces.Add((Definition $source 'DrawFullVrMenuRow').Replace('DrawFullVrMenuRow(','DrawFullVrMenuRowNative('))
$page=Definition $source 'DrawQuestGraphicsPage'
$pieces.Add($page)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'graphics-menu-layout-production.inc'),($pieces -join "`n"),[Text.UTF8Encoding]::new($false))
Write-Host ('Menu SHA256: '+(Get-FileHash -LiteralPath $MenuSourcePath).Hash)
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-graphics-menu-layout.cpp')
if($LASTEXITCODE -ne 0){throw 'Graphics layout verification failed'}
