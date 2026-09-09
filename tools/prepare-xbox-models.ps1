# Downloads the optional Xbox vehicle pack and builds a separate overlay.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [string]$WorkDir="C:\VCVRBuild\modern-assets",
    [string]$OutputDir,
    [string]$XboxArchive,
    [string]$AndroidSdk,
    [string]$Serial,
    [switch]$BuildOnly,
    [switch]$AcceptDownloads,
    [switch]$NonInteractive
)
$ErrorActionPreference="Stop"
. (Join-Path $PSScriptRoot "modelsets/xbox-modelset.ps1")
try {
    $game=(Resolve-Path -LiteralPath $GameDir).Path
    foreach ($relative in @("models/gta3.img","models/gta3.dir")) {
        if (-not (Test-Path -LiteralPath (Join-Path $game $relative) -PathType Leaf)) { throw "Select the legal original Vice City game folder; missing $relative" }
    }
    if (-not $OutputDir) { $OutputDir=Join-Path $game "modelsets/xbox" }
    $output=[IO.Path]::GetFullPath($OutputDir)
    if (Test-XboxOverlay $output) {
        Write-Host "Reusing the complete verified Xbox profile: $output"
    } else {
        $work=[IO.Path]::GetFullPath($WorkDir)
        $downloads=Join-Path $work "downloads"
        New-Item -ItemType Directory -Path $downloads -Force | Out-Null
        $archive=Join-Path $downloads "Fixed_Xbox_Vehs_1.3.rar"
        if ($XboxArchive) {
            $archive=[IO.Path]::GetFullPath($XboxArchive)
            Assert-XboxArchive $archive
        } else {
            $valid=$false
            try { Assert-XboxArchive $archive; $valid=$true } catch { }
            if (-not $valid) {
                if (-not $AcceptDownloads) {
                    if ($NonInteractive) { throw "Pass -AcceptDownloads or a verified -XboxArchive in non-interactive mode." }
                    $answer=Read-Host "Download the optional 35 MB Fixed Xbox Vehicles 1.3 pack? [Y/n]"
                    if ($answer -and $answer -notmatch '^[Yy]') { throw "Xbox download cancelled; no Quest data changed." }
                }
                $partial="$archive.partial"
                if ((Test-Path -LiteralPath $partial) -and (Get-Item -LiteralPath $partial).Length -gt $XboxArchiveSize) { Remove-Item -LiteralPath $partial -Force }
                $partialReady=$false
                if ((Test-Path -LiteralPath $partial) -and (Get-Item -LiteralPath $partial).Length -eq $XboxArchiveSize) {
                    if ((Get-XboxHash $partial) -eq $XboxArchiveSha256) { $partialReady=$true }
                    else { Remove-Item -LiteralPath $partial -Force }
                }
                if (-not $partialReady) {
                    $curl=(Get-Command curl.exe -ErrorAction Stop).Source
                    Invoke-XboxTool $curl @("--fail","--location","--retry","5","--retry-all-errors","--continue-at","-","--output",$partial,$XboxArchiveUrl)
                }
                Assert-XboxArchive $partial
                Move-Item -LiteralPath $partial -Destination $archive -Force
            }
        }
        $sources=Join-Path $work "sources"
        New-Item -ItemType Directory -Path $sources -Force | Out-Null
        $extract=Join-Path $sources "xbox-pack"
        $main=Join-Path $extract "Fixed Xbox Vehicles"
        $valid=$false
        try { Assert-XboxSource $main; $valid=$true } catch { }
        if (-not $valid) {
            $id=[Guid]::NewGuid().ToString("N")
            $fresh=Join-Path $sources ".xbox.source-$id"
            New-Item -ItemType Directory -Path $fresh | Out-Null
            try {
                # The archive identity is checked before extraction. Only the
                # main vehicles are extracted; optional prop/cutscene variants
                # and unrelated archive content are never copied to the profile.
                Assert-XboxArchive $archive
                $tar=(Get-Command tar.exe -ErrorAction Stop).Source
                Invoke-XboxTool $tar @("-xf",$archive,"-C",$fresh,"Fixed Xbox Vehicles")
                Assert-XboxSource (Join-Path $fresh "Fixed Xbox Vehicles")
                if (Test-Path -LiteralPath $extract) {
                    $old=Join-Path $sources ".xbox.previous-$id"
                    Move-Item -LiteralPath $extract -Destination $old
                    try { Move-Item -LiteralPath $fresh -Destination $extract }
                    catch { Move-Item -LiteralPath $old -Destination $extract; throw }
                    Remove-XboxTemporary $old $sources
                } else { Move-Item -LiteralPath $fresh -Destination $extract }
            } finally { Remove-XboxTemporary $fresh $sources }
        }
        & (Join-Path $PSScriptRoot "modelsets/build-xbox-modelset.ps1") -Source $main -Out $output -Force
        if (-not $? -or -not (Test-XboxOverlay $output)) { throw "Xbox profile build failed." }
    }
    if ($BuildOnly) { Write-Host "Xbox profile ready; no device commands were sent."; return }
    $options=@{ModernDir=$output;Profile="Xbox";NonInteractive=$NonInteractive}
    if ($AndroidSdk) { $options.AndroidSdk=$AndroidSdk }
    if ($Serial) { $options.Serial=$Serial }
    & (Join-Path $PSScriptRoot "install-modern-models.ps1") @options
    if (-not $?) { throw "Xbox profile installation failed." }
} catch { Write-Error $_; exit 1 }
