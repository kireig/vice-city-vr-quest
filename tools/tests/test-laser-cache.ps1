param(
    [string]$KitRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vice-city-laser-cache-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Assert-PlainPath([string]$Path) {
    $probe = [IO.Path]::GetFullPath($Path)
    if (-not [IO.Path]::IsPathRooted($Path) -or $probe -cne $Path) {
        throw "An absolute canonical path is required: $Path"
    }
    while ($probe) {
        if (Test-Path -LiteralPath $probe) {
            if (((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse points are not supported by this test: $probe"
            }
        }
        $parent = [IO.Directory]::GetParent($probe)
        if ($null -eq $parent) { break }
        $probe = $parent.FullName
    }
}

$KitRoot = [IO.Path]::GetFullPath($KitRoot)
$OutDir = [IO.Path]::GetFullPath($OutDir)
Assert-PlainPath $KitRoot
Assert-PlainPath $OutDir
if (Test-Path -LiteralPath $OutDir) { throw 'Test output already exists; choose a new output directory.' }
$source = Join-Path $KitRoot 'overlay\src\vr\QuestWeaponVR.cpp'
$fixtureTemplate = Join-Path $PSScriptRoot 'laser-cache-fixture.cpp.in'
Assert-PlainPath $source
Assert-PlainPath $fixtureTemplate
$text = [IO.File]::ReadAllText($source)
$template = [IO.File]::ReadAllText($fixtureTemplate)
$parts = [ordered]@{
    '@@PRODUCTION_CACHE@@' = '(?s)struct WeaponLaserOverride\s*\{.*?static WeaponLaserOverride gWeaponLaserOverrides\[2\]\[WEAPONTYPE_TOTALWEAPONS\];'
    '@@PRODUCTION_READ_WRITE@@' = '(?s)static void\s+WeaponLaserSection\(.*?(?=static bool\s+CalibrationConfigured\()'
    '@@PRODUCTION_INVALIDATION@@' = '(?s)void\s+InvalidateQuestWeaponLaserOverrides\(\)\s*\{.*?\n\}'
    '@@PRODUCTION_PUBLIC_API@@' = '(?s)bool\s+IsTrackedWeaponLaserEnabledForType\(.*?(?=bool IsTrackedScopeActive\()'
}
foreach ($part in $parts.GetEnumerator()) {
    $matches = [regex]::Matches($text, $part.Value)
    if ($matches.Count -ne 1) { throw "Expected exactly one production region: $($part.Key)" }
    $template = $template.Replace($part.Key, $matches[0].Value)
}
if ($template.Contains('@@PRODUCTION_')) { throw 'Unexpanded production fixture marker.' }
foreach ($hook in @('LoadSettings', 'PrepareForGameShutdown')) {
    if ($text -notmatch ('(?s)' + $hook + '\(\)\s*\{.{0,100}InvalidateQuestWeaponLaserOverrides\(\);')) {
        throw "Missing cache lifecycle hook: $hook"
    }
}
$menuPath = Join-Path $KitRoot 'overlay\src\skel\android\vrdebug.cpp'
Assert-PlainPath $menuPath
$menu = [IO.File]::ReadAllText($menuPath)
if ($menu -notmatch 'OculusVR::InvalidateQuestWeaponLaserOverrides\(\);') {
    throw 'The VR menu must invalidate cached overrides when it opens.'
}

$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio 2022 or Build Tools with the Desktop development with C++ workload is required.'
}
$install = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if ($LASTEXITCODE -ne 0 -or $install.Count -ne 1 -or [string]::IsNullOrWhiteSpace($install[0])) {
    throw 'Cannot locate a Visual Studio installation with the x64 C++ tools.'
}
$toolchainRoot = Join-Path $install[0] 'VC\Tools\MSVC'
$versions = @(Get-ChildItem -LiteralPath $toolchainRoot -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') -PathType Leaf
} | Sort-Object { [version]$_.Name } -Descending)
if ($versions.Count -eq 0) { throw 'No x64-hosted MSVC compiler was found.' }
$vc = $versions[0].FullName
$compiler = Join-Path $vc 'bin\Hostx64\x64\cl.exe'
$sdk = Join-Path $programFilesX86 'Windows Kits\10'
$sdkRegistry = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue
if ($sdkRegistry -and $sdkRegistry.PSObject.Properties.Name -contains 'KitsRoot10') {
    $sdk = $sdkRegistry.KitsRoot10.TrimEnd('\')
}
$sdkVersions = @(Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object {
    (Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\stdlib.h') -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $sdk ('Lib\' + $_.Name + '\um\x64\kernel32.lib')) -PathType Leaf)
} | Sort-Object { [version]$_.Name } -Descending)
if ($sdkVersions.Count -eq 0) { throw 'Windows 10/11 SDK headers and x64 libraries are required.' }
$sdkVersion = $sdkVersions[0].Name

[void][IO.Directory]::CreateDirectory($OutDir)
Assert-PlainPath $OutDir
$fixture = Join-Path $OutDir 'laser_cache_test.cpp'
[IO.File]::WriteAllText($fixture, $template, (New-Object Text.UTF8Encoding($false)))
$exe = Join-Path $OutDir 'laser_cache_test.exe'
$obj = Join-Path $OutDir 'laser_cache_test.obj'
$compileOutput = & $compiler /nologo /EHsc /W4 /WX /std:c++17 /MT /O2 `
    ('/I' + (Join-Path $vc 'include')) `
    ('/I' + (Join-Path $sdk ('Include\' + $sdkVersion + '\ucrt'))) `
    $fixture ('/Fo' + $obj) ('/Fe' + $exe) /link `
    ('/LIBPATH:' + (Join-Path $vc 'lib\x64')) `
    ('/LIBPATH:' + (Join-Path $sdk ('Lib\' + $sdkVersion + '\ucrt\x64'))) `
    ('/LIBPATH:' + (Join-Path $sdk ('Lib\' + $sdkVersion + '\um\x64'))) 2>&1
$compileCode = $LASTEXITCODE
$compileOutput | ForEach-Object { Write-Host $_ }
if ($compileCode -ne 0) { throw "Host fixture compilation failed: $compileCode" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Host fixture failed: $LASTEXITCODE" }
Write-Host ('Production source SHA256: ' + (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash)
Write-Host "Test artifacts: $OutDir"
Write-Host 'Only the host fixture was compiled and run; no game, headset or shared build was changed.'
