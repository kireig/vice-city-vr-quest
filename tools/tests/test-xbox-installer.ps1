param(
 [string]$KitRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [Parameter(Mandatory=$true)][string]$ProfileRoot,
 [Parameter(Mandatory=$true)][string]$OutDir
)
$ErrorActionPreference="Stop"
if(Test-Path -LiteralPath $OutDir){throw 'Use a fresh output directory.'}
New-Item -ItemType Directory -Path $OutDir | Out-Null
$OutDir=(Resolve-Path -LiteralPath $OutDir).Path
$ProfileRoot=(Resolve-Path -LiteralPath $ProfileRoot).Path
$source=[IO.File]::ReadAllText((Join-Path $KitRoot 'tools/install-modern-models.ps1')).Replace("`r`n","`n")
$helper=(Join-Path $KitRoot 'tools/modelsets/xbox-modelset.ps1').Replace("'","''")
$source=$source.Replace('(Join-Path $PSScriptRoot "modelsets/xbox-modelset.ps1")',"'$helper'")
$marker='try {'+"`n"+'    Write-Host "Vice City VR - $Profile model installer'
$at=$source.IndexOf($marker)
if($at -lt 0){throw 'Production installer main block missing.'}
$mock=@'
function Find-Adb { return 'fixture-adb' }
function Invoke-NativeCapture {
 param([string]$FilePath,[string[]]$Arguments)
 if($FilePath -ne 'fixture-adb'){throw 'A real native command was attempted.'}
 [IO.File]::AppendAllText($env:XBOX_TEST_CALLS,(([pscustomobject]@{args=@($Arguments)} | ConvertTo-Json -Compress)+"`n"))
 $a=@($Arguments)
 if($a.Count -ge 2 -and $a[0] -eq '-s'){$a=@($a | Select-Object -Skip 2)}
 $code=0;$lines=@('ok')
 if($a[0] -eq 'devices'){$lines=@('List of devices attached','fixture-quest device product:test')}
 elseif($a[0] -eq 'shell' -and $a[1] -eq 'pm'){$lines=@('package:/data/app/fixture.apk')}
 elseif($a[0] -eq 'shell' -and ($a[1] -eq 'stat' -or $a[1] -eq 'sha256sum')){
   $remote=$a[-1]
   if($remote -notmatch '/\.xbox-incoming-[^/]+/(.+)$'){throw "Unexpected verification path: $remote"}
   $local=Join-Path $env:XBOX_TEST_PROFILE $Matches[1]
   if($a[1] -eq 'stat'){$lines=@([string](Get-Item -LiteralPath $local).Length)}
   else {
     $hash=(Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash
     if($env:XBOX_TEST_FAIL -eq 'hash'){$hash='0'*64}
     $lines=@("$hash  $remote")
   }
 }
 elseif($a[0] -eq 'shell' -and $a[1] -eq 'chmod' -and $env:XBOX_TEST_FAIL -eq 'permissions'){$code=1;$lines=@('fixture permission failure')}
 elseif($a[0] -eq 'shell' -and $a[1] -eq 'mv' -and $a[2] -match '/\.xbox-incoming-' -and $env:XBOX_TEST_FAIL -eq 'commit'){$code=1;$lines=@('fixture rename failure')}
 return [pscustomobject]@{ExitCode=$code;Lines=$lines}
}
'@
$harness=$source.Substring(0,$at)+$mock+"`n"+$source.Substring($at)
$script=Join-Path $OutDir 'installer-production.ps1'
[IO.File]::WriteAllText($script,$harness,[Text.UTF8Encoding]::new($false))
$checks=0
function Check([bool]$Result,[string]$What){$script:checks++;if(-not $Result){throw $What}}
$saved=@{}
foreach($name in @('XBOX_TEST_PROFILE','XBOX_TEST_FAIL','XBOX_TEST_CALLS')){$saved[$name]=[Environment]::GetEnvironmentVariable($name)}
try {
 foreach($failure in @('none','hash','commit','permissions')) {
  $env:XBOX_TEST_PROFILE=$ProfileRoot;$env:XBOX_TEST_FAIL=$failure;$env:XBOX_TEST_CALLS=Join-Path $OutDir "$failure-calls.jsonl"
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -ModernDir $ProfileRoot -Profile Xbox -NonInteractive -LogPath (Join-Path $OutDir "$failure-transcript.log") *> (Join-Path $OutDir "$failure-output.log")
  $result=$LASTEXITCODE
  Check (($result -eq 0) -eq ($failure -eq 'none')) "Wrong installer result for $failure"
  $calls=@([IO.File]::ReadAllLines($env:XBOX_TEST_CALLS) | ForEach-Object {($_ | ConvertFrom-Json).args -join ' '})
  Check (@($calls | Where-Object {$_ -match 'uninstall|shell am start|vr_settings|gta_vc\.set|/modelsets/modern'}).Count -eq 0) 'Xbox path touched another profile, settings, uninstall or launch'
  Check (@($calls | Where-Object {$_ -match ' push '}).Count -eq 7) 'Not exactly seven profile files transferred'
  Check (@($calls | Where-Object {$_ -match 'shell sha256sum'}).Count -eq $(if($failure -eq 'hash'){1}elseif($failure -eq 'permissions'){0}else{7})) 'Unexpected profile hash checks'
  $grant=@($calls | Where-Object {$_ -match 'shell chmod -R a\+rX .*/\.xbox-incoming-[^/ ]+$'})
  Check ($grant.Count -eq 1) 'Missing application read/traverse access for staged profile'
  $moves=@($calls | Where-Object {$_ -match 'shell mv'})
  if($moves.Count) { Check ([array]::IndexOf($calls,$grant[0]) -lt [array]::IndexOf($calls,$moves[0])) 'Permissions granted after activation' }
  if($failure -eq 'hash' -or $failure -eq 'permissions') { Check ($moves.Count -eq 0) 'Hash mismatch moved active profile' }
  elseif($failure -eq 'commit') {
   Check ($moves.Count -eq 3) 'Commit failure did not restore old profile'
   Check ($moves[-1] -match '/\.xbox-backup-[^ ]+ .*/xbox$') 'Wrong rollback destination'
  } else {
   Check ($moves.Count -eq 2) 'Atomic replacement did not stage/backup old profile'
   Check ($moves[1] -match '/\.xbox-incoming-[^ ]+ .*/xbox$') 'Wrong committed profile'
  }
 }
} finally {foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name])}}
@{checks=$checks;deviceCommands=0;production=(Get-FileHash -LiteralPath (Join-Path $KitRoot 'tools/install-modern-models.ps1') -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'results.json')
Write-Host "PASS $checks production Xbox installer routing/verification/rollback checks; mocked ADB only."
