param(
 [string]$StageRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('vc-player-gates-'+[Guid]::NewGuid().ToString('N'))),
 [switch]$DevTools
)
$ErrorActionPreference='Stop'
$path=Join-Path $StageRoot 'src/skel/android/android.cpp'
if(-not(Test-Path -LiteralPath $path)){$path=Join-Path $StageRoot 'overlay/src/skel/android/android.cpp'}
$source=[IO.File]::ReadAllText($path)
$source=$source.Replace("`r`n","`n")
$guard='#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS'
$start=$source.IndexOf($guard+"`nstatic uint32 gQuestQuickStartSkipFrames;")
$end=$source.IndexOf("`n#endif",$start)+7
if($start -lt 0 -or $end -le $start){throw 'Missing guarded actual quick-start helpers.'}
$helpers=$source.Substring($start,$end-$start)
$front=$source.IndexOf('if(gGameState == GS_INIT_FRONTEND){')
if($front -lt 0){throw 'Missing actual frontend entry branch.'}
$brace=$source.IndexOf('{',$front);$end=$brace+1;$depth=1
while($depth -gt 0){if($source[$end] -eq '{'){$depth++}elseif($source[$end] -eq '}'){$depth--};$end++}
$frontend=$source.Substring($front,$end-$front)
$begin=$source.IndexOf('case GS_PLAYING_GAME:')
$end=$source.IndexOf('default:',$begin)
if($begin -lt 0 -or $end -lt 0){throw 'Missing actual game idle branch.'}
$step=$source.Substring($begin,$end-$begin)
$output=('#define MIAMIVR_DEV_TOOLS '+[int]$DevTools.IsPresent)+"`n"+$helpers+"`nbool RunFrontend(){`n"+$frontend+"`nreturn gGameState == GS_FRONTEND;`n}`nvoid RunStep(){switch(gGameState){`n"+$step+"default:break;}}`n"
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'player-debug-gates-production.inc'),$output,[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-ragdoll.ps1') -KitRoot $StageRoot -OutDir $OutDir -Sources @('test-player-debug-gates.cpp')
if($LASTEXITCODE -ne 0){throw 'Player debug gate checks failed.'}
$exe=Join-Path $OutDir 'test-player-debug-gates.exe'
$binary=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe))
if(-not $DevTools -and ($binary.Contains('QuickTestStart') -or $binary.Contains('quick test start:'))){throw 'Player executable contains an actual quick-start INI key or log string.'}
Write-Host ('Android source SHA256: '+(Get-FileHash -LiteralPath $path).Hash)
