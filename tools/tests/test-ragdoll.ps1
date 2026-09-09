param(
    [string]$KitRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('vice-city-ragdoll-' + [Guid]::NewGuid().ToString('N'))),
    [string[]]$Sources = @('test-ragdoll-physics.cpp', 'test-ragdoll-anatomy.cpp', 'test-ragdoll-self-contact.cpp', 'test-ragdoll-pose.cpp', 'test-ragdoll-impact.cpp', 'vehicle-contact-tests.cpp', 'test-ragdoll-contacts.cpp', 'test-ragdoll-ray.cpp', 'test-ragdoll-motion.cpp', 'test-ragdoll-torque.cpp', 'test-ragdoll-reaction.cpp', 'test-ragdoll-brake.cpp', 'test-ragdoll-hood.cpp')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$KitRoot = [IO.Path]::GetFullPath($KitRoot)
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (Test-Path -LiteralPath $OutDir) { throw 'Test output already exists; choose a new output directory.' }
$physicsDir = Join-Path $KitRoot 'overlay\src\peds'
if (-not (Test-Path -LiteralPath (Join-Path $physicsDir 'VrRagdollPhysics.h'))) {
    $physicsDir = Join-Path $KitRoot 'src\peds'
}
if (-not (Test-Path -LiteralPath (Join-Path $physicsDir 'VrRagdollPhysics.h'))) {
    $physicsDir = Join-Path $KitRoot 'src'
}
if (-not (Test-Path -LiteralPath (Join-Path $physicsDir 'VrRagdollPhysics.h'))) {
    throw 'Cannot locate the production VrRagdollPhysics.h header.'
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
foreach ($sourceName in $Sources) {
    $source = Join-Path $PSScriptRoot $sourceName
    $stem = [IO.Path]::GetFileNameWithoutExtension($sourceName)
    $exe = Join-Path $OutDir ($stem + '.exe')
    $obj = Join-Path $OutDir ($stem + '.obj')
    & $compiler /nologo /EHsc /W4 /WX /std:c++17 /MT /O2 `
        ('/I' + $physicsDir) ('/I' + $PSScriptRoot) `
        ('/I' + (Join-Path $vc 'include')) `
        ('/I' + (Join-Path $sdk ('Include\' + $sdkVersion + '\ucrt'))) `
        $source ('/Fo' + $obj) ('/Fe' + $exe) /link `
        ('/LIBPATH:' + (Join-Path $vc 'lib\x64')) `
        ('/LIBPATH:' + (Join-Path $sdk ('Lib\' + $sdkVersion + '\ucrt\x64'))) `
        ('/LIBPATH:' + (Join-Path $sdk ('Lib\' + $sdkVersion + '\um\x64')))
    if ($LASTEXITCODE -ne 0) { throw "Host compilation failed: $sourceName ($LASTEXITCODE)" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Host fixture failed: $sourceName ($LASTEXITCODE)" }
}
Write-Host ('Production solver SHA256: ' + (Get-FileHash -LiteralPath (Join-Path $physicsDir 'VrRagdollPhysics.h') -Algorithm SHA256).Hash)
Write-Host "Host-only test artifacts: $OutDir"
