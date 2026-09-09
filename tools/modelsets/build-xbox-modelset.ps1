# Builds a compact PC-compatible vehicle overlay; inputs are never changed.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Out,
    [switch]$Force
)
$ErrorActionPreference="Stop"
Set-StrictMode -Version 2.0
. (Join-Path $PSScriptRoot "xbox-modelset.ps1")

$sourceRoot=(Resolve-Path -LiteralPath $Source).Path.TrimEnd('\','/')
if (Test-Path -LiteralPath (Join-Path $sourceRoot "Fixed Xbox Vehicles/gta3.img") -PathType Container) {
    $sourceRoot=Join-Path $sourceRoot "Fixed Xbox Vehicles"
}
Assert-XboxSource $sourceRoot
$outRoot=[IO.Path]::GetFullPath($Out).TrimEnd('\','/')
if ([IO.Path]::GetFileName($outRoot) -cne "xbox") { throw "Use a separate output folder named xbox (normally game/modelsets/xbox)." }
foreach ($pair in @(@($sourceRoot,$outRoot),@($outRoot,$sourceRoot))) {
    if (($pair[0]+[IO.Path]::DirectorySeparatorChar).StartsWith($pair[1]+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw "The Xbox output and source folders must not overlap."
    }
}
if (Test-Path -LiteralPath $outRoot) {
    if (-not $Force) { throw "Output exists; use -Force for a staged replacement: $outRoot" }
    $info=Join-Path $outRoot "BUILD_INFO.txt"
    if (-not (Test-Path -LiteralPath $info -PathType Leaf) -or [IO.File]::ReadAllLines($info) -cnotcontains "Profile=XBOX") {
        throw "Refusing to replace a folder that is not a generated Xbox profile: $outRoot"
    }
}
$vehicles=Join-Path $sourceRoot "gta3.img"
$files=@(Get-ChildItem -LiteralPath $vehicles -File | Sort-Object Name)
if ($files.Count -ne 214) { throw "The main Fixed Xbox Vehicles folder must contain 214 vehicle files." }
$models=@($files | Where-Object Extension -eq ".dff" | ForEach-Object { $_.BaseName.ToLowerInvariant() })
if ($models.Count -ne 107) { throw "Expected 107 main-pack vehicle DFFs." }
foreach ($file in $files) {
    if ($file.Name -notmatch '^[a-zA-Z0-9_]{1,19}\.(dff|txd)$') { throw "Unexpected file in main vehicle folder: $($file.Name)" }
}
foreach ($model in $models) {
    if (-not (Test-Path -LiteralPath (Join-Path $vehicles "$model.txd") -PathType Leaf)) { throw "Missing TXD for $model" }
}
foreach ($relative in @("models/coll/vehicles.col","models/generic/wheels.dff","models/generic/wheels.txd")) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $relative) -PathType Leaf)) { throw "Missing Xbox source file: $relative" }
}
$compressor=Join-Path $PSScriptRoot "txdcompress.exe"
if (-not (Test-Path -LiteralPath $compressor -PathType Leaf)) { throw "Bundled texture compressor is missing." }
$parent=Split-Path -Parent $outRoot
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$id=[Guid]::NewGuid().ToString("N")
$stage=Join-Path $parent ".xbox.build-$id"
$work=Join-Path $parent ".xbox.source-$id"
$backup=Join-Path $parent ".xbox.previous-$id"
$committed=$false
try {
    New-Item -ItemType Directory -Path $stage,$work,(Join-Path $stage "models/coll"),(Join-Path $stage "models/generic") -Force | Out-Null
    foreach ($file in $files) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $work $file.Name.ToLowerInvariant()) }
    foreach ($relative in @("models/coll/vehicles.col","models/generic/wheels.dff","models/generic/wheels.txd")) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot $relative) -Destination (Join-Path $stage $relative)
    }
    # This pack is already D3D8/PC. Only staged TXDs are compressed; native
    # geometry, material flags, and COLL records retain their authored bytes.
    foreach ($txd in @(Get-ChildItem -LiteralPath $work -File -Filter "*.txd")+@(Get-Item -LiteralPath (Join-Path $stage "models/generic/wheels.txd"))) {
        Invoke-XboxTool $compressor @($txd.FullName)
    }
    $img=[IO.File]::Create((Join-Path $stage "models/gta3.img"))
    $dir=[IO.File]::Create((Join-Path $stage "models/gta3.dir"))
    $writer=New-Object IO.BinaryWriter($dir)
    try {
        $sector=0L
        foreach ($file in @(Get-ChildItem -LiteralPath $work -File | Sort-Object Name)) {
            $bytes=[IO.File]::ReadAllBytes($file.FullName)
            $size=[int][Math]::Ceiling($bytes.Length/2048.0)
            $writer.Write([uint32]$sector); $writer.Write([uint32]$size)
            $name=New-Object byte[] 24
            [Text.Encoding]::ASCII.GetBytes($file.Name).CopyTo($name,0)
            $writer.Write($name)
            $img.Write($bytes,0,$bytes.Length)
            $padding=New-Object byte[] ($size*2048-$bytes.Length)
            $img.Write($padding,0,$padding.Length)
            $sector+=$size
        }
    } finally { $writer.Dispose(); $img.Dispose(); $dir.Dispose() }
    $utf8=New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $stage "vehicle_models.txt"),(($models | Sort-Object) -join "`n")+"`n",$utf8)
    Assert-XboxImg $stage
    $lines=@("Vice City VR Xbox vehicle profile","Profile=XBOX","BuilderVersion=$XboxBuilderVersion","SourceArchiveSHA256=$XboxArchiveSha256","VehicleCount=107","ArchiveEntries=214")
    foreach ($relative in $XboxOutputFiles) { $lines+="SHA256 $relative=$(Get-XboxHash (Join-Path $stage $relative))" }
    $lines+="Assets are supplied by the player from the external pack; do not redistribute this generated folder."
    [IO.File]::WriteAllText((Join-Path $stage "BUILD_INFO.txt"),($lines -join "`n")+"`n",$utf8)
    if (-not (Test-XboxOverlay $stage)) { throw "Completed Xbox profile failed validation." }
    if (Test-Path -LiteralPath $outRoot) { Move-Item -LiteralPath $outRoot -Destination $backup }
    try { Move-Item -LiteralPath $stage -Destination $outRoot; $committed=$true }
    catch {
        if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $outRoot)) { Move-Item -LiteralPath $backup -Destination $outRoot }
        throw
    }
    Remove-XboxTemporary $backup $parent
    Write-Host "Xbox profile built and verified: $outRoot (107 vehicles; Modern untouched)."
} finally {
    Remove-XboxTemporary $work $parent
    if (-not $committed) { Remove-XboxTemporary $stage $parent }
}
