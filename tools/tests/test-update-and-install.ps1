param(
    [string]$Kit = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$OutDir = (Join-Path $env:TEMP ("vcvr-update-test-" + [Guid]::NewGuid().ToString("N")))
)
$ErrorActionPreference = "Stop"
if (Test-Path -LiteralPath $OutDir) { throw "Use a new test output directory." }
New-Item -ItemType Directory -Path $OutDir | Out-Null
$OutDir = [IO.Path]::GetFullPath($OutDir)
$script:checks = 0
function Check([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw "Check failed: $Message" }
}
function Reject([scriptblock]$Action, [string]$Pattern) {
    $message = ""
    try { & $Action | Out-Null } catch { $message = $_.Exception.Message }
    Check ($message -match $Pattern) "expected rejection '$Pattern', got '$message'"
}
function Write-File([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Force -Path (Split-Path $Path -Parent) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Read-Ast([string]$Path) {
    $tokens = $null; $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$parseErrors)
    Check ($parseErrors.Count -eq 0) "PowerShell syntax: $Path"
    return $ast
}
$updater = Join-Path $Kit "tools/update-and-install.ps1"
$builder = Join-Path $Kit "tools/build-and-install.ps1"
$updateAst = Read-Ast $updater
$builderAst = Read-Ast $builder
foreach ($definition in $updateAst.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
    . ([scriptblock]::Create($definition.Extent.Text))
}
$script:UpdateGit = (Get-Command git.exe).Source
$script:NativeUpdateGit = ${function:Invoke-UpdateGit}
$script:GitCalls = [Collections.Generic.List[string]]::new()
$official = "https://github.com/dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git"
$seed = Join-Path $OutDir "author"
$script:TestRemote = Join-Path $OutDir "remote.git"
New-Item -ItemType Directory -Path $seed | Out-Null
function Local-Git([string]$Directory, [string[]]$Arguments) {
    return & $script:NativeUpdateGit $Directory $Arguments
}
function Invoke-UpdateGit {
    param([string]$Directory, [string[]]$Arguments)
    $script:GitCalls.Add(($Arguments -join "|"))
    if ($Arguments[0] -eq "fetch") {
        Check ($Arguments[1] -eq "origin" -and $Arguments[2] -eq "master") "fetch targets master"
        return Local-Git $Directory @("fetch", $script:TestRemote, "master")
    }
    if ($Arguments[0] -eq "clone") {
        Check ($Arguments[4] -eq $official) "clone targets official source"
        $copy = @($Arguments); $copy[4] = $script:TestRemote
        $result = Local-Git $Directory $copy
        Local-Git $Arguments[5] @("remote", "set-url", "origin", $official) | Out-Null
        return $result
    }
    return Local-Git $Directory $Arguments
}
function Set-Version([string]$Directory, [int]$Version) {
    Write-File (Join-Path $Directory "overlay/android/app/build.gradle.kts") "android {`n defaultConfig {`n versionCode = $Version`n }`n}`n"
}
function Commit-Fixture([string]$Directory, [string]$Message) {
    Local-Git $Directory @("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "add", ".") | Out-Null
    Local-Git $Directory @("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", $Message) | Out-Null
}
Local-Git $seed @("init", "-b", "master") | Out-Null
Set-Version $seed 520
Write-File (Join-Path $seed ".gitignore") "private/`n*.keystore`nrelease-signing.properties`n"
Write-File (Join-Path $seed "source.txt") "original"
Write-File (Join-Path $seed "tools/build-and-install.ps1") @'
param([string]$WorkDir,[switch]$BuildOnly,[switch]$NonInteractive,[switch]$UpdateOnly,[string]$Serial,[switch]$Release)
if (-not $UpdateOnly) { throw "Unsafe build mode" }
[IO.File]::WriteAllText((Join-Path $WorkDir 'build-invocation.txt'), "$BuildOnly|$NonInteractive|$UpdateOnly|$Serial|$Release")
'@
Commit-Fixture $seed "base"
Local-Git $OutDir @("clone", "--bare", $seed, $script:TestRemote) | Out-Null
$clients = @{}
foreach ($name in @("normal", "dirty", "staged", "diverged", "ignored", "untracked", "branch", "remote", "newer", "marker", "dry", "ignored-collision", "hook")) {
    $directory = Join-Path $OutDir "client $name"
    Local-Git $OutDir @("clone", $script:TestRemote, $directory) | Out-Null
    Local-Git $directory @("remote", "set-url", "origin", $official) | Out-Null
    $clients[$name] = $directory
}
$originalHead = Local-Git $seed @("rev-parse", "HEAD")
Set-Version $seed 521
Write-File (Join-Path $seed "source.txt") "updated"
Commit-Fixture $seed "update"
Local-Git $seed @("push", $script:TestRemote, "master") | Out-Null
$newHead = Local-Git $seed @("rev-parse", "HEAD")
$cache = Join-Path $OutDir "cache space's"
New-Item -ItemType Directory -Path $cache | Out-Null
Check ((Get-UpdateVersion "versionCode = 520") -eq 520) "version parser"
Reject { Get-UpdateVersion "versionName = 520" } "Cannot verify"
Reject { Get-UpdateVersion "versionCode = 520`nversionCode = 521" } "Cannot verify"
Write-File (Join-Path $clients.dry "private/keep.keystore") "fixture-secret"
$script:GitCalls.Clear()
Update-SourceKit -LocalKit $clients.dry -CacheRoot $cache -ReadOnly | Out-Null
Check (-not ($script:GitCalls -match '^(fetch|clone|merge)\|')) "dry run no remote/mutation"
Check ((Local-Git $clients.dry @("rev-parse", "HEAD")) -eq $originalHead) "dry run HEAD"
Write-File (Join-Path $clients.dirty "source.txt") "local edits"
Write-File (Join-Path $clients.staged "source.txt") "staged edits"
Local-Git $clients.staged @("add", "source.txt") | Out-Null
foreach ($name in @("dirty", "staged")) {
    $script:GitCalls.Clear()
    Reject { Update-SourceKit $clients[$name] $cache } "Local source changes"
    Check (-not ($script:GitCalls -match '^fetch\|')) "$name refuses before fetch"
    Check ((Local-Git $clients[$name] @("rev-parse", "HEAD")) -eq $originalHead) "$name preserves HEAD"
}
Write-File (Join-Path $clients.diverged "local.txt") "local commit"
Commit-Fixture $clients.diverged "local"
$divergedHead = Local-Git $clients.diverged @("rev-parse", "HEAD")
Reject { Update-SourceKit $clients.diverged $cache } "diverge"
Check ((Local-Git $clients.diverged @("rev-parse", "HEAD")) -eq $divergedHead) "divergence preserves commits"
Local-Git $clients.branch @("checkout", "-b", "local-work") | Out-Null
Reject { Update-SourceKit $clients.branch $cache } "requires the master"
Local-Git $clients.remote @("remote", "set-url", "origin", "https://example.invalid/other.git") | Out-Null
Reject { Update-SourceKit $clients.remote $cache } "not the official"
Set-Version $clients.newer 522
Commit-Fixture $clients.newer "newer local version"
Reject { Update-SourceKit $clients.newer $cache } "requires at least 522"
Write-File (Join-Path $clients.hook ".git/hooks/post-merge") "#!/bin/sh`nprintf 'fixture hook edit' > source.txt`n"
Reject { Update-SourceKit $clients.hook $cache } "Local source changes"
Check ((Get-Content -Raw -LiteralPath (Join-Path $clients.hook "source.txt")) -eq "fixture hook edit") "actual post-merge edit detected before build"
foreach ($name in @("normal", "ignored", "untracked")) {
    Write-File (Join-Path $clients[$name] "private/keep.keystore") "fixture-secret"
    Write-File (Join-Path $clients[$name] "release-signing.properties") "private configuration"
    Write-File (Join-Path $clients[$name] "notes-personal.txt") "untracked note"
    $result = Update-SourceKit $clients[$name] $cache
    Check ($result -eq $clients[$name]) "uses current Git kit"
    Check ((Local-Git $clients[$name] @("rev-parse", "HEAD")) -eq $newHead) "fast-forward source"
    Check ((Get-Content -Raw -LiteralPath (Join-Path $clients[$name] "private/keep.keystore")) -eq "fixture-secret") "preserves ignored credentials"
    Check ((Get-Content -Raw -LiteralPath (Join-Path $clients[$name] "notes-personal.txt")) -eq "untracked note") "preserves unrelated untracked files"
}
Invoke-UpdatedBuild $clients.normal @{WorkDir=$cache;BuildOnly=$true;NonInteractive=$true;Serial="fixture-quest"}
Check ((Get-Content -Raw -LiteralPath (Join-Path $cache "build-invocation.txt")) -eq "True|True|True|fixture-quest|False") "updated builder receives APK-only flag and cache; debug remains default"
Invoke-UpdatedBuild $clients.normal @{WorkDir=$cache;BuildOnly=$true;Release=$true}
Check ((Get-Content -Raw -LiteralPath (Join-Path $cache "build-invocation.txt")) -eq "True|False|True||True") "release signing explicitly forwarded"
Write-File (Join-Path $clients.normal "tools/build-and-install.ps1") 'param([switch]$UpdateOnly) exit 7'
Reject { Invoke-UpdatedBuild $clients.normal @{} } "script failed"
Write-File (Join-Path $clients.normal "tools/build-and-install.ps1") 'param([switch]$BuildOnly) throw "must not run"'
Reject { Invoke-UpdatedBuild $clients.normal @{} } "does not support safe"
$zip = Join-Path $OutDir "ZIP source"
Set-Version $zip 520
Write-File (Join-Path $zip "release-signing.properties") "ZIP private file"
$zipCache = Join-Path $OutDir "ZIP cache"
$script:GitCalls.Clear()
Update-SourceKit $zip $zipCache -ReadOnly | Out-Null
Check (-not (Test-Path -LiteralPath $zipCache)) "ZIP dry run creates no cache"
$first = Update-SourceKit $zip $zipCache
$second = Update-SourceKit $zip $zipCache
Check ($first -eq $second) "ZIP reuses managed clone"
Check (@($script:GitCalls -match '^clone\|').Count -eq 1) "ZIP clone only once"
Check ((Get-Content -Raw -LiteralPath (Join-Path $zip "release-signing.properties")) -eq "ZIP private file") "ZIP private file untouched"
Set-Version $zip 523
Reject { Update-SourceKit $zip $zipCache } "requires at least 523"
Check ((Local-Git $first @("rev-parse", "HEAD")) -eq $newHead) "unpublished newer ZIP does not downgrade clone"
# New tracked files must not overwrite even ignored local credentials.
Write-File (Join-Path $clients['ignored-collision'] "private/new.txt") "local private file"
Write-File (Join-Path $seed "private/new.txt") "published file"
Local-Git $seed @("add", "-f", "private/new.txt") | Out-Null
Commit-Fixture $seed "new tracked file"
Local-Git $seed @("push", $script:TestRemote, "master") | Out-Null
Reject { Update-SourceKit $clients['ignored-collision'] $cache } "overwritten|abort|failed"
Check ((Get-Content -Raw -LiteralPath (Join-Path $clients['ignored-collision'] "private/new.txt")) -eq "local private file") "ignored collision preserved"
Write-File (Join-Path $seed "tools/update-required-assets.txt") "Asset format changed."
Commit-Fixture $seed "assets required"
Local-Git $seed @("push", $script:TestRemote, "master") | Out-Null
Reject { Update-SourceKit $clients.marker $cache } "requires updated bundled assets"
Check ((Local-Git $clients.marker @("rev-parse", "HEAD")) -eq $originalHead) "asset requirement before merge"
Check (-not ($script:GitCalls -match '(^|\|)(reset|clean|stash|--force)(\||$)')) "no destructive Git operations"
New-Item -ItemType Directory -Path (Join-Path $zip "tools") -Force | Out-Null
Copy-Item -LiteralPath $updater -Destination (Join-Path $zip "tools/update-and-install.ps1") -Force
$dryCache = Join-Path $OutDir "entrypoint dry cache"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $zip "tools/update-and-install.ps1") -WorkDir $dryCache -DryRun
Check ($LASTEXITCODE -eq 0) "full script dry-run succeeds"
Check (-not (Test-Path -LiteralPath $dryCache)) "full dry-run creates no build/cache directory"
foreach ($Release in @($false, $true)) {
    $assembledDir = Join-Path $OutDir "build variant"
    foreach ($name in @("buildVariant", "buildTask", "apk")) {
        $assignment = $builderAst.Find({ param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq ('$' + $name) }, $true)
        Check ($null -ne $assignment) "production build variant expression $name"
        . ([scriptblock]::Create($assignment.Extent.Text))
    }
    $expectedVariant = if ($Release) { "release" } else { "debug" }
    $expectedTask = if ($Release) { ":app:assembleRelease" } else { ":app:assembleDebug" }
    Check ($buildTask -eq $expectedTask) "correct Gradle task for $expectedVariant"
    Check ($apk -eq (Join-Path $assembledDir "android\app\build\outputs\apk\$expectedVariant\app-$expectedVariant.apk")) "correct APK path for $expectedVariant"
}

# Run the actual installer section with only the ADB process boundary replaced.
foreach ($name in @("Get-AdbArguments", "Invoke-AdbChecked", "Invoke-Checked", "Select-QuestDevice", "Assert-UpdateInstalled", "Write-Step")) {
    $definition = $builderAst.Find({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true)
    Check ($null -ne $definition) "installer dependency $name"
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Stop-DiagnosticLog { }
$builderText = Get-Content -Raw -LiteralPath $builder
$start = $builderText.IndexOf('    Write-Step 7 "Installing on the connected Quest"')
$end = $builderText.IndexOf('    Stop-DiagnosticLog', $start)
Check ($start -gt 0 -and $end -gt $start) "extract complete production installer"
$installBody = [scriptblock]::Create($builderText.Substring($start, $end - $start))
$script:adb = Join-Path $OutDir "fake-adb.ps1"
Write-File $script:adb @'
$line = $args -join '|'
Add-Content -LiteralPath $env:VCVR_FIXTURE_ADB_LOG -Value $line
if ($line -match 'devices') { Write-Output 'List of devices attached'; Write-Output 'fixture-quest device product:fixture'; exit 0 }
if ($line -match 'pm\|path') {
    if ($env:VCVR_FIXTURE_ADB_MODE -eq 'missing') { exit 0 }
    Write-Output 'package:/data/app/fixture/base.apk'; exit 0
}
if ($line -match 'install\|-r') {
    if ($env:VCVR_FIXTURE_ADB_MODE -eq 'signature') { Write-Output 'INSTALL_FAILED_UPDATE_INCOMPATIBLE'; exit 1 }
    if ($env:VCVR_FIXTURE_ADB_MODE -eq 'downgrade') { Write-Output 'INSTALL_FAILED_VERSION_DOWNGRADE'; exit 1 }
    Write-Output 'Success'; exit 0
}
if ($line -match 'content\|query') { Write-Output '_display_name=GTAVCsf1.b, _size=0'; exit 0 }
exit 0
'@
$Serial = "fixture-quest"; $apk = "fixture.apk"; $apkHash = "fixture-sha"; $LogPath = "fixture-log"
$saveProviderUri = "content://com.miamivr.quest.saves/slot/1"
$remoteGameData = "/fixture/gamedata"; $assembledDir = Join-Path $OutDir "assembled"
$SkipGameData = $true; $NonInteractive = $true; $UpdateOnly = $true
foreach ($mode in @("success", "signature", "downgrade", "missing")) {
    $env:VCVR_FIXTURE_ADB_MODE = $mode
    $env:VCVR_FIXTURE_ADB_LOG = Join-Path $OutDir "adb-$mode.log"
    if ($mode -eq "success") { & $installBody } else {
        $pattern = @{ signature="different signing key"; downgrade="newer app"; missing="already installed" }[$mode]
        Reject { & $installBody } $pattern
    }
    $calls = @(Get-Content -LiteralPath $env:VCVR_FIXTURE_ADB_LOG)
    Check (-not ($calls -match '(^|\|)(push|uninstall|clear|content|mkdir|start|monkey|-d)(\||$)')) "$mode no assets/data/bootstrap/destructive/launch command"
    Check (@($calls -match '\|install\|-r\|').Count -eq [int]($mode -ne "missing")) "$mode exact install count"
    Check (@($calls -match 'force-stop').Count -eq [int]($mode -eq "success")) "$mode only successful app left stopped"
}
$UpdateOnly = $false; $env:VCVR_FIXTURE_ADB_MODE = "success"
$env:VCVR_FIXTURE_ADB_LOG = Join-Path $OutDir "adb-normal-install.log"
foreach ($name in @("american.gxt","french.gxt","german.gxt","italian.gxt","russian.gxt","spanish.gxt")) { Write-File (Join-Path $assembledDir "gamefiles/TEXT/$name") "fixture" }
foreach ($name in @("fonts_r.txd","frontend_ds2.txd","frontend_ds3.txd","frontend_ds4.txd","frontend_x360.txd","frontend_xone.txd","generic.txd","particle.txd","ps3btns.txd","x360btns.txd")) { Write-File (Join-Path $assembledDir "gamefiles/models/$name") "fixture" }
foreach ($name in @("BigHandLeft.uxrh","BigHandRight.uxrh","BigHandsAlbedo.png")) { Write-File (Join-Path $assembledDir "gamefiles/models/vrhands/$name") "fixture" }
& $installBody
$normalCalls = @(Get-Content -LiteralPath $env:VCVR_FIXTURE_ADB_LOG)
Check (@($normalCalls -match '\|push\|').Count -eq 19) "ordinary SkipGameData retains 19 bundled asset writes"
Check (@($normalCalls -match '\|content\|query\|').Count -eq 1) "ordinary storage bootstrap unchanged"
$env:VCVR_FIXTURE_ADB_MODE = $null; $env:VCVR_FIXTURE_ADB_LOG = $null
Write-Host "PASS: $script:checks updater/installer checks; only isolated local Git fixtures and mocked ADB were used."
