# Updates this source kit, then builds and installs only its APK.
[CmdletBinding()]
param(
    [string]$WorkDir = "C:\VCVRBuild",
    [string]$AndroidSdk,
    [string]$JavaHome,
    [string]$Serial,
    [switch]$BuildOnly,
    [switch]$Release,
    [switch]$NonInteractive,
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Invoke-UpdateGit {
    param([string]$Directory, [string[]]$Arguments)
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = @(& $script:UpdateGit -C $Directory @Arguments 2>&1)
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($code -ne 0) {
        throw "Git $($Arguments[0]) failed (exit $code). $($output -join [Environment]::NewLine)"
    }
    # Git progress and warnings on stderr are not command results.
    $stdout = @($output | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] })
    $output | Where-Object { $_ -is [Management.Automation.ErrorRecord] } | ForEach-Object { Write-Host $_ }
    return ($stdout -join [Environment]::NewLine).Trim()
}

function Find-UpdateGit {
    param([string]$CacheRoot, [switch]$ReadOnly)
    $installed = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -ne $installed) { return $installed.Source }
    $version = "2.55.0.3"
    $gitDirectory = Join-Path $CacheRoot ".tools/MinGit-$version"
    $gitExecutable = Join-Path $gitDirectory "cmd/git.exe"
    if (Test-Path -LiteralPath $gitExecutable -PathType Leaf) { return $gitExecutable }
    if ($ReadOnly) { return $null }
    $downloadDirectory = Join-Path $CacheRoot ".downloads"
    $archive = Join-Path $downloadDirectory "MinGit-$version-64-bit.zip"
    New-Item -ItemType Directory -Path $downloadDirectory -Force | Out-Null
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        Write-Host "Downloading portable Git once; it will share the build tool cache."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/git-for-windows/git/releases/download/v2.55.0.windows.3/MinGit-2.55.0.3-64-bit.zip" -OutFile $archive
    }
    $expected = "F48E2D2DC74A24454ADC6D8FD0AC25BF9C2386F19CFB06202B9465AAAD4F9F05"
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash -ne $expected) {
        throw "Portable Git archive checksum failed: $archive. No update was attempted."
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $gitDirectory -Force
    if (-not (Test-Path -LiteralPath $gitExecutable -PathType Leaf)) { throw "Portable Git extraction failed." }
    return $gitExecutable
}

function Get-UpdateVersion {
    param([string]$GradleText)
    $versions = [regex]::Matches($GradleText, '(?m)^\s*versionCode\s*=\s*(\d+)\s*$')
    if ($versions.Count -ne 1) { throw "Cannot verify the source kit versionCode; no build or installation will run." }
    return [int]$versions[0].Groups[1].Value
}

function Get-LocalUpdateVersion {
    param([string]$Directory)
    $file = Join-Path $Directory "overlay/android/app/build.gradle.kts"
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Incomplete source kit: $file is missing." }
    return Get-UpdateVersion (Get-Content -Raw -LiteralPath $file)
}

function Assert-UpdateCheckout {
    param([string]$Directory)
    $top = Invoke-UpdateGit $Directory @("rev-parse", "--show-toplevel")
    if ([IO.Path]::GetFullPath($top).TrimEnd('\','/') -ne [IO.Path]::GetFullPath($Directory).TrimEnd('\','/')) {
        throw "The update directory belongs to a different enclosing Git repository. It was left untouched."
    }
    $remote = Invoke-UpdateGit $Directory @("remote", "get-url", "origin")
    $allowedRemotes = @(
        "https://github.com/dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git",
        "https://github.com/dubrovskiy-yevhen-stakelogic/vice-city-vr-quest",
        "git@github.com:dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git"
    )
    if ($remote -cnotin $allowedRemotes) { throw "Origin is not the official Vice City VR source kit. No remote was changed." }
    $branch = Invoke-UpdateGit $Directory @("symbolic-ref", "--short", "HEAD")
    if ($branch -cne "master") { throw "UPDATE requires the master branch. Your current branch was left untouched." }
    $changes = Invoke-UpdateGit $Directory @("status", "--porcelain", "--untracked-files=no")
    if ($changes) { throw "Local source changes were found. Commit or back them up yourself before updating; UPDATE does not reset or stash them." }
    return Invoke-UpdateGit $Directory @("rev-parse", "HEAD")
}

function Update-SourceKit {
    param([string]$LocalKit, [string]$CacheRoot, [switch]$ReadOnly)
    $minimumVersion = [Math]::Max(520, (Get-LocalUpdateVersion $LocalKit))
    $source = $LocalKit
    if (-not (Test-Path -LiteralPath (Join-Path $source ".git"))) {
        $source = Join-Path $CacheRoot "source-kit"
        Write-Host "ZIP source kit: updates use the persistent Git copy at $source"
        Write-Host "This ZIP folder and its private files stay unchanged."
        if (Test-Path -LiteralPath $source) {
            if (-not (Test-Path -LiteralPath (Join-Path $source ".git"))) {
                throw "The managed source-kit directory already exists without Git. It was left untouched: $source"
            }
        } elseif ($ReadOnly) {
            Write-Host "DRY RUN: would clone the official master branch once, then verify versionCode >= $minimumVersion."
            return $null
        } else {
            New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
            Invoke-UpdateGit $CacheRoot @("clone", "--single-branch", "--branch", "master", "https://github.com/dubrovskiy-yevhen-stakelogic/vice-city-vr-quest.git", $source) | Out-Host
        }
    }
    $before = Assert-UpdateCheckout $source
    $minimumVersion = [Math]::Max($minimumVersion, (Get-LocalUpdateVersion $source))
    if ($ReadOnly) {
        Write-Host "DRY RUN: would fetch master, require a fast-forward and versionCode >= $minimumVersion, then build using $CacheRoot."
        Write-Host "DRY RUN: APK update only; no commands were sent to a Quest."
        return $null
    }
    Write-Host "Fetching source changes..."
    Invoke-UpdateGit $source @("fetch", "origin", "master") | Out-Host
    $revision = Invoke-UpdateGit $source @("rev-parse", "FETCH_HEAD")
    $version = Get-UpdateVersion (Invoke-UpdateGit $source @("show", "${revision}:overlay/android/app/build.gradle.kts"))
    if ($version -lt $minimumVersion) {
        throw "The published source is versionCode $version; this kit requires at least $minimumVersion. The new version may not be published yet, or your local kit is newer. No build or installation was started."
    }
    $assetsMarker = Invoke-UpdateGit $source @("ls-tree", "--name-only", $revision, "--", "tools/update-required-assets.txt")
    if ($assetsMarker) {
        throw "This source version requires updated bundled assets. UPDATE leaves game data unchanged. Download that release and run its normal BUILD_AND_INSTALL separately after reading its asset instructions."
    }
    $base = Invoke-UpdateGit $source @("merge-base", $before, $revision)
    if ($base -ne $before) { throw "Local commits diverge from the published version. No merge, reset, build or installation was attempted." }
    $checkedHead = Assert-UpdateCheckout $source
    if ($checkedHead -ne $before) { throw "The source checkout changed during the update. Retry after the other Git operation finishes." }
    Invoke-UpdateGit $source @("-c", "merge.autostash=false", "merge", "--ff-only", "--no-overwrite-ignore", $revision) | Out-Host
    if ((Assert-UpdateCheckout $source) -ne $revision) {
        throw "The source checkout changed after the merge. No build or installation was started."
    }
    if ((Get-LocalUpdateVersion $source) -ne $version) { throw "Updated version verification failed; no build will run." }
    Write-Host "Source ready: versionCode $version. Reusing downloaded build tools and the clean reVC base."
    return $source
}

function Invoke-UpdatedBuild {
    param([string]$Source, [hashtable]$Options)
    $builder = Join-Path $Source "tools/build-and-install.ps1"
    if (-not (Test-Path -LiteralPath $builder -PathType Leaf)) { throw "The updated kit is missing its build script." }
    $command = Get-Command -Name $builder -ErrorAction Stop
    if (-not $command.Parameters.ContainsKey("UpdateOnly")) {
        throw "The published builder does not support safe APK-only updates yet. No installation was attempted."
    }
    if ($Options.ContainsKey("Release") -and $Options.Release -and -not $command.Parameters.ContainsKey("Release")) {
        throw "The published builder does not support release signing yet. No installation was attempted."
    }
    & $builder @Options -UpdateOnly
    if (-not $?) { throw "The build/update script failed. Read its diagnostic log." }
}

try {
    $cache = [IO.Path]::GetFullPath($WorkDir)
    $localKit = Split-Path $PSScriptRoot -Parent
    $script:UpdateGit = Find-UpdateGit -CacheRoot $cache -ReadOnly:$DryRun
    if ($DryRun -and -not $script:UpdateGit) {
        Write-Host "DRY RUN: Git is not installed. An actual run would download and verify portable Git once in $cache."
        return
    }
    $source = Update-SourceKit -LocalKit $localKit -CacheRoot $cache -ReadOnly:$DryRun
    if ($DryRun) { return }
    $options = @{ WorkDir = $cache; BuildOnly = $BuildOnly; NonInteractive = $NonInteractive }
    if ($Release) { $options.Release = $true }
    if ($AndroidSdk) { $options.AndroidSdk = $AndroidSdk }
    if ($JavaHome) { $options.JavaHome = $JavaHome }
    if ($Serial) { $options.Serial = $Serial }
    Invoke-UpdatedBuild -Source $source -Options $options
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
