# Shared format and cache checks for the optional Xbox vehicle profile.
$XboxBuilderVersion = "0.5.6-xbox-1"
$XboxArchiveSize = 34822164L
$XboxArchiveSha256 = "0F719F6FA4C51DD5EA65ED1C08F77BDF5B6D81154574DC2A10C1C31A746E308A"
$XboxArchiveUrl = "https://drive.usercontent.google.com/download?id=1wqUggAT4VJGW6RHJiXhft8Ni8uxe3N5v&export=download&confirm=t"
$XboxOutputFiles = @("models/gta3.img", "models/gta3.dir", "models/coll/vehicles.col", "models/generic/wheels.dff", "models/generic/wheels.txd", "vehicle_models.txt")
$XboxPack = Get-Content -LiteralPath (Join-Path $PSScriptRoot "xbox-pack.json") -Raw | ConvertFrom-Json

function Get-XboxHash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-XboxArchive([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-Item -LiteralPath $Path).Length -ne $XboxArchiveSize -or
        (Get-XboxHash $Path) -ne $XboxArchiveSha256) {
        throw "Xbox archive does not match the pinned Fixed_Xbox_Vehs_1.3.rar size/SHA256: $Path"
    }
}

function Assert-XboxSource([string]$Root) {
    $files=@(Get-ChildItem -LiteralPath $Root -Recurse -File -Force)
    if ($files.Count -ne 217 -or @(Get-ChildItem -LiteralPath $Root -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw "Unexpected files or links in the extracted Xbox main pack."
    }
    foreach ($entry in $XboxPack.files.PSObject.Properties) {
        $path=Join-Path $Root $entry.Name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-XboxHash $path) -cne $entry.Value) {
            throw "Extracted Xbox source failed SHA256 verification: $($entry.Name)"
        }
    }
}

function Assert-XboxImg([string]$Root) {
    $img = Join-Path $Root "models/gta3.img"
    $dir = Join-Path $Root "models/gta3.dir"
    $bytes = [IO.File]::ReadAllBytes($dir)
    if ($bytes.Length -ne 214*32) { throw "Xbox archive must contain exactly 107 DFF/TXD pairs." }
    $length = (Get-Item -LiteralPath $img).Length
    $names = @{}
    $expectedSector = 0L
    for ($i=0; $i -lt 214; $i++) {
        $offset = $i*32
        $sector = [int64][BitConverter]::ToUInt32($bytes,$offset)
        $size = [int64][BitConverter]::ToUInt32($bytes,$offset+4)
        $name = [Text.Encoding]::ASCII.GetString($bytes,$offset+8,24).Split([char]0)[0]
        if ($name -cnotmatch '^[a-z0-9_]{1,19}\.(dff|txd)$' -or $names.ContainsKey($name) -or
            $sector -ne $expectedSector -or $size -le 0 -or ($sector+$size)*2048 -gt $length) {
            throw "Invalid Xbox archive directory entry: $name"
        }
        $names[$name] = $true
        $expectedSector = $sector+$size
    }
    if ($expectedSector*2048 -ne $length) { throw "Xbox IMG has an invalid trailing size." }
    $models = @([IO.File]::ReadAllLines((Join-Path $Root "vehicle_models.txt")))
    if ($models.Count -ne 107 -or @($models | Sort-Object -Unique).Count -ne 107) { throw "Invalid Xbox vehicle manifest." }
    foreach ($name in $models) {
        if (-not $names.ContainsKey("$name.dff") -or -not $names.ContainsKey("$name.txd")) { throw "Unpaired Xbox vehicle: $name" }
    }
}

function Test-XboxOverlay([string]$Root) {
    try {
        if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return $false }
        $info = [IO.File]::ReadAllLines((Join-Path $Root "BUILD_INFO.txt"))
        if ($info -cnotcontains "Profile=XBOX" -or $info -cnotcontains "BuilderVersion=$XboxBuilderVersion" -or
            $info -cnotcontains "SourceArchiveSHA256=$XboxArchiveSha256") { return $false }
        foreach ($relative in $XboxOutputFiles) {
            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0 -or
                $info -cnotcontains "SHA256 $relative=$(Get-XboxHash $path)") { return $false }
        }
        # Only these generated files are part of the transferable profile.
        if (@(Get-ChildItem -LiteralPath $Root -Recurse -File -Force).Count -ne 7) { return $false }
        if (@(Get-ChildItem -LiteralPath $Root -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { return $false }
        Assert-XboxImg $Root
        return $true
    } catch { return $false }
}

function Invoke-XboxTool([string]$Tool,[string[]]$Arguments) {
    $saved = $ErrorActionPreference
    try { $ErrorActionPreference="Continue"; & $Tool @Arguments; $code=$LASTEXITCODE }
    finally { $ErrorActionPreference=$saved }
    if ($code -ne 0) { throw "Tool failed (exit $code): $Tool" }
}

function Remove-XboxTemporary([string]$Path,[string]$Parent) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $expected = [IO.Path]::GetFullPath($Parent).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($expected,[StringComparison]::OrdinalIgnoreCase) -or
        ([IO.Path]::GetFileName($resolved)) -notmatch '^\.xbox\.(build|source|previous)-[0-9a-f]+$') {
        throw "Refusing an unexpected temporary directory: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
